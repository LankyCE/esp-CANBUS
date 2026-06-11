# Board Module

This module handles board-level hardware initialization for the display platform.

## Responsibilities

- Initialize display and backlight hardware
- Initialize touch controller path used by the UI
- Apply board-specific pin and panel configuration

## Files

- `board.c`: board bring-up implementation
- `board.h`: public board init interface
- `board_config.h`: board constants and configuration values

This layer focuses on device setup so higher modules can use display/touch services without hardware-specific logic in application code.
