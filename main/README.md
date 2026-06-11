# Main Application Source

This folder contains the firmware application logic for the CAN Rotary Hall live display.

## Structure

- `main.c`: startup task and top-level application flow
- `board/`: display/touch board initialization
- `canbus/`: CAN driver startup, frame receive loop, decode, and diagnostics
- `ui/`: LVGL user interface and on-screen telemetry presentation

## Runtime Sequence

1. Start app task.
2. Initialize UI and board peripherals.
3. Start CAN subsystem.
4. Process incoming frames and publish decoded state to UI.

This keeps the project modular: hardware bring-up, CAN processing, and visualization are separated by folder.
