#include "canbus/canbus.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "canbus/canbus_config.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/ui.h"

static const char *TAG = "canbus";

#define CANBUS_RX_POLL_TIMEOUT_MS       100
#define CANBUS_UI_REFRESH_PERIOD_MS     200
#define CANBUS_STATUS_REFRESH_PERIOD_MS 250
#define CANBUS_SELF_TEST_STD_ID         0x123
#define CANBUS_SELF_TEST_EXT_ID         0x18FF50E5
#define CANBUS_ACTIVE_ACK_PROBE_ID      0x18FF00E5
#define CANBUS_BASIC_JOYSTICK_PGN       0xFDD6U
#define CANBUS_EXTENDED_JOYSTICK_PGN    0xFDD7U
#define CANBUS_TARGET_FRAME_DLC         8U
#define CANBUS_J1939_REQUEST_ID         0x18EAFFA5U
#define CANBUS_J1939_GLOBAL_DM13_ID     0x18DFFFA5U

static ui_can_sniffer_state_t s_sniffer_state;
static TickType_t s_last_rx_log_tick;
static uint32_t s_suppressed_rx_logs;
static TickType_t s_last_decoded_log_tick;
static uint32_t s_suppressed_decoded_logs;
static uint8_t s_self_test_sequence;
static bool s_recovery_in_progress;
static TickType_t s_last_basic_target_tick;
static TickType_t s_last_extended_target_tick;
static TickType_t s_last_target_tick;
#if CANBUS_ENABLE_RX_EDGE_PROBE
static volatile uint32_t s_rx_edge_probe_count;
#if CANBUS_ENABLE_AUX_RX_EDGE_PROBE
static volatile uint32_t s_aux_rx_edge_probe_count;
#endif
#endif
static TickType_t s_last_error_alert_log_tick;

static uint32_t canbus_get_alert_mask(void)
{
    return TWAI_ALERT_RX_DATA |
           TWAI_ALERT_TX_SUCCESS |
           TWAI_ALERT_TX_FAILED |
           TWAI_ALERT_TX_IDLE |
           TWAI_ALERT_ERR_PASS |
           TWAI_ALERT_BUS_ERROR |
           TWAI_ALERT_RX_QUEUE_FULL |
           TWAI_ALERT_BUS_OFF |
           TWAI_ALERT_RX_FIFO_OVERRUN |
           TWAI_ALERT_ABOVE_ERR_WARN |
           TWAI_ALERT_BELOW_ERR_WARN |
           TWAI_ALERT_ARB_LOST |
           TWAI_ALERT_RECOVERY_IN_PROGRESS |
           TWAI_ALERT_BUS_RECOVERED |
           TWAI_ALERT_PERIPH_RESET;
}

static uint8_t canbus_get_frame_filter_mode(void)
{
    return CANBUS_FRAME_FILTER_MODE;
}

static twai_mode_t canbus_get_runtime_mode(void)
{
    switch (CANBUS_RUNTIME_MODE) {
    case CANBUS_MODE_NO_ACK:
        return TWAI_MODE_NO_ACK;
    case CANBUS_MODE_LISTEN_ONLY:
        return TWAI_MODE_LISTEN_ONLY;
    case CANBUS_MODE_NORMAL:
    default:
        return TWAI_MODE_NORMAL;
    }
}

static const char *canbus_mode_to_str(twai_mode_t mode)
{
    switch (mode) {
    case TWAI_MODE_NORMAL:
        return "NORMAL";
    case TWAI_MODE_NO_ACK:
        return "NO_ACK";
    case TWAI_MODE_LISTEN_ONLY:
        return "LISTEN_ONLY";
    default:
        return "UNKNOWN";
    }
}

static const char *canbus_filter_mode_to_str(uint8_t filter_mode)
{
    switch (filter_mode) {
    case CANBUS_FILTER_ALL:
        return "ALL";
    case CANBUS_FILTER_EXT_ONLY:
        return "EXT_ONLY";
    case CANBUS_FILTER_STD_ONLY:
        return "STD_ONLY";
    default:
        return "UNKNOWN";
    }
}

static bool canbus_get_timing_cfg_for_bitrate(int bitrate_hz, twai_timing_config_t *out_timing)
{
    if (!out_timing) {
        return false;
    }

    switch (bitrate_hz) {
    case 250000:
        *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
        return true;
    case 500000:
        *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
        return true;
    case 125000:
        *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
        return true;
    case 100000:
        *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS();
        return true;
    case 1000000:
        *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
        return true;
    default:
        return false;
    }
}

static uint32_t canbus_j1939_extract_pgn(uint32_t identifier)
{
    uint32_t pgn = (identifier >> 8) & 0x3FFFFU;
    uint32_t pf = (identifier >> 16) & 0xFFU;
    if (pf < 240U) {
        pgn &= 0x3FF00U;
    }
    return pgn;
}

static const char *canbus_source_label_from_address(uint8_t source_address)
{
    switch (source_address) {
    case 0x33:
        return "Right";
    case 0x34:
        return "Left";
    case 0x35:
        return "Center";
    case 0x36:
        return "Auxiliary";
    default:
        return "Unknown";
    }
}

static const char *canbus_signal_status_to_str(ui_can_signal_status_t status)
{
    switch (status) {
    case UI_CAN_SIGNAL_STATUS_INACTIVE:
        return "inactive";
    case UI_CAN_SIGNAL_STATUS_ACTIVE:
        return "active";
    case UI_CAN_SIGNAL_STATUS_ERROR:
        return "error";
    case UI_CAN_SIGNAL_STATUS_NOT_AVAILABLE:
        return "n/a";
    default:
        return "unknown";
    }
}

