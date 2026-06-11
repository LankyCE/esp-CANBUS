# CANBUS Module

This module manages CAN communication, decoding, and runtime diagnostics.

## Responsibilities

- Configure and start TWAI/CAN at the project bitrate (`250 kbps`)
- Receive and classify incoming CAN frames
- Decode target J1939-style messages (`PGN 0xFDD6`, `PGN 0xFDD7`)
- Track counters, alerts, stale-data timing, and last-frame state
- Publish decoded and diagnostic data to the UI layer

## Files

- `canbus.c`: runtime CAN logic, receive task, decode, and diagnostics
- `canbus.h`: module interface used by app startup
- `canbus_config.h`: mode flags and operational configuration

## Process Overview

1. Start CAN controller with configured mode and pins.
2. Wait for CAN alerts and read available frames.
3. Decode relevant payloads and update health counters.
4. Push current state to UI for live display.

This module is the data pipeline from raw CAN traffic to usable on-screen readings.
