#pragma once

#include <stdbool.h>
#include <stdint.h>

#define UI_CAN_FRAME_HISTORY_LEN 3

typedef enum {
    UI_CAN_SIGNAL_STATUS_INACTIVE = 0,
    UI_CAN_SIGNAL_STATUS_ACTIVE = 1,
    UI_CAN_SIGNAL_STATUS_ERROR = 2,
    UI_CAN_SIGNAL_STATUS_NOT_AVAILABLE = 3,
} ui_can_signal_status_t;

typedef struct {
    ui_can_signal_status_t neutral_status;
    ui_can_signal_status_t negative_status;
    ui_can_signal_status_t positive_status;
    uint16_t position;
} ui_can_axis_decoded_t;

typedef struct {
    bool valid;
    bool stale;
    uint8_t source_address;
    uint32_t pgn;
    uint32_t last_rx_ms;
    ui_can_axis_decoded_t base_x;
    ui_can_axis_decoded_t base_y;
    ui_can_signal_status_t button_status[12];
} ui_can_basic_decoded_t;

typedef struct {
    bool valid;
    bool stale;
    uint8_t source_address;
    uint32_t pgn;
    uint32_t last_rx_ms;
    ui_can_axis_decoded_t grip_x;
    ui_can_axis_decoded_t grip_y;
    ui_can_axis_decoded_t theta;
} ui_can_extended_decoded_t;

typedef struct {
    bool valid;
    uint32_t id;
    uint8_t dlc;
    bool extended;
    bool rtr;
    uint8_t data[8];
} ui_can_frame_snapshot_t;

typedef struct {
    uint8_t runtime_mode;
    uint8_t frame_filter_mode;
    uint8_t controller_state;
    bool self_test_enabled;
    bool minimal_app_mode;
    int bitrate_hz;
    int tx_gpio;
    int rx_gpio;
    uint32_t matched_basic_frames;
    uint32_t matched_extended_frames;
    uint32_t invalid_target_frame_count;
    uint32_t last_target_rx_ms;
    bool target_signal_stale;
    uint32_t total_rx_frames;
    uint32_t total_extended_frames;
    uint32_t total_standard_frames;
    uint32_t total_filtered_frames;
    uint32_t alert_rx_data_count;
    uint32_t alert_tx_success_count;
    uint32_t alert_tx_failed_count;
    uint32_t alert_tx_idle_count;
    uint32_t alert_err_pass_count;
    uint32_t alert_bus_error_count;
    uint32_t alert_bus_off_count;
    uint32_t alert_rx_queue_full_count;
    uint32_t alert_rx_fifo_overrun_count;
    uint32_t alert_above_err_warn_count;
    uint32_t alert_below_err_warn_count;
    uint32_t alert_arb_lost_count;
    uint32_t alert_recovery_in_progress_count;
    uint32_t alert_bus_recovered_count;
    uint32_t alert_periph_reset_count;
    uint32_t last_alerts;
    uint32_t self_test_tx_count;
    uint32_t self_test_rx_count;
    uint32_t j1939_request_probe_tx_count;
    uint32_t j1939_request_probe_fail_count;
    uint8_t j1939_request_probe_last_step;
    uint32_t tx_error_counter;
    uint32_t rx_error_counter;
    uint32_t bus_error_count;
    uint32_t rx_missed_count;
    uint32_t rx_overrun_count;
    uint32_t queued_rx_count;
    bool has_last_frame;
    uint32_t last_frame_id;
    uint8_t last_frame_dlc;
    bool last_frame_extended;
    bool last_frame_rtr;
    uint8_t last_frame_data[8];
    bool has_last_standard_frame;
    uint32_t last_standard_id;
    uint8_t last_standard_dlc;
    bool last_standard_rtr;
    uint8_t last_standard_data[8];
    bool has_last_extended_frame;
    uint32_t last_extended_id;
    uint32_t last_extended_pgn;
    uint8_t last_extended_dlc;
    bool last_extended_rtr;
    uint8_t last_extended_data[8];
    bool rx_edge_probe_done;
    uint32_t rx_edge_probe_count;
    uint32_t rx_edge_probe_duration_ms;
    uint8_t rx_edge_probe_initial_level;
    uint8_t aux_rx_probe_gpio;
    uint8_t aux_rx_probe_initial_level;
    uint32_t aux_rx_probe_count;
    ui_can_basic_decoded_t decoded_basic;
    ui_can_extended_decoded_t decoded_extended;
    uint8_t history_count;
    ui_can_frame_snapshot_t history[UI_CAN_FRAME_HISTORY_LEN];
} ui_can_sniffer_state_t;

void ui_init(void);
void ui_set_can_sniffer_state(const ui_can_sniffer_state_t *state);
void ui_clear_can_sniffer_state(void);