static ui_can_signal_status_t canbus_decode_signal_status(uint8_t packed_byte, uint8_t shift)
{
    return (ui_can_signal_status_t)((packed_byte >> shift) & 0x3U);
}

static void canbus_decode_axis(ui_can_axis_decoded_t *out_axis, uint8_t status_byte, uint8_t position_byte)
{
    if (!out_axis) {
        return;
    }

    out_axis->neutral_status = canbus_decode_signal_status(status_byte, 0);
    out_axis->negative_status = canbus_decode_signal_status(status_byte, 2);
    out_axis->positive_status = canbus_decode_signal_status(status_byte, 4);
    out_axis->position = (uint16_t)(((uint16_t)position_byte << 2) | ((status_byte >> 6) & 0x3U));
}

static void canbus_decode_button_group(ui_can_signal_status_t *button_status, uint8_t packed_byte,
                                       uint8_t button_a, uint8_t button_b, uint8_t button_c, uint8_t button_d)
{
    if (!button_status) {
        return;
    }

    button_status[button_a] = canbus_decode_signal_status(packed_byte, 0);
    button_status[button_b] = canbus_decode_signal_status(packed_byte, 2);
    button_status[button_c] = canbus_decode_signal_status(packed_byte, 4);
    button_status[button_d] = canbus_decode_signal_status(packed_byte, 6);
}

#if CANBUS_DEBUG_LOG_DECODED
static bool canbus_should_emit_decoded_log(void)
{
    TickType_t now = xTaskGetTickCount();

    if (CANBUS_DEBUG_LOG_MIN_INTERVAL_MS > 0 &&
        s_last_decoded_log_tick != 0 &&
        (now - s_last_decoded_log_tick) < pdMS_TO_TICKS(CANBUS_DEBUG_LOG_MIN_INTERVAL_MS)) {
        s_suppressed_decoded_logs++;
        return false;
    }

    if (s_suppressed_decoded_logs > 0) {
        ESP_LOGI(TAG, "decoded log throttle suppressed %lu message(s)",
                 (unsigned long)s_suppressed_decoded_logs);
        s_suppressed_decoded_logs = 0;
    }

    s_last_decoded_log_tick = now;
    return true;
}

static void canbus_format_button_summary(char *out, size_t out_size,
                                         const ui_can_signal_status_t button_status[12])
{
    size_t used = 0;
    bool any_pressed = false;

    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';
    for (size_t i = 0; i < 12; i++) {
        if (button_status[i] != UI_CAN_SIGNAL_STATUS_ACTIVE) {
            continue;
        }

        int written = snprintf(out + used, out_size - used, "%s%u", any_pressed ? "," : "",
                               (unsigned)(i + 1U));
        if (written < 0 || (size_t)written >= (out_size - used)) {
            return;
        }

        used += (size_t)written;
        any_pressed = true;
    }

    if (!any_pressed) {
        snprintf(out, out_size, "none");
    }
}

static void canbus_log_decoded_basic(const ui_can_basic_decoded_t *basic)
{
    char button_text[48];

    if (!basic || !canbus_should_emit_decoded_log()) {
        return;
    }

    canbus_format_button_summary(button_text, sizeof(button_text), basic->button_status);
    ESP_LOGI(TAG,
             "DEC BASIC PGN=0x%05" PRIX32 " src=0x%02X(%s) "
             "baseX=%u[%s/%s/%s] baseY=%u[%s/%s/%s] buttons=%s",
             basic->pgn,
             basic->source_address,
             canbus_source_label_from_address(basic->source_address),
             basic->base_x.position,
             canbus_signal_status_to_str(basic->base_x.neutral_status),
             canbus_signal_status_to_str(basic->base_x.negative_status),
             canbus_signal_status_to_str(basic->base_x.positive_status),
             basic->base_y.position,
             canbus_signal_status_to_str(basic->base_y.neutral_status),
             canbus_signal_status_to_str(basic->base_y.negative_status),
             canbus_signal_status_to_str(basic->base_y.positive_status),
             button_text);
}

static void canbus_log_decoded_extended(const ui_can_extended_decoded_t *extended)
{
    if (!extended || !canbus_should_emit_decoded_log()) {
        return;
    }

    ESP_LOGI(TAG,
             "DEC EXT PGN=0x%05" PRIX32 " src=0x%02X(%s) "
             "gripX=%u[%s/%s/%s] gripY=%u[%s/%s/%s] theta=%u[%s/%s/%s]",
             extended->pgn,
             extended->source_address,
             canbus_source_label_from_address(extended->source_address),
             extended->grip_x.position,
             canbus_signal_status_to_str(extended->grip_x.neutral_status),
             canbus_signal_status_to_str(extended->grip_x.negative_status),
             canbus_signal_status_to_str(extended->grip_x.positive_status),
             extended->grip_y.position,
             canbus_signal_status_to_str(extended->grip_y.neutral_status),
             canbus_signal_status_to_str(extended->grip_y.negative_status),
             canbus_signal_status_to_str(extended->grip_y.positive_status),
             extended->theta.position,
             canbus_signal_status_to_str(extended->theta.neutral_status),
             canbus_signal_status_to_str(extended->theta.negative_status),
             canbus_signal_status_to_str(extended->theta.positive_status));
}
#endif

