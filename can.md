# CANBUS Code Documentation (`can.md`)

## Scope
This document covers the CAN-related code in this repository:
- `main/canbus/canbus_config.h`
- `main/canbus/canbus.c`
- `main/ui/ui.h`
- `canbusINFO.txt`
- `docs/hardware/TS CAN info.pdf`

## What This CAN Code Does
The firmware uses ESP-IDF TWAI on the Waveshare `ESP32-S3-Touch-LCD-4.3B` to:
1. Start the onboard CAN interface on `GPIO15/16`.
2. Run at the documented fixed CAN bitrate of `250 kbps`.
3. Use a configurable runtime mode: `NORMAL`, `NO_ACK`, or `LISTEN_ONLY`.
4. Receive any CAN frame with an accept-all hardware filter and optional software frame-class filtering.
5. Optionally run a self-test using self-reception frames in `NO_ACK` mode.
6. Optionally skip LVGL startup and run as a minimal serial-only diagnostic.
7. Decode the documented joystick payloads for `PGN 0xFDD6` and `PGN 0xFDD7`.
8. Publish diagnostic state to the LVGL UI when enabled:
   - total RX counts
   - filtered-frame count
   - target basic/ext match counters
   - invalid target DLC count
   - TWAI alert counters and last alert mask
   - last received frame
   - decoded basic joystick state
   - decoded extended joystick state
   - short rolling frame history
   - TWAI error, queue, TX, and self-test counters

The current bench build also keeps an active J1939 request probe and a startup RX edge probe enabled so the system can distinguish:
- no incoming line activity,
- line activity with the wrong protocol settings,
- successful receive/decode.

This keeps the sniffer diagnostics intact while also decoding the joystick payloads described in the PDF.

## Target CAN Messages
Source: `docs/hardware/TS CAN info.pdf`

### Basic joystick message
- PGN: `0xFDD6`
- Full 29-bit identifier format: `0x0CFDD6xx`
- Frame type: extended CAN
- DLC: `8`
- Source address: low byte of the CAN identifier
- Payload:
  - Base X axis status and 10-bit position
  - Base Y axis status and 10-bit position
  - Grip buttons `1..12`

### Extended joystick message
- PGN: `0xFDD7`
- Full 29-bit identifier format: `0x0CFDD7xx`
- Frame type: extended CAN
- DLC: `8`
- Payload:
  - Grip X axis status and 10-bit position
  - Grip Y axis status and 10-bit position
  - Theta axis status and 10-bit position

### Factory source addresses
- `0x33`: Right
- `0x34`: Left
- `0x35`: Center
- `0x36`: Auxiliary

The decoder accepts any source address, but it labels the documented factory addresses when they match.

## J1939 Decode Rules Used In Code

### PGN extraction
`main/canbus/canbus.c` extracts the PGN from the 29-bit CAN identifier by shifting out the source address and normalizing PDU1 identifiers when needed. For these messages, the effective PGNs are:
- `0xFDD6`
- `0xFDD7`

### PGN extraction still shown for extended frames
The diagnostic UI derives a PGN for any extended frame using:
```c
pgn = (identifier >> 8) & 0x3FFFF;
```

### Payload decode now implemented
- `FDD6` basic joystick:
  - decodes base X and base Y status/position
  - decodes grip button states for buttons `1..12`
- `FDD7` extended joystick:
  - decodes grip X, grip Y, and theta status/position
- Target frames must be extended and use `DLC=8`
- Matching target PGNs with the wrong DLC increment an invalid-target counter instead of overwriting decoded state
- Decoded target values are marked stale if no matching frame arrives within `CANBUS_SIGNAL_STALE_TIMEOUT_MS`

## Application-Specific Constants
Defined in `main/canbus/canbus_config.h`.

- `CANBUS_BITRATE_HZ = 250000`
  - Fixed bitrate from the target PDF.
