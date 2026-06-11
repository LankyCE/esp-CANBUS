#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

#include "board/board.h"
#include "board/board_config.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#ifndef CONFIG_CANBUS_LVGL_BUF_LINES
#define CONFIG_CANBUS_LVGL_BUF_LINES 100
#endif

#define UI_GRAPH_WINDOW_SECONDS        30U
#define UI_GRAPH_SAMPLE_INTERVAL_MS    200U
#define UI_GRAPH_POINT_COUNT           ((UI_GRAPH_WINDOW_SECONDS * 1000U) / UI_GRAPH_SAMPLE_INTERVAL_MS)
#define UI_GRAPH_AXIS_MAGNITUDE_MAX    1023
#define UI_GRAPH_VALUE_MIN             (-UI_GRAPH_AXIS_MAGNITUDE_MAX)
#define UI_GRAPH_VALUE_MAX             (UI_GRAPH_AXIS_MAGNITUDE_MAX)
#define UI_BASIC_MESSAGE_BASE_ID       0x0CFDD600UL
#define UI_EXT_MESSAGE_BASE_ID         0x0CFDD700UL

typedef enum {
    UI_GRAPH_STATE_INACTIVE = 0,
    UI_GRAPH_STATE_NEUTRAL,
    UI_GRAPH_STATE_FORWARD,
    UI_GRAPH_STATE_REVERSE,
} ui_graph_state_t;

typedef enum {
    UI_PAGE_DIAGNOSTICS = 0,
    UI_PAGE_GRAPH = 1,
} ui_page_t;

static const char *TAG = "canbus_ui";

static lv_obj_t *s_diag_page;
static lv_obj_t *s_graph_page;
static lv_obj_t *s_diag_tab_btn;
static lv_obj_t *s_graph_tab_btn;

static lv_obj_t *s_mode_label;
static lv_obj_t *s_counts_label;
static lv_obj_t *s_health_label;
static lv_obj_t *s_alerts_label;
static lv_obj_t *s_diag_label;
static lv_obj_t *s_target_label;
static lv_obj_t *s_last_frame_label;
static lv_obj_t *s_last_frame_data_label;
static lv_obj_t *s_last_std_label;
static lv_obj_t *s_last_std_data_label;
static lv_obj_t *s_last_ext_label;
static lv_obj_t *s_last_ext_data_label;
static lv_obj_t *s_decoded_basic_label;
static lv_obj_t *s_decoded_basic_data_label;
static lv_obj_t *s_decoded_ext_label;
static lv_obj_t *s_decoded_ext_data_label;
static lv_obj_t *s_history_label;

static lv_obj_t *s_graph_title_label;
static lv_obj_t *s_graph_mode_label;
static lv_obj_t *s_graph_value_label;
static lv_obj_t *s_graph_status_label;
static lv_obj_t *s_graph_chart;
static lv_obj_t *s_graph_forward_hint_label;
static lv_obj_t *s_graph_neutral_hint_label;
static lv_obj_t *s_graph_reverse_hint_label;
static lv_chart_series_t *s_graph_series;

static bool s_ui_ready;
static ui_page_t s_selected_page = UI_PAGE_DIAGNOSTICS;
static ui_can_sniffer_state_t s_state;
static uint32_t s_graph_last_sample_ms;

static char s_mode_text[320];
static char s_counts_text[256];
static char s_health_text[320];
static char s_alerts_text[320];
static char s_diag_text[256];
static char s_target_text[256];
static char s_last_frame_text[160];
static char s_last_frame_data_text[96];
static char s_last_std_text[160];
static char s_last_std_data_text[96];
static char s_last_ext_text[160];
static char s_last_ext_data_text[96];
static char s_basic_text[224];
static char s_basic_data_text[256];
static char s_ext_text[224];
static char s_ext_data_text[256];
static char s_history_text[640];
static char s_button_summary[48];
static char s_graph_mode_text[32];
static char s_graph_value_text[64];
static char s_graph_status_text[128];