static void canbus_update_target_stale_flags(TickType_t now)
{
    const TickType_t stale_ticks = pdMS_TO_TICKS(CANBUS_SIGNAL_STALE_TIMEOUT_MS);

    if (s_sniffer_state.decoded_basic.valid && s_last_basic_target_tick != 0) {
        s_sniffer_state.decoded_basic.stale = (now - s_last_basic_target_tick) > stale_ticks;
    }

    if (s_sniffer_state.decoded_extended.valid && s_last_extended_target_tick != 0) {
        s_sniffer_state.decoded_extended.stale = (now - s_last_extended_target_tick) > stale_ticks;
    }

    if (s_last_target_tick == 0) {
        s_sniffer_state.target_signal_stale = false;
    } else {
        s_sniffer_state.target_signal_stale = (now - s_last_target_tick) > stale_ticks;
    }
}

static void canbus_reset_sniffer_state(void)
{
    memset(&s_sniffer_state, 0, sizeof(s_sniffer_state));
    s_sniffer_state.runtime_mode = (uint8_t)canbus_get_runtime_mode();
    s_sniffer_state.frame_filter_mode = canbus_get_frame_filter_mode();
    s_sniffer_state.self_test_enabled = CANBUS_ENABLE_SELF_TEST;
    s_sniffer_state.minimal_app_mode = CANBUS_MINIMAL_APP_MODE;
    s_sniffer_state.bitrate_hz = CANBUS_BITRATE_HZ;
    s_sniffer_state.tx_gpio = CANBUS_TWAI_TX_GPIO;
    s_sniffer_state.rx_gpio = CANBUS_TWAI_RX_GPIO;
    s_last_rx_log_tick = 0;
    s_suppressed_rx_logs = 0;
    s_last_decoded_log_tick = 0;
    s_suppressed_decoded_logs = 0;
    s_self_test_sequence = 0;
    s_recovery_in_progress = false;
    s_last_basic_target_tick = 0;
    s_last_extended_target_tick = 0;
    s_last_target_tick = 0;
    s_last_error_alert_log_tick = 0;
    s_sniffer_state.rx_edge_probe_done = false;
    s_sniffer_state.rx_edge_probe_count = 0;
    s_sniffer_state.rx_edge_probe_duration_ms = 0;
    s_sniffer_state.rx_edge_probe_initial_level = 0;
    s_sniffer_state.aux_rx_probe_gpio = CANBUS_AUX_RX_PROBE_GPIO;
    s_sniffer_state.aux_rx_probe_initial_level = 0;
    s_sniffer_state.aux_rx_probe_count = 0;
}

static bool canbus_should_log_error_alert(void)
{
    TickType_t now = xTaskGetTickCount();

    if (s_last_error_alert_log_tick != 0 &&
        (now - s_last_error_alert_log_tick) < pdMS_TO_TICKS(1000)) {
        return false;
    }

    s_last_error_alert_log_tick = now;
    return true;
}

static void canbus_refresh_status_from_driver(void)
{
    twai_status_info_t status = {0};
    if (twai_get_status_info(&status) != ESP_OK) {
        return;
    }

    s_sniffer_state.controller_state = (uint8_t)status.state;
    s_sniffer_state.tx_error_counter = status.tx_error_counter;
    s_sniffer_state.rx_error_counter = status.rx_error_counter;
    s_sniffer_state.bus_error_count = status.bus_error_count;
    s_sniffer_state.rx_missed_count = status.rx_missed_count;
    s_sniffer_state.rx_overrun_count = status.rx_overrun_count;
    s_sniffer_state.queued_rx_count = status.msgs_to_rx;
}

static void canbus_publish_sniffer_state(void)
{
    ui_set_can_sniffer_state(&s_sniffer_state);
}

static void canbus_log_heartbeat(void)
{
    ESP_LOGI(TAG,
             "heartbeat mode=%s state=%u rx=%lu ext=%lu std=%lu drop=%lu "
             "alerts=0x%08lX tec=%lu rec=%lu buserr=%lu miss=%lu over=%lu queued=%lu",
             canbus_mode_to_str(canbus_get_runtime_mode()),
             (unsigned int)s_sniffer_state.controller_state,
             (unsigned long)s_sniffer_state.total_rx_frames,
             (unsigned long)s_sniffer_state.total_extended_frames,
             (unsigned long)s_sniffer_state.total_standard_frames,
             (unsigned long)s_sniffer_state.total_filtered_frames,
             (unsigned long)s_sniffer_state.last_alerts,
             (unsigned long)s_sniffer_state.tx_error_counter,
             (unsigned long)s_sniffer_state.rx_error_counter,
             (unsigned long)s_sniffer_state.bus_error_count,
             (unsigned long)s_sniffer_state.rx_missed_count,
             (unsigned long)s_sniffer_state.rx_overrun_count,
             (unsigned long)s_sniffer_state.queued_rx_count);
}

static void canbus_maybe_start_recovery(void)
{
    if (s_sniffer_state.controller_state != (uint8_t)TWAI_STATE_BUS_OFF) {
        return;
    }

    if (s_recovery_in_progress) {
        return;
    }

    if (twai_initiate_recovery() == ESP_OK) {
        s_recovery_in_progress = true;
        ESP_LOGW(TAG, "TWAI bus-off recovery started");
    } else {
        ESP_LOGW(TAG, "TWAI bus-off recovery request failed");
    }
}

static void canbus_push_history_frame(const twai_message_t *msg)
{
    ui_can_frame_snapshot_t *frame = NULL;

    if (!msg) {
        return;
    }

    if (s_sniffer_state.history_count < UI_CAN_FRAME_HISTORY_LEN) {
        s_sniffer_state.history_count++;
    }

    memmove(&s_sniffer_state.history[1], &s_sniffer_state.history[0],
            (s_sniffer_state.history_count - 1U) * sizeof(s_sniffer_state.history[0]));

    frame = &s_sniffer_state.history[0];
    memset(frame, 0, sizeof(*frame));
    frame->valid = true;
    frame->id = msg->identifier;
    frame->dlc = msg->data_length_code;
    frame->extended = msg->extd;
    frame->rtr = msg->rtr;
    memcpy(frame->data, msg->data, (msg->data_length_code <= 8U) ? msg->data_length_code : 8U);
}

