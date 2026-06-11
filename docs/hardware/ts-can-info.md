# TS CAN Info Summary

Source document: `docs/hardware/TS CAN info.pdf`

## What It Says
- The device outputs CAN using a J1939-style message layout.
- Bus speed is `250 kbps`.
- The relevant messages are:
  - basic joystick PGN `0xFDD6`
  - extended joystick PGN `0xFDD7`
- The resulting CAN identifiers are:
  - `0x0CFDD6xx`
  - `0x0CFDD7xx`
- The low byte is the joystick source address.

## Source Address Mapping
- `0x33`: Right
- `0x34`: Left
- `0x35`: Center
- `0x36`: Auxiliary

Bench labels seen on real hardware match this mapping:
- `ADDRESS 34H`
- `CAN ADDRESS J35H`

## Basic Message Contents
- Base X axis status and 10-bit position
- Base Y axis status and 10-bit position
- Grip button states for buttons `1..12`

## Extended Message Contents
- Grip X axis status and 10-bit position
- Grip Y axis status and 10-bit position
- Theta axis status and 10-bit position

## Two-Bit Status Encoding
- `00`: inactive / not pressed
- `01`: active / pressed
- `10`: error indicator
- `11`: not available

## Axis Position Packing
The PDF shows each axis as a 10-bit value:
- bits `1..6` of the first byte are status flags,
- bits `7..8` of the first byte are the two low bits of the position,
- the next byte contains the upper eight bits.

Decode formula used in code:
```c
position = (next_byte << 2) | ((current_byte >> 6) & 0x3);
```

## 120 Ohm Termination
The PDF does not mention:
- `120 ohm`
- `termination`
- onboard resistor values

So it is useful for protocol decode, but not for confirming physical CAN bus termination requirements.

## Bench Reality Notes

The PDF turned out to be directionally correct for decode, but not sufficient for bring-up by itself.

Important bench findings:
- the sensor family did not start talking until the supply polarity was corrected,
- the working wire mapping on the tested RH112 units was:
  - `red` -> `+24VDC`
  - `black` -> `0V`
  - `blue` -> `CANH`
  - `white` -> `CANL`
- before that power correction, the ESP32-side raw RX probe saw no meaningful bus transitions,
- after that correction, the Waveshare transceiver RXD probe on `GPIO16` began showing heavy activity.

Current practical takeaway:
- this PDF is still the best source for payload layout and source-address meaning,
- but final field wiring should be validated from the actual cable colors and live bus behavior, not assumed from the PDF alone.