static const char *ui_twai_mode_to_str(uint8_t mode)
{
    switch ((twai_mode_t)mode) {
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

static const char *ui_frame_filter_to_str(uint8_t filter_mode)
{
    switch (filter_mode) {
    case 0:
        return "ALL";
    case 1:
        return "EXT_ONLY";
    case 2:
        return "STD_ONLY";
    default:
        return "UNKNOWN";
    }
}

static const char *ui_twai_state_to_str(uint8_t state)
{
    switch ((twai_state_t)state) {
    case TWAI_STATE_STOPPED:
        return "STOPPED";
    case TWAI_STATE_RUNNING:
        return "RUNNING";
    case TWAI_STATE_BUS_OFF:
        return "BUS_OFF";
    case TWAI_STATE_RECOVERING:
        return "RECOVERING";
    default:
        return "UNKNOWN";
    }
}

static const char *ui_signal_status_to_str(ui_can_signal_status_t status)
{
    switch (status) {
    case UI_CAN_SIGNAL_STATUS_INACTIVE:
        return "off";
    case UI_CAN_SIGNAL_STATUS_ACTIVE:
        return "on";
    case UI_CAN_SIGNAL_STATUS_ERROR:
        return "err";
    case UI_CAN_SIGNAL_STATUS_NOT_AVAILABLE:
        return "n/a";
    default:
        return "?";
    }
}

static const char *ui_source_label(uint8_t source_address)
{
    switch (source_address) {
    case 0x33:
        return "Right";
    case 0x34:
        return "Left";
    case 0x35:
        return "Center";
    case 0x36:
        return "Aux";
    default:
        return "Unknown";
    }
}

static const char *ui_j1939_probe_step_to_str(uint8_t step)
{
    switch (step) {
    case 0:
        return "AddrClaim";
    case 1:
        return "DM1";
    case 2:
        return "SOFT FEFF";
    case 3:
        return "SOFT FEDA";
    case 4:
        return "DM13 Start";
    default:
        return "--";
    }
}

static ui_graph_state_t ui_get_graph_state(void)
{
    if (!s_state.decoded_basic.valid || s_state.decoded_basic.stale) {
        return UI_GRAPH_STATE_INACTIVE;
    }

    if (s_state.decoded_basic.base_y.positive_status == UI_CAN_SIGNAL_STATUS_ACTIVE) {
        return UI_GRAPH_STATE_FORWARD;
    }

    if (s_state.decoded_basic.base_y.negative_status == UI_CAN_SIGNAL_STATUS_ACTIVE) {
        return UI_GRAPH_STATE_REVERSE;
    }

    if (s_state.decoded_basic.base_y.neutral_status == UI_CAN_SIGNAL_STATUS_ACTIVE) {
        return UI_GRAPH_STATE_NEUTRAL;
    }

    return UI_GRAPH_STATE_NEUTRAL;
}

static lv_color_t ui_get_graph_state_color(ui_graph_state_t state)
{
    switch (state) {
    case UI_GRAPH_STATE_FORWARD:
        return lv_color_hex(0x2E8B57);
    case UI_GRAPH_STATE_REVERSE:
        return lv_color_hex(0xC0392B);
    case UI_GRAPH_STATE_NEUTRAL:
        return lv_color_hex(0x1F5AA6);
    case UI_GRAPH_STATE_INACTIVE:
    default:
        return lv_color_hex(0x5A5A5A);
    }
}

static const char *ui_get_graph_state_text(ui_graph_state_t state)
{
    switch (state) {
    case UI_GRAPH_STATE_FORWARD:
        return "FORWARD";
    case UI_GRAPH_STATE_REVERSE:
        return "REVERSE";
    case UI_GRAPH_STATE_NEUTRAL:
        return "NEUTRAL";
    case UI_GRAPH_STATE_INACTIVE:
    default:
        return "--";
    }
}

static uint32_t ui_get_ms_since(uint32_t timestamp_ms)
{
    uint32_t now_ms = lv_tick_get();

    if (timestamp_ms == 0 || now_ms < timestamp_ms) {
        return 0;
    }

    return now_ms - timestamp_ms;
}

static const char *ui_get_can_health_text(void)
{
    if (s_state.controller_state == (uint8_t)TWAI_STATE_BUS_OFF) {
        return "BUS OFF";
    }

    if (s_state.alert_err_pass_count > 0 || s_state.bus_error_count > 0 || s_state.rx_overrun_count > 0) {
        if (s_state.total_rx_frames == 0) {
            return "ERROR / NO RX";
        }
        if (s_state.target_signal_stale) {
            return "BUS ACTIVE / TARGET STALE";
        }
        return "BUS ACTIVE / ERRORS PRESENT";
    }

    if (s_state.total_rx_frames == 0) {
        if (s_state.rx_edge_probe_done && s_state.rx_edge_probe_count == 0) {
            return "NO BUS ACTIVITY";
        }
        return "WAITING FOR FRAMES";
    }

    if (s_state.matched_basic_frames == 0 && s_state.matched_extended_frames == 0) {
        return "BUS ACTIVE / NO TARGET PGN";
    }

    if (s_state.target_signal_stale) {
        return "BUS ACTIVE / TARGET STALE";
    }

    return "BUS ACTIVE / TARGET OK";
}

static int32_t ui_get_base_y_signed_magnitude(void)
{
    if (!s_state.decoded_basic.valid || s_state.decoded_basic.stale) {
        return 0;
    }

    if (s_state.decoded_basic.base_y.positive_status == UI_CAN_SIGNAL_STATUS_ACTIVE) {
        return (int32_t)s_state.decoded_basic.base_y.position;
    }

    if (s_state.decoded_basic.base_y.negative_status == UI_CAN_SIGNAL_STATUS_ACTIVE) {
        return -(int32_t)s_state.decoded_basic.base_y.position;
    }

    return 0;
}

static void ui_format_frame_data(char *out, size_t out_size, const uint8_t *data, uint8_t dlc)
{
    size_t used = 0;

    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';
    for (uint8_t i = 0; i < dlc && i < 8; i++) {
        int written = snprintf(out + used, out_size - used, "%s%02X", (i == 0) ? "" : " ", data[i]);
        if (written < 0 || (size_t)written >= (out_size - used)) {
            return;
        }
        used += (size_t)written;
    }

    if (dlc == 0) {
        snprintf(out, out_size, "--");
    }
}

static void ui_format_history_line(char *out, size_t out_size, const ui_can_frame_snapshot_t *frame)
{
    char data_text[32];

    if (!out || out_size == 0) {
        return;
    }

    if (!frame || !frame->valid) {
        snprintf(out, out_size, "--");
        return;
    }

    ui_format_frame_data(data_text, sizeof(data_text), frame->data, frame->dlc);
    snprintf(out, out_size, "%s 0x%08lX dlc=%u %s [%s]",
             frame->extended ? "EXT" : "STD",
             (unsigned long)frame->id,
             (unsigned)frame->dlc,
             frame->rtr ? "RTR" : "DATA",
             data_text);
}

static void ui_format_button_summary(char *out, size_t out_size, const ui_can_signal_status_t *button_status)
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

static void ui_apply_tab_style(lv_obj_t *btn, bool selected)
{
    lv_obj_set_style_bg_color(btn, selected ? lv_color_hex(0x4F6D4A) : lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_text_color(btn, selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x707070), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 10, 0);
}

static void ui_select_page(ui_page_t page)
{
    s_selected_page = page;

    if (page == UI_PAGE_DIAGNOSTICS) {
        lv_obj_clear_flag(s_diag_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_graph_page, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_diag_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_graph_page, LV_OBJ_FLAG_HIDDEN);
    }

    ui_apply_tab_style(s_diag_tab_btn, page == UI_PAGE_DIAGNOSTICS);
    ui_apply_tab_style(s_graph_tab_btn, page == UI_PAGE_GRAPH);
}

static void ui_tab_event_cb(lv_event_t *e)
{
    ui_select_page((ui_page_t)(uintptr_t)lv_event_get_user_data(e));
}

static void ui_seed_graph_series(void)
{
    if (!s_graph_chart || !s_graph_series) {
        return;
    }

    for (uint32_t i = 0; i < UI_GRAPH_POINT_COUNT; i++) {
        lv_chart_set_next_value(s_graph_chart, s_graph_series, LV_CHART_POINT_NONE);
    }
    lv_chart_refresh(s_graph_chart);
}

static lv_coord_t ui_get_base_y_graph_value(void)
{
    if (!s_state.decoded_basic.valid || s_state.decoded_basic.stale) {
        return LV_CHART_POINT_NONE;
    }

    return (lv_coord_t)ui_get_base_y_signed_magnitude();
}

static void ui_update_graph_series(void)
{
    uint32_t now_ms = lv_tick_get();

    if (!s_graph_chart || !s_graph_series) {
        return;
    }

    if (s_graph_last_sample_ms == 0) {
        s_graph_last_sample_ms = now_ms;
        lv_chart_set_next_value(s_graph_chart, s_graph_series, ui_get_base_y_graph_value());
        lv_chart_refresh(s_graph_chart);
        return;
    }

    while ((now_ms - s_graph_last_sample_ms) >= UI_GRAPH_SAMPLE_INTERVAL_MS) {
        s_graph_last_sample_ms += UI_GRAPH_SAMPLE_INTERVAL_MS;
        lv_chart_set_next_value(s_graph_chart, s_graph_series, ui_get_base_y_graph_value());
    }

    lv_chart_refresh(s_graph_chart);
}

static void ui_refresh_graph_labels(void)
{
    ui_graph_state_t graph_state;
    lv_color_t graph_color;

    if (!s_graph_title_label || !s_graph_mode_label || !s_graph_value_label ||
        !s_graph_status_label || !s_graph_chart) {
        return;
    }

    graph_state = ui_get_graph_state();
    graph_color = ui_get_graph_state_color(graph_state);

    if (s_state.decoded_basic.valid) {
        snprintf(s_graph_mode_text, sizeof(s_graph_mode_text), "%s",
                 ui_get_graph_state_text(graph_state));
        snprintf(s_graph_value_text, sizeof(s_graph_value_text), "%ld",
                 (long)ui_get_base_y_signed_magnitude());
        snprintf(s_graph_status_text, sizeof(s_graph_status_text),
                 "Interpreting CAN 0x%08lX  PGN 0x%05lX  %s(0x%02X)",
                 (unsigned long)(UI_BASIC_MESSAGE_BASE_ID | s_state.decoded_basic.source_address),
                 (unsigned long)s_state.decoded_basic.pgn,
                 ui_source_label(s_state.decoded_basic.source_address),
                 (unsigned)s_state.decoded_basic.source_address);
    } else {
        snprintf(s_graph_mode_text, sizeof(s_graph_mode_text), "--");
        snprintf(s_graph_value_text, sizeof(s_graph_value_text), "--");
        snprintf(s_graph_status_text, sizeof(s_graph_status_text),
                 "Waiting for interpreted CAN message 0x0CFDD6xx");
    }

    lv_label_set_text(s_graph_title_label, "Base Y Position");
    lv_label_set_text(s_graph_mode_label, s_graph_mode_text);
    lv_label_set_text(s_graph_value_label, s_graph_value_text);
    lv_label_set_text(s_graph_status_label, s_graph_status_text);
    lv_obj_set_style_text_color(s_graph_mode_label, graph_color, 0);
    lv_obj_set_style_text_color(s_graph_value_label, graph_color, 0);
    lv_obj_set_style_text_color(s_graph_status_label, graph_color, 0);
    lv_obj_set_style_line_color(s_graph_chart, graph_color, LV_PART_ITEMS);
}

static void ui_refresh_diag_labels(void)
{
    uint32_t last_target_age_ms = ui_get_ms_since(s_state.last_target_rx_ms);

    if (!s_mode_label || !s_counts_label || !s_health_label || !s_alerts_label || !s_diag_label ||
        !s_target_label || !s_last_frame_label || !s_last_frame_data_label ||
        !s_last_std_label || !s_last_std_data_label || !s_last_ext_label || !s_last_ext_data_label ||
        !s_decoded_basic_label || !s_decoded_basic_data_label || !s_decoded_ext_label ||
        !s_decoded_ext_data_label || !s_history_label) {
        return;
    }

    snprintf(s_mode_text, sizeof(s_mode_text),
             "Health: %s  TWAI: %s  Mode: %s  Filter: %s\n"
             "Bitrate: %d kbps  Pins: TX%d RX%d  SelfTest: %s  UI: %s",
             ui_get_can_health_text(),
             ui_twai_state_to_str(s_state.controller_state),
             ui_twai_mode_to_str(s_state.runtime_mode),
             ui_frame_filter_to_str(s_state.frame_filter_mode),
             s_state.bitrate_hz / 1000,
             s_state.tx_gpio,
             s_state.rx_gpio,
             s_state.self_test_enabled ? "on" : "off",
             s_state.minimal_app_mode ? "minimal" : "full");

    snprintf(s_counts_text, sizeof(s_counts_text),
             "Traffic RX total/ext/std/filtered: %lu / %lu / %lu / %lu   queued=%lu\n"
             "Target matches basic/ext/badDLC: %lu / %lu / %lu   stale=%s",
             (unsigned long)s_state.total_rx_frames,
             (unsigned long)s_state.total_extended_frames,
             (unsigned long)s_state.total_standard_frames,
             (unsigned long)s_state.total_filtered_frames,
             (unsigned long)s_state.queued_rx_count,
             (unsigned long)s_state.matched_basic_frames,
             (unsigned long)s_state.matched_extended_frames,
             (unsigned long)s_state.invalid_target_frame_count,
             s_state.target_signal_stale ? "yes" : "no");

    snprintf(s_health_text, sizeof(s_health_text),
             "Controller TEC/REC=%lu/%lu  busErr=%lu  missed=%lu  overrun=%lu\n"
             "TX alerts ok/fail/idle=%lu/%lu/%lu   RX alerts=%lu   lastAlert=0x%08lX",
             (unsigned long)s_state.tx_error_counter,
             (unsigned long)s_state.rx_error_counter,
             (unsigned long)s_state.bus_error_count,
             (unsigned long)s_state.rx_missed_count,
             (unsigned long)s_state.rx_overrun_count,
             (unsigned long)s_state.alert_tx_success_count,
             (unsigned long)s_state.alert_tx_failed_count,
             (unsigned long)s_state.alert_tx_idle_count,
             (unsigned long)s_state.alert_rx_data_count,
             (unsigned long)s_state.last_alerts);

    snprintf(s_alerts_text, sizeof(s_alerts_text),
             "Alerts errPass/busOff/recovered/above/belowWarn: %lu/%lu/%lu/%lu/%lu\n"
             "arbLost=%lu  queueFull=%lu  fifoOver=%lu  recover=%lu  periphReset=%lu",
             (unsigned long)s_state.alert_err_pass_count,
             (unsigned long)s_state.alert_bus_off_count,
             (unsigned long)s_state.alert_bus_recovered_count,
             (unsigned long)s_state.alert_above_err_warn_count,
             (unsigned long)s_state.alert_below_err_warn_count,
             (unsigned long)s_state.alert_arb_lost_count,
             (unsigned long)s_state.alert_rx_queue_full_count,
             (unsigned long)s_state.alert_rx_fifo_overrun_count,
             (unsigned long)s_state.alert_recovery_in_progress_count,
             (unsigned long)s_state.alert_periph_reset_count);

    snprintf(s_diag_text, sizeof(s_diag_text),
             "Probe %s  GPIO%d edges=%lu init=%u  auxGPIO%d edges=%lu init=%u dur=%lu ms\n"
             "J1939 probe tx/fail/last=%lu/%lu/%s   self tx/rx=%lu/%lu",
             s_state.rx_edge_probe_done ? "done" : "pending",
             s_state.rx_gpio,
             (unsigned long)s_state.rx_edge_probe_count,
             (unsigned)s_state.rx_edge_probe_initial_level,
             (unsigned)s_state.aux_rx_probe_gpio,
             (unsigned long)s_state.aux_rx_probe_count,
             (unsigned)s_state.aux_rx_probe_initial_level,
             (unsigned long)s_state.rx_edge_probe_duration_ms,
             (unsigned long)s_state.j1939_request_probe_tx_count,
             (unsigned long)s_state.j1939_request_probe_fail_count,
             ui_j1939_probe_step_to_str(s_state.j1939_request_probe_last_step),
             (unsigned long)s_state.self_test_tx_count,
             (unsigned long)s_state.self_test_rx_count);

    snprintf(s_target_text, sizeof(s_target_text),
             "Target lastRx=%lu ms  age=%lu ms  stale=%s  basicID=%s  extID=%s",
             (unsigned long)s_state.last_target_rx_ms,
             (unsigned long)last_target_age_ms,
             s_state.target_signal_stale ? "yes" : "no",
             s_state.decoded_basic.valid ? "0x0CFDD6xx" : "--",
             s_state.decoded_extended.valid ? "0x0CFDD7xx" : "--");

    if (s_state.has_last_frame) {
        snprintf(s_last_frame_text, sizeof(s_last_frame_text),
                 "Last frame: %s ID=0x%08lX RTR=%d DLC=%u",
                 s_state.last_frame_extended ? "EXT" : "STD",
                 (unsigned long)s_state.last_frame_id,
                 s_state.last_frame_rtr ? 1 : 0,
                 (unsigned)s_state.last_frame_dlc);
        ui_format_frame_data(s_last_frame_data_text, sizeof(s_last_frame_data_text),
                             s_state.last_frame_data, s_state.last_frame_dlc);
    } else {
        snprintf(s_last_frame_text, sizeof(s_last_frame_text), "Last frame: --");
        snprintf(s_last_frame_data_text, sizeof(s_last_frame_data_text), "Data: --");
    }

    if (s_state.has_last_standard_frame) {
        snprintf(s_last_std_text, sizeof(s_last_std_text),
                 "Last STD: ID=0x%03lX RTR=%d DLC=%u",
                 (unsigned long)s_state.last_standard_id,
                 s_state.last_standard_rtr ? 1 : 0,
                 (unsigned)s_state.last_standard_dlc);
        ui_format_frame_data(s_last_std_data_text, sizeof(s_last_std_data_text),
                             s_state.last_standard_data, s_state.last_standard_dlc);
    } else {
        snprintf(s_last_std_text, sizeof(s_last_std_text), "Last STD: --");
        snprintf(s_last_std_data_text, sizeof(s_last_std_data_text), "Data: --");
    }

    if (s_state.has_last_extended_frame) {
        snprintf(s_last_ext_text, sizeof(s_last_ext_text),
                 "Last EXT: ID=0x%08lX PGN=0x%05lX RTR=%d DLC=%u",
                 (unsigned long)s_state.last_extended_id,
                 (unsigned long)s_state.last_extended_pgn,
                 s_state.last_extended_rtr ? 1 : 0,
                 (unsigned)s_state.last_extended_dlc);
        ui_format_frame_data(s_last_ext_data_text, sizeof(s_last_ext_data_text),
                             s_state.last_extended_data, s_state.last_extended_dlc);
    } else {
        snprintf(s_last_ext_text, sizeof(s_last_ext_text), "Last EXT: --");
        snprintf(s_last_ext_data_text, sizeof(s_last_ext_data_text), "Data: --");
    }

    if (s_state.decoded_basic.valid) {
        snprintf(s_basic_text, sizeof(s_basic_text),
                 "Decoded Basic: %s src=%s(0x%02X) ID=0x%08lX PGN=0x%05lX last=%lu ms age=%lu ms",
                 s_state.decoded_basic.stale ? "stale" : "valid",
                 ui_source_label(s_state.decoded_basic.source_address),
                 (unsigned)s_state.decoded_basic.source_address,
                 (unsigned long)(UI_BASIC_MESSAGE_BASE_ID | s_state.decoded_basic.source_address),
                 (unsigned long)s_state.decoded_basic.pgn,
                 (unsigned long)s_state.decoded_basic.last_rx_ms,
                 (unsigned long)ui_get_ms_since(s_state.decoded_basic.last_rx_ms));
        ui_format_button_summary(s_button_summary, sizeof(s_button_summary),
                                 s_state.decoded_basic.button_status);
        snprintf(s_basic_data_text, sizeof(s_basic_data_text),
                 "BaseX=%u N/L/R=%s/%s/%s\n"
                 "BaseY=%u N/B/F=%s/%s/%s\n"
                 "Buttons=%s",
                 (unsigned)s_state.decoded_basic.base_x.position,
                 ui_signal_status_to_str(s_state.decoded_basic.base_x.neutral_status),
                 ui_signal_status_to_str(s_state.decoded_basic.base_x.negative_status),
                 ui_signal_status_to_str(s_state.decoded_basic.base_x.positive_status),
                 (unsigned)s_state.decoded_basic.base_y.position,
                 ui_signal_status_to_str(s_state.decoded_basic.base_y.neutral_status),
                 ui_signal_status_to_str(s_state.decoded_basic.base_y.negative_status),
                 ui_signal_status_to_str(s_state.decoded_basic.base_y.positive_status),
                 s_button_summary);
    } else {
        snprintf(s_basic_text, sizeof(s_basic_text), "Decoded Basic: --");
        snprintf(s_basic_data_text, sizeof(s_basic_data_text), "BaseX/ BaseY/ Buttons: --");
    }

    if (s_state.decoded_extended.valid) {
        snprintf(s_ext_text, sizeof(s_ext_text),
                 "Decoded Ext: %s src=%s(0x%02X) ID=0x%08lX PGN=0x%05lX last=%lu ms age=%lu ms",
                 s_state.decoded_extended.stale ? "stale" : "valid",
                 ui_source_label(s_state.decoded_extended.source_address),
                 (unsigned)s_state.decoded_extended.source_address,
                 (unsigned long)(UI_EXT_MESSAGE_BASE_ID | s_state.decoded_extended.source_address),
                 (unsigned long)s_state.decoded_extended.pgn,
                 (unsigned long)s_state.decoded_extended.last_rx_ms,
                 (unsigned long)ui_get_ms_since(s_state.decoded_extended.last_rx_ms));
        snprintf(s_ext_data_text, sizeof(s_ext_data_text),
                 "GripX=%u N/L/R=%s/%s/%s\n"
                 "GripY=%u N/B/F=%s/%s/%s\n"
                 "Theta=%u N/CCW/CW=%s/%s/%s",
                 (unsigned)s_state.decoded_extended.grip_x.position,
                 ui_signal_status_to_str(s_state.decoded_extended.grip_x.neutral_status),
                 ui_signal_status_to_str(s_state.decoded_extended.grip_x.negative_status),
                 ui_signal_status_to_str(s_state.decoded_extended.grip_x.positive_status),
                 (unsigned)s_state.decoded_extended.grip_y.position,
                 ui_signal_status_to_str(s_state.decoded_extended.grip_y.neutral_status),
                 ui_signal_status_to_str(s_state.decoded_extended.grip_y.negative_status),
                 ui_signal_status_to_str(s_state.decoded_extended.grip_y.positive_status),
                 (unsigned)s_state.decoded_extended.theta.position,
                 ui_signal_status_to_str(s_state.decoded_extended.theta.neutral_status),
                 ui_signal_status_to_str(s_state.decoded_extended.theta.negative_status),
                 ui_signal_status_to_str(s_state.decoded_extended.theta.positive_status));
    } else {
        snprintf(s_ext_text, sizeof(s_ext_text), "Decoded Ext: --");
        snprintf(s_ext_data_text, sizeof(s_ext_data_text), "GripX/ GripY/ Theta: --");
    }

    s_history_text[0] = '\0';
    if (s_state.history_count == 0) {
        snprintf(s_history_text, sizeof(s_history_text), "Recent frames:\n--");
    } else {
        size_t used = (size_t)snprintf(s_history_text, sizeof(s_history_text), "Recent frames:");
        for (uint8_t i = 0; i < s_state.history_count && i < UI_CAN_FRAME_HISTORY_LEN; i++) {
            char line_text[96];
            int written;

            ui_format_history_line(line_text, sizeof(line_text), &s_state.history[i]);
            written = snprintf(s_history_text + used, sizeof(s_history_text) - used,
                               "\n%u. %s", (unsigned)(i + 1), line_text);
            if (written < 0 || (size_t)written >= (sizeof(s_history_text) - used)) {
                break;
            }
            used += (size_t)written;
        }
    }

    lv_label_set_text(s_mode_label, s_mode_text);
    lv_label_set_text(s_counts_label, s_counts_text);
    lv_label_set_text(s_health_label, s_health_text);
    lv_label_set_text(s_alerts_label, s_alerts_text);
    lv_label_set_text(s_diag_label, s_diag_text);
    lv_label_set_text(s_target_label, s_target_text);
    lv_label_set_text(s_last_frame_label, s_last_frame_text);
    lv_label_set_text_fmt(s_last_frame_data_label, "Data: %s", s_last_frame_data_text);
    lv_label_set_text(s_last_std_label, s_last_std_text);
    lv_label_set_text_fmt(s_last_std_data_label, "Data: %s", s_last_std_data_text);
    lv_label_set_text(s_last_ext_label, s_last_ext_text);
    lv_label_set_text_fmt(s_last_ext_data_label, "Data: %s", s_last_ext_data_text);
    lv_label_set_text(s_decoded_basic_label, s_basic_text);
    lv_label_set_text(s_decoded_basic_data_label, s_basic_data_text);
    lv_label_set_text(s_decoded_ext_label, s_ext_text);
    lv_label_set_text(s_decoded_ext_data_label, s_ext_data_text);
    lv_label_set_text(s_history_label, s_history_text);
}

static void ui_refresh_all(void)
{
    ui_refresh_diag_labels();
    ui_refresh_graph_labels();
    ui_update_graph_series();
}

void ui_init(void)
{
    board_display_t display = {0};
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();

    ESP_LOGI(TAG, "Initializing display board");
    if (board_init(&display) != ESP_OK) {
        ESP_LOGE(TAG, "Board init failed");
        return;
    }

    ESP_LOGI(TAG, "Initializing LVGL port");
    if (lvgl_port_init(&lvgl_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port init failed");
        return;
    }

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = NULL,
        .panel_handle = display.panel_handle,
        .control_handle = NULL,
        .buffer_size = BOARD_LCD_H_RES * CONFIG_CANBUS_LVGL_BUF_LINES,
        .double_buffer = false,
        .trans_size = 0,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
            .full_refresh = false,
            .direct_mode = false,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = false,
            .avoid_tearing = false,
        },
    };

    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "LVGL display add failed");
        return;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = display.touch_handle,
        .scale = {1.0f, 1.0f},
    };

    if (!lvgl_port_add_touch(&touch_cfg)) {
        ESP_LOGW(TAG, "Touch add failed");
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "LVGL lock failed");
        return;
    }

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xA9A9A9), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title_label = lv_label_create(scr);
    lv_label_set_text(title_label, "CAN Decoder");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_28, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 20, 10);

    lv_obj_t *tab_container = lv_obj_create(scr);
    lv_obj_set_size(tab_container, 248, 44);
    lv_obj_align(tab_container, LV_ALIGN_TOP_RIGHT, -16, 10);
    lv_obj_set_style_bg_opa(tab_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tab_container, 0, 0);
    lv_obj_set_style_pad_all(tab_container, 0, 0);
    lv_obj_set_layout(tab_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab_container, 8, 0);
    lv_obj_clear_flag(tab_container, LV_OBJ_FLAG_SCROLLABLE);

    s_diag_tab_btn = lv_btn_create(tab_container);
    lv_obj_set_size(s_diag_tab_btn, 120, 40);
    lv_obj_add_event_cb(s_diag_tab_btn, ui_tab_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)UI_PAGE_DIAGNOSTICS);
    lv_obj_t *diag_tab_label = lv_label_create(s_diag_tab_btn);
    lv_label_set_text(diag_tab_label, "Diagnostics");
    lv_obj_center(diag_tab_label);

    s_graph_tab_btn = lv_btn_create(tab_container);
    lv_obj_set_size(s_graph_tab_btn, 120, 40);
    lv_obj_add_event_cb(s_graph_tab_btn, ui_tab_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)UI_PAGE_GRAPH);
    lv_obj_t *graph_tab_label = lv_label_create(s_graph_tab_btn);
    lv_label_set_text(graph_tab_label, "Graph");
    lv_obj_center(graph_tab_label);

    s_diag_page = lv_obj_create(scr);
    lv_obj_set_size(s_diag_page, 780, 404);
    lv_obj_align(s_diag_page, LV_ALIGN_TOP_LEFT, 10, 66);
    lv_obj_set_style_bg_opa(s_diag_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_diag_page, 0, 0);
    lv_obj_set_style_pad_all(s_diag_page, 8, 0);
    lv_obj_set_scrollbar_mode(s_diag_page, LV_SCROLLBAR_MODE_AUTO);

    s_mode_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_mode_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_mode_label, 744);
    lv_obj_align(s_mode_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_counts_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_counts_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_counts_label, 744);
    lv_obj_align(s_counts_label, LV_ALIGN_TOP_LEFT, 0, 42);

    s_health_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_health_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_health_label, 744);
    lv_obj_align(s_health_label, LV_ALIGN_TOP_LEFT, 0, 76);

    s_alerts_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_alerts_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_alerts_label, 744);
    lv_obj_align(s_alerts_label, LV_ALIGN_TOP_LEFT, 0, 110);

    s_diag_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_diag_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_diag_label, 744);
    lv_obj_align(s_diag_label, LV_ALIGN_TOP_LEFT, 0, 162);

    s_target_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_target_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_target_label, 744);
    lv_obj_align(s_target_label, LV_ALIGN_TOP_LEFT, 0, 212);

    s_last_frame_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_last_frame_label, lv_color_hex(0x000000), 0);
    lv_obj_align(s_last_frame_label, LV_ALIGN_TOP_LEFT, 0, 262);

    s_last_frame_data_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_last_frame_data_label, lv_color_hex(0x000000), 0);
    lv_obj_align(s_last_frame_data_label, LV_ALIGN_TOP_LEFT, 0, 284);

    s_last_std_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_last_std_label, lv_color_hex(0x000000), 0);
    lv_obj_align(s_last_std_label, LV_ALIGN_TOP_LEFT, 0, 310);

    s_last_std_data_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_last_std_data_label, lv_color_hex(0x000000), 0);
    lv_obj_align(s_last_std_data_label, LV_ALIGN_TOP_LEFT, 0, 332);

    s_last_ext_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_last_ext_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_last_ext_label, 744);
    lv_obj_align(s_last_ext_label, LV_ALIGN_TOP_LEFT, 0, 358);

    s_last_ext_data_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_last_ext_data_label, lv_color_hex(0x000000), 0);
    lv_obj_align(s_last_ext_data_label, LV_ALIGN_TOP_LEFT, 0, 380);

    s_decoded_basic_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_decoded_basic_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_decoded_basic_label, 744);
    lv_obj_align(s_decoded_basic_label, LV_ALIGN_TOP_LEFT, 0, 406);

    s_decoded_basic_data_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_decoded_basic_data_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_decoded_basic_data_label, 744);
    lv_obj_align(s_decoded_basic_data_label, LV_ALIGN_TOP_LEFT, 0, 428);

    s_decoded_ext_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_decoded_ext_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_decoded_ext_label, 744);
    lv_obj_align(s_decoded_ext_label, LV_ALIGN_TOP_LEFT, 0, 478);

    s_decoded_ext_data_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_decoded_ext_data_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_decoded_ext_data_label, 744);
    lv_obj_align(s_decoded_ext_data_label, LV_ALIGN_TOP_LEFT, 0, 500);

    s_history_label = lv_label_create(s_diag_page);
    lv_obj_set_style_text_color(s_history_label, lv_color_hex(0x000000), 0);
    lv_obj_set_width(s_history_label, 744);
    lv_obj_align(s_history_label, LV_ALIGN_TOP_LEFT, 0, 564);

    s_graph_page = lv_obj_create(scr);
    lv_obj_set_size(s_graph_page, 780, 404);
    lv_obj_align(s_graph_page, LV_ALIGN_TOP_LEFT, 10, 66);
    lv_obj_set_style_bg_opa(s_graph_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_graph_page, 0, 0);
    lv_obj_set_style_pad_all(s_graph_page, 8, 0);
    lv_obj_clear_flag(s_graph_page, LV_OBJ_FLAG_SCROLLABLE);

    s_graph_title_label = lv_label_create(s_graph_page);
    lv_obj_set_style_text_color(s_graph_title_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(s_graph_title_label, &lv_font_montserrat_28, 0);
    lv_obj_align(s_graph_title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_graph_mode_label = lv_label_create(s_graph_page);
    lv_obj_set_style_text_color(s_graph_mode_label, lv_color_hex(0x1F5AA6), 0);
    lv_obj_set_style_text_font(s_graph_mode_label, &lv_font_montserrat_48, 0);
    lv_obj_align(s_graph_mode_label, LV_ALIGN_TOP_LEFT, 0, 42);

    s_graph_value_label = lv_label_create(s_graph_page);
    lv_obj_set_style_text_color(s_graph_value_label, lv_color_hex(0x1F5AA6), 0);
    lv_obj_set_style_text_font(s_graph_value_label, &lv_font_montserrat_48, 0);
    lv_obj_align(s_graph_value_label, LV_ALIGN_TOP_RIGHT, -8, 42);

    s_graph_status_label = lv_label_create(s_graph_page);
    lv_obj_set_style_text_color(s_graph_status_label, lv_color_hex(0x202020), 0);
    lv_obj_set_width(s_graph_status_label, 760);
    lv_obj_align(s_graph_status_label, LV_ALIGN_TOP_LEFT, 0, 104);

    s_graph_chart = lv_chart_create(s_graph_page);
    lv_obj_set_size(s_graph_chart, 640, 246);
    lv_obj_align(s_graph_chart, LV_ALIGN_TOP_LEFT, 0, 146);
    lv_obj_set_style_bg_color(s_graph_chart, lv_color_hex(0xECEFE7), 0);
    lv_obj_set_style_border_color(s_graph_chart, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_border_width(s_graph_chart, 1, 0);
    lv_chart_set_type(s_graph_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(s_graph_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(s_graph_chart, UI_GRAPH_POINT_COUNT);
    lv_chart_set_div_line_count(s_graph_chart, 6, 6);
    lv_chart_set_range(s_graph_chart, LV_CHART_AXIS_PRIMARY_Y, UI_GRAPH_VALUE_MIN, UI_GRAPH_VALUE_MAX);
    lv_chart_set_axis_tick(s_graph_chart, LV_CHART_AXIS_PRIMARY_Y, 8, 4, 7, 1, true, 55);
    lv_chart_set_axis_tick(s_graph_chart, LV_CHART_AXIS_PRIMARY_X, 6, 4, 7, 1, true, 35);
    s_graph_series = lv_chart_add_series(s_graph_chart, lv_color_hex(0x005F73), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(s_graph_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_graph_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s_graph_chart, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_graph_chart, lv_color_hex(0x202020), LV_PART_TICKS);

    s_graph_forward_hint_label = lv_label_create(s_graph_page);
    lv_label_set_text(s_graph_forward_hint_label, "FORWARD");
    lv_obj_set_style_text_color(s_graph_forward_hint_label, lv_color_hex(0x2E8B57), 0);
    lv_obj_set_style_text_font(s_graph_forward_hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(s_graph_forward_hint_label, s_graph_chart, LV_ALIGN_OUT_RIGHT_TOP, 18, 6);

    s_graph_neutral_hint_label = lv_label_create(s_graph_page);
    lv_label_set_text(s_graph_neutral_hint_label, "NEUTRAL");
    lv_obj_set_style_text_color(s_graph_neutral_hint_label, lv_color_hex(0x1F5AA6), 0);
    lv_obj_set_style_text_font(s_graph_neutral_hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(s_graph_neutral_hint_label, s_graph_chart, LV_ALIGN_OUT_RIGHT_MID, 18, 0);

    s_graph_reverse_hint_label = lv_label_create(s_graph_page);
    lv_label_set_text(s_graph_reverse_hint_label, "REVERSE");
    lv_obj_set_style_text_color(s_graph_reverse_hint_label, lv_color_hex(0xC0392B), 0);
    lv_obj_set_style_text_font(s_graph_reverse_hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(s_graph_reverse_hint_label, s_graph_chart, LV_ALIGN_OUT_RIGHT_BOTTOM, 18, -6);

    memset(&s_state, 0, sizeof(s_state));
    s_graph_last_sample_ms = 0;
    ui_seed_graph_series();
    ui_select_page(UI_PAGE_DIAGNOSTICS);
    ui_refresh_all();
    s_ui_ready = true;

    lvgl_port_unlock();
}

void ui_set_can_sniffer_state(const ui_can_sniffer_state_t *state)
{
    if (!state) {
        return;
    }

    s_state = *state;
    if (!s_ui_ready) {
        return;
    }

    if (!lvgl_port_lock(0)) {
        return;
    }

    ui_refresh_all();
    lvgl_port_unlock();
}

void ui_clear_can_sniffer_state(void)
{
    ui_can_sniffer_state_t cleared = {0};
    ui_set_can_sniffer_state(&cleared);
}