static bool canbus_is_self_test_frame(const twai_message_t *msg)
{
    if (!msg || msg->data_length_code < 3U) {
        return false;
    }

    if (msg->data[0] != 0x53 || msg->data[1] != 0x54) {
        return false;
    }

    if (!msg->extd && msg->identifier == CANBUS_SELF_TEST_STD_ID) {
        return true;
    }

    if (msg->extd && msg->identifier == CANBUS_SELF_TEST_EXT_ID) {
        return true;
    }

    return false;
}

static bool canbus_frame_passes_runtime_filter(const twai_message_t *msg)
{
    if (!msg) {
        return false;
    }

    switch (canbus_get_frame_filter_mode()) {
    case CANBUS_FILTER_EXT_ONLY:
        return msg->extd;
    case CANBUS_FILTER_STD_ONLY:
        return !msg->extd;
    case CANBUS_FILTER_ALL:
    default:
        return true;
    }
}

#if CANBUS_DEBUG_LOG_ALL_RX
static void canbus_log_rx_frame(const twai_message_t *msg)
{
    TickType_t now = xTaskGetTickCount();

    if (!msg) {
        return;
    }

    if (CANBUS_DEBUG_LOG_MIN_INTERVAL_MS > 0 &&
        s_last_rx_log_tick != 0 &&
        (now - s_last_rx_log_tick) < pdMS_TO_TICKS(CANBUS_DEBUG_LOG_MIN_INTERVAL_MS)) {
        s_suppressed_rx_logs++;
        return;
    }

    if (s_suppressed_rx_logs > 0) {
        ESP_LOGI(TAG, "RX log throttle suppressed %lu frame(s)", (unsigned long)s_suppressed_rx_logs);
        s_suppressed_rx_logs = 0;
    }

    ESP_LOGI(TAG,
             "RX %s ID=0x%0*" PRIX32 " RTR=%d DLC=%d DATA=[%02X %02X %02X %02X %02X %02X %02X %02X]",
             msg->extd ? "EXT" : "STD",
             msg->extd ? 8 : 3,
             msg->identifier,
             msg->rtr ? 1 : 0,
             msg->data_length_code,
             msg->data[0], msg->data[1], msg->data[2], msg->data[3],
             msg->data[4], msg->data[5], msg->data[6], msg->data[7]);
    s_last_rx_log_tick = now;
}
#endif

static void canbus_handle_rx_message(const twai_message_t *msg)
{
    if (!msg) {
        return;
    }

    s_sniffer_state.total_rx_frames++;
    s_sniffer_state.has_last_frame = true;
    s_sniffer_state.last_frame_id = msg->identifier;
    s_sniffer_state.last_frame_dlc = msg->data_length_code;
    s_sniffer_state.last_frame_extended = msg->extd;
    s_sniffer_state.last_frame_rtr = msg->rtr;
    memset(s_sniffer_state.last_frame_data, 0, sizeof(s_sniffer_state.last_frame_data));
    memcpy(s_sniffer_state.last_frame_data, msg->data,
           (msg->data_length_code <= 8U) ? msg->data_length_code : 8U);

    if (msg->extd) {
        s_sniffer_state.total_extended_frames++;
    } else {
        s_sniffer_state.total_standard_frames++;
    }

    if (canbus_is_self_test_frame(msg)) {
        s_sniffer_state.self_test_rx_count++;
    }

    if (msg->extd) {
        uint32_t pgn = canbus_j1939_extract_pgn(msg->identifier);

        if (pgn == CANBUS_BASIC_JOYSTICK_PGN || pgn == CANBUS_EXTENDED_JOYSTICK_PGN) {
            if (msg->data_length_code != CANBUS_TARGET_FRAME_DLC) {
                s_sniffer_state.invalid_target_frame_count++;
            } else if (pgn == CANBUS_BASIC_JOYSTICK_PGN) {
                ui_can_basic_decoded_t *basic = &s_sniffer_state.decoded_basic;
                TickType_t now = xTaskGetTickCount();

                memset(basic, 0, sizeof(*basic));
                basic->valid = true;
                basic->stale = false;
                basic->source_address = (uint8_t)(msg->identifier & 0xFFU);
                basic->pgn = pgn;
                basic->last_rx_ms = (uint32_t)now * (uint32_t)portTICK_PERIOD_MS;
                canbus_decode_axis(&basic->base_x, msg->data[0], msg->data[1]);
                canbus_decode_axis(&basic->base_y, msg->data[2], msg->data[3]);
                canbus_decode_button_group(basic->button_status, msg->data[5], 3, 2, 1, 0);
                canbus_decode_button_group(basic->button_status, msg->data[6], 7, 6, 5, 4);
                canbus_decode_button_group(basic->button_status, msg->data[7], 11, 10, 9, 8);
                s_sniffer_state.matched_basic_frames++;
                s_sniffer_state.last_target_rx_ms = basic->last_rx_ms;
                s_last_basic_target_tick = now;
                s_last_target_tick = now;
                s_sniffer_state.target_signal_stale = false;
#if CANBUS_DEBUG_LOG_DECODED
                canbus_log_decoded_basic(basic);
#endif
            } else {
                ui_can_extended_decoded_t *extended = &s_sniffer_state.decoded_extended;
                TickType_t now = xTaskGetTickCount();

                memset(extended, 0, sizeof(*extended));
                extended->valid = true;
                extended->stale = false;
                extended->source_address = (uint8_t)(msg->identifier & 0xFFU);
                extended->pgn = pgn;
                extended->last_rx_ms = (uint32_t)now * (uint32_t)portTICK_PERIOD_MS;
                canbus_decode_axis(&extended->grip_x, msg->data[0], msg->data[1]);
                canbus_decode_axis(&extended->grip_y, msg->data[2], msg->data[3]);
                canbus_decode_axis(&extended->theta, msg->data[4], msg->data[5]);
                s_sniffer_state.matched_extended_frames++;
                s_sniffer_state.last_target_rx_ms = extended->last_rx_ms;
                s_last_extended_target_tick = now;
                s_last_target_tick = now;
                s_sniffer_state.target_signal_stale = false;
#if CANBUS_DEBUG_LOG_DECODED
                canbus_log_decoded_extended(extended);
#endif
            }
        }
    }

    if (!canbus_frame_passes_runtime_filter(msg)) {
        s_sniffer_state.total_filtered_frames++;
#if CANBUS_DEBUG_LOG_ALL_RX
        canbus_log_rx_frame(msg);
#endif
        return;
    }

    canbus_push_history_frame(msg);

    if (msg->extd) {
        s_sniffer_state.has_last_extended_frame = true;
        s_sniffer_state.last_extended_id = msg->identifier;
        s_sniffer_state.last_extended_pgn = canbus_j1939_extract_pgn(msg->identifier);
        s_sniffer_state.last_extended_dlc = msg->data_length_code;
        s_sniffer_state.last_extended_rtr = msg->rtr;
        memset(s_sniffer_state.last_extended_data, 0, sizeof(s_sniffer_state.last_extended_data));
        memcpy(s_sniffer_state.last_extended_data, msg->data,
               (msg->data_length_code <= 8U) ? msg->data_length_code : 8U);
    } else {
        s_sniffer_state.has_last_standard_frame = true;
        s_sniffer_state.last_standard_id = msg->identifier;
        s_sniffer_state.last_standard_dlc = msg->data_length_code;
        s_sniffer_state.last_standard_rtr = msg->rtr;
        memset(s_sniffer_state.last_standard_data, 0, sizeof(s_sniffer_state.last_standard_data));
        memcpy(s_sniffer_state.last_standard_data, msg->data,
               (msg->data_length_code <= 8U) ? msg->data_length_code : 8U);
    }

#if CANBUS_DEBUG_LOG_ALL_RX
    canbus_log_rx_frame(msg);
#endif
}

