# canBUS CAN Rotary Hall Display

This repository contains firmware for an ESP32-S3 touchscreen display that shows live CAN bus readings from a CAN Rotary Hall Switch.

The current implementation targets J1939-style CAN traffic from the documented joystick/switch messages at `250 kbps`, then presents decoded values and diagnostics on an LVGL-based UI.

## What This Project Does

- Brings up the Waveshare `ESP32-S3-Touch-LCD-4.3B` display and touch hardware
- Receives CAN traffic through the onboard transceiver (`GPIO15` TX, `GPIO16` RX)
- Decodes target messages (`PGN 0xFDD6` and `PGN 0xFDD7`)
- Displays live readings (axis and button state) and CAN diagnostics
- Tracks signal freshness and communication health for troubleshooting

## High-Level Process

1. The app starts and initializes the display/touch UI.
2. The CAN interface is configured for `250 kbps` and started.
3. A receive task listens for CAN alerts and reads incoming frames.
4. Frames are classified, target PGNs are decoded, and counters are updated.
5. The UI is refreshed with live values, recent frame info, and system health status.

This flow keeps the project focused on "read, decode, and visualize" rather than control outputs.

## Repository Guide

- `main/`: application source code (startup, CAN handling, UI, board setup)
- `docs/`: supporting documentation and hardware notes
- `can.md`: detailed CAN implementation notes and diagnostics behavior
- `canbusINFO.txt`: quick reference for expected CAN message formats
- `flash.ps1`: helper script to flash firmware
- `monitor-safe.ps1`: safer serial monitor attach flow used during bench testing

## Core Runtime Modes

The firmware can run in:

- Normal mode (default): receive/decode real bus traffic
- Diagnostic-focused variants (for bench/bring-up):
  - listen-only
  - no-ack/self-test
  - minimal app mode without UI

Most users should start with default normal mode and only use diagnostic modes when troubleshooting bus behavior.

## Getting Started

1. Build and flash with ESP-IDF tools (or project scripts such as `flash.ps1`).
2. Connect to serial monitor (`monitor-safe.ps1` is recommended on this setup).
3. Verify CAN traffic is present and readings are updating in the UI.

For deeper technical detail, start with `can.md` and `docs/documentation.md`.
