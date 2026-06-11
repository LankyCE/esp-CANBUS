# canBUS Repository Documentation

## Purpose
This project is an ESP-IDF application for the Waveshare `ESP32-S3-Touch-LCD-4.3B` that:
- initializes the `800x480` RGB display and GT911 capacitive touch stack,
- receives CAN traffic through the onboard transceiver on `GPIO15/16`,
- decodes the documented PQ joystick J1939 payloads for `PGN 0xFDD6` and `PGN 0xFDD7`,
- keeps raw last-frame information and recent frame history in the LVGL UI,
- shows decoded base, grip, theta, and button state information in the LVGL UI,
- shows a rolling `30 second` Base Y graph with a large live Base Y readout,
- shows CAN health counters on screen,
- runs the CAN controller at the documented fixed bus speed of `250 kbps`.

The target device is still the PQ joystick or hall-effect control device that should emit J1939 extended frames at `250 kbps`, and this build now keeps both the sniffer diagnostics and payload decode active at the same time.

## Repository Layout
- `main/main.c`: app entry point.
- `main/board/`: board bring-up for the Waveshare display/touch hardware.
- `main/canbus/`: fixed-rate TWAI startup, raw CAN sniffer receive loop, and joystick payload decoder.
- `main/ui/`: LVGL diagnostic screen for raw and decoded CAN state.
- `canbusINFO.txt`: text summary of the target CAN messages.
- `can.md`: CAN implementation notes.
- `docs/hardware/esp32-s3-touch-lcd-4.3b.md`: Waveshare board notes.
- `docs/hardware/TS CAN info.pdf`: extracted source document for the target joystick CAN format.
- `docs/hardware/ts-can-info.md`: markdown summary of that PDF.

## Runtime Flow
1. `app_main()` creates the `canbus_startup` task.
2. The startup task calls `ui_init()`.
3. `ui_init()` calls `board_init()` and brings up the display, backlight, touch reset, and GT911 touch input.
4. The startup task then calls `canbus_start()`.
5. `canbus_start()`:
   - resets cached sniffer state,
   - starts TWAI on `GPIO15/16`,
   - uses the documented fixed bitrate of `250 kbps`,
   - uses the configured runtime mode (`NORMAL`, `NO_ACK`, or `LISTEN_ONLY`),
   - starts the RX task,
   - publishes raw receive state to the screen or serial-only mode.
6. The RX task:
   - waits on TWAI alerts and drains received frames when `RX_DATA` fires,
   - can transmit self-test self-reception frames when enabled,
   - counts standard and extended frames separately,
   - can software-filter frames to `ALL`, `EXT_ONLY`, or `STD_ONLY`,
   - detects target extended frames with `PGN 0xFDD6` and `0xFDD7`,
   - validates that target frames use `DLC=8`,
   - decodes axis status, axis position, source address, and grip button state from valid target frames,
   - marks decoded target data stale if no fresh target frame arrives within the configured timeout,
   - counts TWAI alert conditions such as RX, TX, error-passive, bus-error, bus-off, and queue/fifo issues,
   - starts recovery automatically if the controller enters `BUS_OFF`,
   - emits a periodic serial heartbeat with current counters and error state,
   - stores the last received frame,
   - stores the last received standard frame,
   - stores the last received extended frame,
   - keeps a short rolling frame history,
   - updates TWAI error and queue counters,
   - pushes the diagnostic state to the LVGL UI.

## Bench-Proven Wiring Context

The most important bench finding so far is that the RH112 wiring was initially powered backwards.

Working RH112 wire mapping observed on the bench:
- `red` -> `+24VDC`
- `black` -> `0V / GND`
- `blue` -> `CANH`
- `white` -> `CANL`

Before flipping `red` and `black`, the ESP32 saw no meaningful CAN activity. After correcting power polarity, the startup raw RX probe on `GPIO16` began showing heavy transition counts immediately, which confirmed that valid bus activity was finally reaching the Waveshare transceiver.

Two labeled units seen during bring-up:
- `RH112BDL-204 SN 250703 CAL#5 ADDRESS 34H`
- `RH112BDL-204 SN 260123 CAL#6 CAN ADDRESS J35H`

Those labels align with the documented source-address family:
- `0x34` -> Left
- `0x35` -> Center