static void canbus_handle_alerts(uint32_t alerts_triggered)
{
    if (alerts_triggered == 0) {
        return;
    }

    s_sniffer_state.last_alerts = alerts_triggered;

    if (alerts_triggered & TWAI_ALERT_RX_DATA) {
        s_sniffer_state.alert_rx_data_count++;
    }
    if (alerts_triggered & TWAI_ALERT_TX_SUCCESS) {
        s_sniffer_state.alert_tx_success_count++;
    }
    if (alerts_triggered & TWAI_ALERT_TX_FAILED) {
        s_sniffer_state.alert_tx_failed_count++;
        ESP_LOGW(TAG, "TWAI alert: TX failed");
    }
    if (alerts_triggered & TWAI_ALERT_TX_IDLE) {
        s_sniffer_state.alert_tx_idle_count++;
    }
    if (alerts_triggered & TWAI_ALERT_ERR_PASS) {
        s_sniffer_state.alert_err_pass_count++;
        if (canbus_should_log_error_alert()) {
            ESP_LOGW(TAG, "TWAI alert: error passive");
        }
    }
    if (alerts_triggered & TWAI_ALERT_BUS_ERROR) {
        s_sniffer_state.alert_bus_error_count++;
        if (canbus_should_log_error_alert()) {
            ESP_LOGW(TAG, "TWAI alert: bus error");
        }
    }
    if (alerts_triggered & TWAI_ALERT_RX_QUEUE_FULL) {
        s_sniffer_state.alert_rx_queue_full_count++;
        ESP_LOGW(TAG, "TWAI alert: RX queue full");
    }
    if (alerts_triggered & TWAI_ALERT_BUS_OFF) {
        s_sniffer_state.alert_bus_off_count++;
        ESP_LOGW(TAG, "TWAI alert: bus off");
    }
    if (alerts_triggered & TWAI_ALERT_RX_FIFO_OVERRUN) {
        s_sniffer_state.alert_rx_fifo_overrun_count++;
        ESP_LOGW(TAG, "TWAI alert: RX FIFO overrun");
    }
    if (alerts_triggered & TWAI_ALERT_ABOVE_ERR_WARN) {
        s_sniffer_state.alert_above_err_warn_count++;
        if (canbus_should_log_error_alert()) {
            ESP_LOGW(TAG, "TWAI alert: above error warning");
        }
    }
    if (alerts_triggered & TWAI_ALERT_BELOW_ERR_WARN) {
        s_sniffer_state.alert_below_err_warn_count++;
        ESP_LOGI(TAG, "TWAI alert: below error warning");
    }
    if (alerts_triggered & TWAI_ALERT_ARB_LOST) {
        s_sniffer_state.alert_arb_lost_count++;
        ESP_LOGW(TAG, "TWAI alert: arbitration lost");
    }
    if (alerts_triggered & TWAI_ALERT_RECOVERY_IN_PROGRESS) {
        s_sniffer_state.alert_recovery_in_progress_count++;
        s_recovery_in_progress = true;
        ESP_LOGW(TAG, "TWAI alert: recovery in progress");
    }
    if (alerts_triggered & TWAI_ALERT_BUS_RECOVERED) {
        s_sniffer_state.alert_bus_recovered_count++;
        s_recovery_in_progress = false;
        ESP_LOGI(TAG, "TWAI alert: bus recovered");
    }
    if (alerts_triggered & TWAI_ALERT_PERIPH_RESET) {
        s_sniffer_state.alert_periph_reset_count++;
        ESP_LOGW(TAG, "TWAI alert: peripheral reset");
    }
}

