# UI Module

This module renders the on-device user interface for live CAN monitoring.

## Responsibilities

- Initialize LVGL application screens
- Display decoded signal values from the CAN module
- Display diagnostics such as RX counters and alert/error state
- Provide live visual views (including signal graphing where enabled)

## Files

- `ui.c`: UI creation, update logic, and data presentation
- `ui.h`: UI-facing types and update interfaces shared with CAN code

The UI is intentionally focused on operational visibility: see live readings quickly and confirm communication health at the same time.