The exact `RH112BDL-204` suffix does not match the public catalog numbering format cleanly, so it should be treated as a custom/internal part number. The address labels and live decode behavior were more useful than the raw part number for bench validation.

## CAN Messages Being Investigated
Source: `docs/hardware/TS CAN info.pdf`

### Target bus settings
- CAN speed: `250 kbps`
- Frame type: `29-bit extended CAN`
- Protocol shape: `J1939`

### Source addresses
- Right: `0x33`
- Left: `0x34`
- Center: `0x35`
- Auxiliary: `0x36`

### Basic joystick message
- PGN: `0xFDD6`
- Identifier pattern: `0x0CFDD6xx`
- DLC: `8`
- Contains:
  - base X axis status and 10-bit position,
  - base Y axis status and 10-bit position,
  - button states for buttons `1..12`.

### Extended joystick message
- PGN: `0xFDD7`
- Identifier pattern: `0x0CFDD7xx`
- DLC: `8`
- Contains:
  - grip X axis status and 10-bit position,
  - grip Y axis status and 10-bit position,
  - theta axis status and 10-bit position.

### Status encoding
Two-bit fields use:
- `00`: inactive / not pressed
- `01`: active / pressed
- `10`: error
- `11`: not available

### Position decode
Each axis is a 10-bit value:
- top two bits of the current byte are the low two bits of the axis,
- next byte contains the upper eight bits.

Code formula:
```c
position = (next_byte << 2) | ((current_byte >> 6) & 0x3);
```

## UI Behavior

The UI now has two pages, switched by top-right tab buttons:
- `Diagnostics`
- `Graph`

The `Diagnostics` page shows:
- runtime mode, filter mode, bitrate, and configured pins,
- total RX counts and filtered-frame count,
- target basic/ext match counters and invalid target DLC count,
- bus error, queue, TX, and self-test counters,
- TWAI alert counters and the last alert bitmask,
- the last received frame of any kind,
- the last decoded basic joystick message,
- the last decoded extended joystick message,
- a short recent-frame history

The `Graph` page shows:
- a rolling `30 second` chart of decoded `Base Y`,
- a large live `Base Y` value in the upper corner,
- basic decode freshness/source/status context for the plotted signal.

At this point, `Base Y` is the main decoded value that has proven useful in live testing.

## Key Config Knobs
From `main/canbus/canbus_config.h`:
- `CANBUS_BITRATE_HZ = 250000`
- `CANBUS_TWAI_TX_GPIO = 15`
- `CANBUS_TWAI_RX_GPIO = 16`
- `CANBUS_RUNTIME_MODE = CANBUS_MODE_NORMAL`
- `CANBUS_FRAME_FILTER_MODE = CANBUS_FILTER_ALL`
- `CANBUS_ENABLE_SELF_TEST = 0`
- `CANBUS_MINIMAL_APP_MODE = 0`
- `CANBUS_ENABLE_J1939_REQUEST_PROBE = 1`
- `CANBUS_ENABLE_RX_EDGE_PROBE = 1`
- `CANBUS_HEARTBEAT_PERIOD_MS = 1000`

## Monitoring Workflow

The most reliable serial workflow ended up being:
- flash without monitor using [flash.ps1](../flash.ps1)
- then attach using [monitor-safe.ps1](../monitor-safe.ps1)

Why this exists:
- ordinary serial monitor attach could reset the ESP32-S3 USB serial path at awkward times,
- `monitor-safe.ps1` first performs a controlled reset with `esptool`, then opens the port without relying on the more fragile monitor path.

Useful command:
```powershell
.\monitor-safe.ps1 -Port COM5 -DurationSeconds 30
```

## 120 Ohm Termination
`docs/hardware/TS CAN info.pdf` does not mention `120 ohm` termination or any resistor/termination detail. This repo therefore documents the CAN message format and bitrate from the PDF, but not the physical-layer termination requirements of the external joystick device.

## Board Pin Notes
From the official Waveshare 4.3B board documentation:
- `GPIO15`: `CANTX`
- `GPIO16`: `CANRX`
- RS485 is a separate onboard path and should not be confused with the CAN pins during troubleshooting.

The PDF changes the expected bus protocol and identifiers, not the ESP32 board pin mapping.