static void canbus_log_runtime_config(void)
{
    ESP_LOGI(TAG,
             "CAN config: bitrate=%d mode=%s filter=%s tx_gpio=%d rx_gpio=%d self_test=%d minimal_app=%d",
             CANBUS_BITRATE_HZ,
             canbus_mode_to_str(canbus_get_runtime_mode()),
             canbus_filter_mode_to_str(canbus_get_frame_filter_mode()),
             CANBUS_TWAI_TX_GPIO,
             CANBUS_TWAI_RX_GPIO,
             CANBUS_ENABLE_SELF_TEST ? 1 : 0,
             CANBUS_MINIMAL_APP_MODE ? 1 : 0);

    if (CANBUS_ENABLE_SELF_TEST && canbus_get_runtime_mode() != TWAI_MODE_NO_ACK) {
        ESP_LOGW(TAG, "Self-test is enabled but runtime mode is not NO_ACK");
    }
}

static void canbus_maybe_send_self_test_frames(TickType_t now)
{
    static TickType_t last_self_test_tick;

    if (!CANBUS_ENABLE_SELF_TEST) {
        return;
    }

    if (canbus_get_runtime_mode() == TWAI_MODE_LISTEN_ONLY) {
        return;
    }

    if ((now - last_self_test_tick) < pdMS_TO_TICKS(CANBUS_SELF_TEST_PERIOD_MS)) {
        return;
    }

    last_self_test_tick = now;

    twai_message_t std_msg = {
        .extd = 0,
        .rtr = 0,
        .ss = 1,
        .self = 1,
        .identifier = CANBUS_SELF_TEST_STD_ID,
        .data_length_code = 8,
        .data = {0x53, 0x54, s_self_test_sequence, 0x01, 0x23, 0x45, 0x67, 0x89},
    };
    twai_message_t ext_msg = {
        .extd = 1,
        .rtr = 0,
        .ss = 1,
        .self = 1,
        .identifier = CANBUS_SELF_TEST_EXT_ID,
        .data_length_code = 8,
        .data = {0x53, 0x54, s_self_test_sequence, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5},
    };

    if (twai_transmit(&std_msg, 0) == ESP_OK) {
        s_sniffer_state.self_test_tx_count++;
    } else {
        ESP_LOGW(TAG, "self-test STD frame queue failed");
    }

    if (twai_transmit(&ext_msg, 0) == ESP_OK) {
        s_sniffer_state.self_test_tx_count++;
    } else {
        ESP_LOGW(TAG, "self-test EXT frame queue failed");
    }

    s_self_test_sequence++;
}

static void canbus_maybe_send_active_ack_probe(TickType_t now)
{
    static TickType_t last_probe_tick;

    if (!CANBUS_ENABLE_ACTIVE_ACK_PROBE) {
        return;
    }

    if (canbus_get_runtime_mode() != TWAI_MODE_NORMAL) {
        return;
    }

    if ((now - last_probe_tick) < pdMS_TO_TICKS(CANBUS_ACTIVE_ACK_PROBE_PERIOD_MS)) {
        return;
    }

    last_probe_tick = now;

    twai_message_t probe_msg = {
        .extd = 1,
        .rtr = 0,
        .identifier = CANBUS_ACTIVE_ACK_PROBE_ID,
        .data_length_code = 1,
        .data = {0xA5},
    };

    if (twai_transmit(&probe_msg, 0) == ESP_OK) {
        ESP_LOGI(TAG, "ACK probe queued: EXT ID=0x%08" PRIX32 " DLC=1", probe_msg.identifier);
    } else {
        ESP_LOGW(TAG, "ACK probe queue failed");
    }
}

