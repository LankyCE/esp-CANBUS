# Hardware Notes

This folder contains hardware-facing references used by the CAN display project.

## Contents

- `esp32-s3-touch-lcd-4.3b.md`: Waveshare board integration notes
- `ts-can-info.md`: summarized CAN message format for the target switch/joystick traffic
- `TS CAN info.pdf`: source reference document for payload and PGN layout

## Purpose

These files support two practical needs:

- confirm board-level wiring/pin assumptions
- confirm expected CAN frame format and source addresses

They are reference material for bring-up and validation, while live behavior is implemented in `main/canbus`.
