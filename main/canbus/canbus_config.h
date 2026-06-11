#pragma once

// Fixed CAN bitrate for the PQ joystick J1939 messages described in
// docs/hardware/TS CAN info.pdf.
#define CANBUS_BITRATE_HZ              250000

// Update UI stale state if no valid joystick message arrives within this window.
#define CANBUS_SIGNAL_STALE_TIMEOUT_MS 500

// Waveshare ESP32-S3-Touch-LCD-4.3B onboard CAN interface:
//   GPIO15 -> CANTX
//   GPIO16 -> CANRX
// Official docs also map GPIO12/GPIO13 to the TF card, not CAN.
#define CANBUS_TWAI_TX_GPIO            15
#define CANBUS_TWAI_RX_GPIO            16

// Diagnostic controller mode.
#define CANBUS_MODE_NORMAL               0
#define CANBUS_MODE_NO_ACK               1
#define CANBUS_MODE_LISTEN_ONLY          2
#define CANBUS_RUNTIME_MODE              CANBUS_MODE_NORMAL

// Optional software-side frame filter for diagnostics.
#define CANBUS_FILTER_ALL                0
#define CANBUS_FILTER_EXT_ONLY           1
#define CANBUS_FILTER_STD_ONLY           2
#define CANBUS_FRAME_FILTER_MODE         CANBUS_FILTER_ALL

// Optional self-test/loopback using self-reception requests.
// When enabled, the runtime mode should normally be CANBUS_MODE_NO_ACK.
#define CANBUS_ENABLE_SELF_TEST          0
#define CANBUS_SELF_TEST_PERIOD_MS       1000

// Optional stripped-down runtime that skips LVGL init and relies on serial logs.
#define CANBUS_MINIMAL_APP_MODE          0

// USB recovery firmware: do not initialize CAN or UI. Just boot and emit a
// periodic serial heartbeat so we can verify basic firmware/USB stability.
#define CANBUS_USB_RECOVERY_MODE         0

// Serial debug output for decoded signal frames.
#define CANBUS_DEBUG_LOG_DECODED        1

// Serial debug output for every received CAN frame (sniffer mode).
#define CANBUS_DEBUG_LOG_ALL_RX          1

// Set >0 to rate-limit raw frame logs and emit suppression summaries.
#define CANBUS_DEBUG_LOG_MIN_INTERVAL_MS 100

// Alert wait time for the CAN RX task.
#define CANBUS_ALERT_WAIT_TIMEOUT_MS     100

// Periodic TWAI status logging interval in milliseconds.
#define CANBUS_STATUS_LOG_PERIOD_MS      2000

// Periodic serial heartbeat interval in milliseconds.
#define CANBUS_HEARTBEAT_PERIOD_MS       1000

// Enable serial command injection in monitor for bench testing without CAN hardware.
#define CANBUS_ENABLE_SERIAL_INJECT      1

// Active bus probe: transmit a harmless diagnostic frame in NORMAL mode and
// use TX success/failure to determine whether another node is present to ACK it.
#define CANBUS_ENABLE_ACTIVE_ACK_PROBE   0
#define CANBUS_ACTIVE_ACK_PROBE_PERIOD_MS 1000

// Active J1939 diagnostics: issue request frames for common joystick PGNs and
// watch for ACK/response behavior from the external node.
#define CANBUS_ENABLE_J1939_REQUEST_PROBE 1
#define CANBUS_J1939_REQUEST_PROBE_PERIOD_MS 2000

// Startup diagnostic: sample the transceiver RXD pin as a plain GPIO before TWAI
// takes ownership. This helps detect whether any CAN-like bus activity is reaching
// the transceiver even when no valid frames are decoded.
#define CANBUS_ENABLE_RX_EDGE_PROBE      1
#define CANBUS_RX_EDGE_PROBE_DURATION_MS 15000

// Disabled by default: on ESP32-S3, GPIO19/GPIO20 are the built-in USB D-/D+
// pins, so probing them as normal GPIO can break USB Serial/JTAG comms.
#define CANBUS_ENABLE_AUX_RX_EDGE_PROBE  0
#define CANBUS_AUX_RX_PROBE_GPIO         20