static void canbus_maybe_send_j1939_request_probe(TickType_t now)
{
#if CANBUS_ENABLE_J1939_REQUEST_PROBE
    static TickType_t last_probe_tick;
    static uint8_t probe_step;

    if (canbus_get_runtime_mode() != TWAI_MODE_NORMAL) {
        return;
    }

    if ((now - last_probe_tick) < pdMS_TO_TICKS(CANBUS_J1939_REQUEST_PROBE_PERIOD_MS)) {
        return;
    }

    last_probe_tick = now;

    twai_message_t msg = {
        .extd = 1,
        .rtr = 0,
        .identifier = CANBUS_J1939_REQUEST_ID,
        .data_length_code = 3,
        .data = {0},
    };
    const char *label = "RQST";

    switch (probe_step % 5U) {
    case 0:
        msg.data[0] = 0x00; // PGN 0x00EE00 Address Claim
        msg.data[1] = 0xEE;
        msg.data[2] = 0x00;
        label = "RQST AddressClaim 0xEE00";
        break;
    case 1:
        msg.data[0] = 0xCA; // PGN 0x00FECA DM1
        msg.data[1] = 0xFE;
        msg.data[2] = 0x00;
        label = "RQST DM1 0xFECA";
        break;
    case 2:
        msg.data[0] = 0xFF; // PGN 0x00FEFF Software ID
        msg.data[1] = 0xFE;
        msg.data[2] = 0x00;
        label = "RQST SOFT 0xFEFF";
        break;
    case 3:
        msg.data[0] = 0xDA; // PGN 0x00FEDA alternate software ID
        msg.data[1] = 0xFE;
        msg.data[2] = 0x00;
        label = "RQST SOFT 0xFEDA";
        break;
    case 4:
    default:
        msg.identifier = CANBUS_J1939_GLOBAL_DM13_ID;
        msg.data_length_code = 8;
        msg.data[0] = 0xFD; // current/j1587/j1922 don't care, J1939 network #1 start broadcast
        msg.data[1] = 0xFF; // other network controls: don't care
        msg.data[2] = 0xFF; // other network controls: don't care
        msg.data[3] = 0x0F; // hold all devices, suspend signal no action
        msg.data[4] = 0x00;
        msg.data[5] = 0x00;
        msg.data[6] = 0xFF;
        msg.data[7] = 0xFF;
        label = "DM13 StartBroadcast";
        break;
    }

    probe_step++;
    s_sniffer_state.j1939_request_probe_last_step = (uint8_t)((probe_step - 1U) % 5U);

    if (twai_transmit(&msg, 0) == ESP_OK) {
        s_sniffer_state.j1939_request_probe_tx_count++;
        ESP_LOGI(TAG,
                 "J1939 probe queued: %s ID=0x%08" PRIX32 " DATA=[%02X %02X %02X %02X %02X %02X %02X %02X]",
                 label,
                 msg.identifier,
                 msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                 msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
    } else {
        s_sniffer_state.j1939_request_probe_fail_count++;
        ESP_LOGW(TAG, "J1939 probe queue failed: %s", label);
    }
#else
    (void)now;
#endif
}

#if CANBUS_ENABLE_RX_EDGE_PROBE
static void IRAM_ATTR canbus_rx_edge_probe_isr(void *arg)
{
    (void)arg;
    s_rx_edge_probe_count++;
}

#if CANBUS_ENABLE_AUX_RX_EDGE_PROBE
static void IRAM_ATTR canbus_aux_rx_edge_probe_isr(void *arg)
{
    (void)arg;
    s_aux_rx_edge_probe_count++;
}
#endif

static void canbus_run_rx_edge_probe(void)
{
    const gpio_num_t probe_gpio = (gpio_num_t)CANBUS_TWAI_RX_GPIO;
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << probe_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    s_rx_edge_probe_count = 0;
#if CANBUS_ENABLE_AUX_RX_EDGE_PROBE
    s_aux_rx_edge_probe_count = 0;
#endif
    s_sniffer_state.rx_edge_probe_done = false;
    s_sniffer_state.rx_edge_probe_count = 0;
    s_sniffer_state.rx_edge_probe_duration_ms = CANBUS_RX_EDGE_PROBE_DURATION_MS;

    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGW(TAG, "RX edge probe: gpio_config failed on GPIO%d", CANBUS_TWAI_RX_GPIO);
        return;
    }

    gpio_install_isr_service(0);
    if (gpio_isr_handler_add(probe_gpio, canbus_rx_edge_probe_isr, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "RX edge probe: failed to attach ISR on GPIO%d", CANBUS_TWAI_RX_GPIO);
        gpio_reset_pin(probe_gpio);
        return;
    }

#if CANBUS_ENABLE_AUX_RX_EDGE_PROBE
    {
        const gpio_num_t aux_probe_gpio = (gpio_num_t)CANBUS_AUX_RX_PROBE_GPIO;
        gpio_config_t aux_conf = {
            .pin_bit_mask = 1ULL << aux_probe_gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };

        if (gpio_config(&aux_conf) == ESP_OK &&
            gpio_isr_handler_add(aux_probe_gpio, canbus_aux_rx_edge_probe_isr, NULL) == ESP_OK) {
            s_sniffer_state.aux_rx_probe_initial_level = (uint8_t)gpio_get_level(aux_probe_gpio);
            ESP_LOGI(TAG,
                     "AUX RX probe: sampling GPIO%d for %d ms (initial level=%d)",
                     CANBUS_AUX_RX_PROBE_GPIO,
                     CANBUS_RX_EDGE_PROBE_DURATION_MS,
                     gpio_get_level(aux_probe_gpio));
        } else {
            ESP_LOGW(TAG, "AUX RX probe unavailable on GPIO%d", CANBUS_AUX_RX_PROBE_GPIO);
        }
    }
#endif

    ESP_LOGI(TAG,
             "RX edge probe: sampling GPIO%d for %d ms before starting TWAI (initial level=%d)",
             CANBUS_TWAI_RX_GPIO,
             CANBUS_RX_EDGE_PROBE_DURATION_MS,
             gpio_get_level(probe_gpio));
    s_sniffer_state.rx_edge_probe_initial_level = (uint8_t)gpio_get_level(probe_gpio);

    {
        const int sample_period_ms = 1000;
        const int sample_count = CANBUS_RX_EDGE_PROBE_DURATION_MS / sample_period_ms;
        for (int i = 0; i < sample_count; i++) {
            vTaskDelay(pdMS_TO_TICKS(sample_period_ms));
            ESP_LOGI(TAG,
                     "RX edge probe progress: %d/%d s edges=%lu level=%d"
#if CANBUS_ENABLE_AUX_RX_EDGE_PROBE
                     " | GPIO%d edges=%lu level=%d"
#endif
                     ,
                     i + 1,
                     sample_count,
                     (unsigned long)s_rx_edge_probe_count,
                     gpio_get_level(probe_gpio)
#if CANBUS_ENABLE_AUX_RX_EDGE_PROBE
                     ,
                     CANBUS_AUX_RX_PROBE_GPIO,
                     (unsigned long)s_aux_rx_edge_probe_count,
                     gpio_get_level((gpio_num_t)CANBUS_AUX_RX_PROBE_GPIO)
#endif
                     );
        }
    }

    gpio_isr_handler_remove(probe_gpio);
    ESP_LOGI(TAG,
             "RX edge probe result: edges=%lu on GPIO%d over %d ms",
             (unsigned long)s_rx_edge_probe_count,
             CANBUS_TWAI_RX_GPIO,
             CANBUS_RX_EDGE_PROBE_DURATION_MS);
    s_sniffer_state.rx_edge_probe_done = true;
    s_sniffer_state.rx_edge_probe_count = s_rx_edge_probe_count;

#if CANBUS_ENABLE_AUX_RX_EDGE_PROBE
    gpio_isr_handler_remove((gpio_num_t)CANBUS_AUX_RX_PROBE_GPIO);
    s_sniffer_state.aux_rx_probe_count = s_aux_rx_edge_probe_count;
    ESP_LOGI(TAG,
             "AUX RX probe result: edges=%lu on GPIO%d over %d ms",
             (unsigned long)s_aux_rx_edge_probe_count,
             CANBUS_AUX_RX_PROBE_GPIO,
             CANBUS_RX_EDGE_PROBE_DURATION_MS);
#endif

    if (s_rx_edge_probe_count == 0) {
        ESP_LOGW(TAG,
                 "RX edge probe saw no transitions. This usually means the transceiver RXD stayed idle: "
                 "no bus activity, no sensor transmission, or no valid differential signal reaching the board.");
    }

    gpio_reset_pin(probe_gpio);
}
#endif