- `CANBUS_TWAI_TX_GPIO = 15`
- `CANBUS_TWAI_RX_GPIO = 16`
- `CANBUS_RUNTIME_MODE = CANBUS_MODE_NORMAL`
- `CANBUS_FRAME_FILTER_MODE = CANBUS_FILTER_ALL`
- `CANBUS_ENABLE_SELF_TEST = 0`
- `CANBUS_MINIMAL_APP_MODE = 0`
- `CANBUS_DEBUG_LOG_ALL_RX = 1`
- `CANBUS_DEBUG_LOG_MIN_INTERVAL_MS = 100`
- `CANBUS_HEARTBEAT_PERIOD_MS = 1000`
- `CANBUS_ENABLE_J1939_REQUEST_PROBE = 1`
- `CANBUS_ENABLE_RX_EDGE_PROBE = 1`
- `CANBUS_ENABLE_AUX_RX_EDGE_PROBE = 0`

## Runtime Flow
1. `app_main()` creates a dedicated startup task.
2. The startup task initializes the UI and then starts CAN.
3. `canbus_start()`:
   - resets the cached sniffer state,
   - runs a raw RX-edge probe on `GPIO16` before TWAI starts,
   - starts TWAI at the fixed configured rate,
   - logs the active CAN config at startup,
   - launches the RX task.
4. `canbus_rx_task()`:
   - waits on `twai_read_alerts()` with a timeout,
   - accepts any CAN frame at `250 kbps`,
   - can inject self-test self-reception frames,
   - can send J1939 request/start-broadcast probes in `NORMAL` mode,
   - can filter accepted frames to all/extended-only/standard-only,
   - detects `PGN 0xFDD6` and `PGN 0xFDD7`,
   - decodes valid target payloads,
   - logs decoded payloads separately from raw frame logs,
   - counts important TWAI alerts,
   - starts recovery when the controller reaches `BUS_OFF`,
   - emits a periodic serial heartbeat summarizing current state,
   - tracks total, standard, and extended frame counts,
   - stores the last frame, last standard frame, and last extended frame,
   - keeps a short recent-frame history,
   - refreshes TWAI status counters,
   - publishes that diagnostic state to the UI.

## UI State Published By CAN
`main/ui/ui.h` now exposes `ui_can_sniffer_state_t`, containing:
- mode, filter, bitrate, and pin config
- TWAI state
- total RX, extended RX, standard RX, and filtered counts
- target basic/ext counters and invalid target counter
- error, queue, TX, and self-test counters
- last raw frame
- decoded basic joystick state
- decoded extended joystick state

The UI displays:
- mode / bitrate / TWAI state
- RX counters
- error counters
- alert counters and the last alert mask
- last frame
- decoded basic joystick state
- decoded extended joystick state
- recent frame history

## Logs You Will See

### Sniffer logs
- `RX EXT ID=0x0CFDD635 RTR=0 DLC=8 DATA=[..]`

### Decoded logs
- `DEC BASIC PGN=0x0FDD6 src=0x35(Center) ...`
- `DEC EXT PGN=0x0FDD7 src=0x35(Center) ...`

### Startup physical-layer logs
- `RX edge probe: sampling GPIO16 for 15000 ms before starting TWAI ...`
- `RX edge probe result: edges=... on GPIO16 over 15000 ms`

These were crucial during bring-up because they showed whether the Waveshare CAN transceiver RXD pin was changing at all before protocol decode entered the picture.

## Proven Bench Context

The major bring-up breakthrough was wiring, not decode:
- tested RH112 units only began producing meaningful bus activity after power polarity was corrected
- working mapping on the tested cable colors:
  - `red` -> `+24VDC`
  - `black` -> `0V`
  - `blue` -> `CANH`
  - `white` -> `CANL`

Observed hardware labels that matched the documented source-address family:
- `ADDRESS 34H`
- `CAN ADDRESS J35H`

Once power was corrected, the startup RX-edge probe changed from `0` transitions to very large transition counts, which confirmed that the board and transceiver path were finally seeing real traffic.

## UI Note

The UI now has:
- a `Diagnostics` page for all counters/raw/decode state
- a `Graph` page that plots the last `30 seconds` of `Base Y`

`Base Y` is currently the most useful proven decoded live signal for this project.

## Termination Note
`docs/hardware/TS CAN info.pdf` does not mention `120 ohm` termination or any onboard termination detail. That means termination still needs to be confirmed from the actual device wiring, transceiver board, or vendor electrical documentation.

## Board-Specific Note
The Waveshare board documentation still matters for the ESP32-side wiring:
- `GPIO15`: `CANTX`
- `GPIO16`: `CANRX`

The joystick PDF changes what the firmware should decode on that bus, not the board pin mapping.