static esp_err_t canbus_start_driver(void)
{
    const twai_mode_t mode = canbus_get_runtime_mode();
    const uint32_t alerts_to_enable = canbus_get_alert_mask();
    twai_timing_config_t t_config = {0};
    ESP_RETURN_ON_FALSE(canbus_get_timing_cfg_for_bitrate(CANBUS_BITRATE_HZ, &t_config), ESP_ERR_INVALID_ARG,
                        TAG, "Unsupported bitrate=%d", CANBUS_BITRATE_HZ);

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        CANBUS_TWAI_TX_GPIO, CANBUS_TWAI_RX_GPIO, mode);
    const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_RETURN_ON_ERROR(twai_driver_install(&g_config, &t_config, &f_config), TAG,
                        "twai_driver_install failed");
    ESP_RETURN_ON_ERROR(twai_start(), TAG, "twai_start failed");
    ESP_RETURN_ON_ERROR(twai_reconfigure_alerts(alerts_to_enable, NULL), TAG,
                        "twai_reconfigure_alerts failed");

    canbus_log_runtime_config();
    ESP_LOGI(TAG, "CAN sniffer started: bitrate=%d mode=%s tx_gpio=%d rx_gpio=%d",
             CANBUS_BITRATE_HZ, canbus_mode_to_str(mode), CANBUS_TWAI_TX_GPIO, CANBUS_TWAI_RX_GPIO);
    return ESP_OK;
}

static void canbus_rx_task(void *arg)
{
    (void)arg;
    TickType_t last_ui_publish_tick = xTaskGetTickCount();
    TickType_t last_status_refresh_tick = xTaskGetTickCount();
    TickType_t last_heartbeat_tick = xTaskGetTickCount();

    canbus_reset_sniffer_state();
    canbus_refresh_status_from_driver();
    canbus_publish_sniffer_state();

    while (1) {
        uint32_t alerts_triggered = 0;
        bool should_publish = false;
        esp_err_t err = twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(CANBUS_RX_POLL_TIMEOUT_MS));

        if (err == ESP_OK) {
            canbus_handle_alerts(alerts_triggered);
            should_publish = true;

            if (alerts_triggered & TWAI_ALERT_RX_DATA) {
                twai_message_t msg = {0};
                while (twai_receive(&msg, 0) == ESP_OK) {
                    canbus_handle_rx_message(&msg);
                }
            }
        } else if (err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "twai_read_alerts failed (err=0x%x)", (unsigned int)err);
        }

        TickType_t now = xTaskGetTickCount();
        canbus_maybe_send_self_test_frames(now);
        canbus_maybe_send_active_ack_probe(now);
        canbus_maybe_send_j1939_request_probe(now);
        canbus_update_target_stale_flags(now);

        if ((now - last_status_refresh_tick) >= pdMS_TO_TICKS(CANBUS_STATUS_REFRESH_PERIOD_MS)) {
            canbus_refresh_status_from_driver();
            canbus_maybe_start_recovery();
            last_status_refresh_tick = now;
            should_publish = true;
        }

        if ((now - last_heartbeat_tick) >= pdMS_TO_TICKS(CANBUS_HEARTBEAT_PERIOD_MS)) {
            canbus_log_heartbeat();
            last_heartbeat_tick = now;
        }

        if (should_publish || (now - last_ui_publish_tick) >= pdMS_TO_TICKS(CANBUS_UI_REFRESH_PERIOD_MS)) {
            canbus_publish_sniffer_state();
            last_ui_publish_tick = now;
        }
    }
}

esp_err_t canbus_start(void)
{
    canbus_reset_sniffer_state();
    ui_clear_can_sniffer_state();
#if CANBUS_ENABLE_RX_EDGE_PROBE
    canbus_run_rx_edge_probe();
#endif
    ESP_RETURN_ON_ERROR(canbus_start_driver(), TAG, "TWAI start failed");

    BaseType_t ok = xTaskCreate(canbus_rx_task, "canbus_rx", 4096, NULL, 8, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_FAIL, TAG, "failed to start rx task");
    return ESP_OK;
}
