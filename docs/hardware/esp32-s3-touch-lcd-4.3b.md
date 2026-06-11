# Waveshare ESP32-S3-Touch-LCD-4.3B Hardware Reference

Last verified: 2026-02-22

## Board Identification

- Product family: `ESP32-S3-Touch-LCD-4.3B`
- Amazon ASIN in your link: `B0DCNSRT31` (`ESP32-S3-Touch-LCD-4.3B-BOX`)
- Module: `ESP32-S3-WROOM-1-N16R8`

## Core MCU + Memory

- CPU: Xtensa LX7 dual-core, up to 240 MHz
- Wireless: 2.4 GHz Wi-Fi (802.11 b/g/n) + Bluetooth 5 LE
- SRAM/ROM: 512 KB SRAM, 384 KB ROM
- External memory: 16 MB Flash + 8 MB PSRAM

## Display + Touch

- LCD size: 4.3"
- Resolution: 800 x 480
- Interface: RGB
- Panel type: IPS
- Brightness: 550 cd/m^2
- Viewing angle: 160 deg
- Touch: Capacitive, 5-point, interrupt-capable
- Touch interface: I2C
- Touch panel: Toughened glass

## Power

- USB power/programming: USB Type-C 5V
- External power input: 7-36V DC (for 4.3B/4.3B-BOX carrier)
- Typical board power consumption: 5V @ 450 mA
- Battery support: 3.7V single-cell Li-ion (MX1.25 connector)

## Onboard Interfaces (useful for HMI/display projects)

- CAN 2.0
- RS485
- I2C
- USB
- TF (microSD) slot
- Isolated digital inputs/outputs (5-36V domain)
- RTC (PCF85063)
- CH422G IO expander (used for control lines such as touch reset, display backlight enable, TF CS)

## Communications GPIO Mapping

### CAN

- CAN TX: GPIO15
- CAN RX: GPIO16
- Board uses an onboard CAN transceiver; see Waveshare schematic/resources for the exact transceiver implementation on your revision.

### RS485

- RXD: GPIO43
- TXD: GPIO44
- Check official board docs for any RS485 direction-control details on your exact hardware revision

### TF card

- TF_CLK: GPIO11
- TF_MOSI: GPIO12
- TF_MISO: GPIO13
- TF_CS: CH422G `EXIO4`

## Critical GPIO Mapping For Display Stack

### LCD RGB signals

- VSYNC: GPIO3
- HSYNC: GPIO46
- DE: GPIO5
- PCLK: GPIO7
- Data bus:
  - R: GPIO1, GPIO2, GPIO40, GPIO41, GPIO42
  - G: GPIO0, GPIO21, GPIO39, GPIO45, GPIO47, GPIO48
  - B: GPIO10, GPIO14, GPIO17, GPIO18, GPIO38
- Backlight enable: CH422G `EXIO2` (`DISP`)

### Touch (GT911 over I2C)

- TP_IRQ: GPIO4
- TP_SDA: GPIO8
- TP_SCL: GPIO9
- TP_RST: CH422G `EXIO1`

### Shared I2C bus

- SDA: GPIO8
- SCL: GPIO9
- Used by touch controller, IO expander, RTC, and external I2C header

## Development Notes

- Primary frameworks: ESP-IDF and Arduino
- Board routes many GPIOs to the RGB panel; plan peripheral use around remaining free pins
- TF CS is controlled by CH422G `EXIO4` (not a direct ESP32 GPIO)
- Built-in I2C addresses are already occupied by onboard devices; avoid collisions when adding sensors
- Official board docs map CAN to `GPIO15/16`; do not assume `GPIO12/13` for CAN on this target

## Project CAN Note

- For this repo, the external device CAN format is documented in `docs/hardware/TS CAN info.pdf`
- That document points to J1939 extended CAN traffic at `250 kbps`
- The ESP32 side still uses the Waveshare onboard CAN path on `GPIO15/16`
- Waveshare docs note selectable CAN and RS485 `120 ohm` terminal resistors on the board, so bench tests should confirm whether the correct termination is enabled for the wiring setup
- The joystick PDF does not document physical termination requirements, so final termination still has to be confirmed from the actual switch wiring and bus topology
- A pre-TWAI raw RX probe on `GPIO16` proved very useful during bench debugging because it showed whether the Waveshare transceiver was seeing any line activity before the CAN controller even started
- Do not probe `GPIO19/20` as alternate CAN pins on ESP32-S3 for this board; those are USB D-/D+ and can destabilize USB serial/JTAG during diagnostics

## Bench Wiring Context

Working RH112 color mapping observed during bring-up:
- `red` -> `+24VDC`
- `black` -> `0V / GND`
- `blue` -> `CANH`
- `white` -> `CANL`

The original assumption had `red` and `black` reversed. With reversed power, the ESP32 app still ran but no meaningful CAN traffic reached the Waveshare transceiver. After correcting power polarity, the raw RX probe on `GPIO16` immediately showed large transition counts, confirming that the board-side CAN path was healthy.

For bench use:
- USB-C power/programming can stay connected,
- the RH112 can be powered from external `24VDC`,
- the supply return and board ground must share a reference,
- the CAN pair should stay on the dedicated onboard CAN terminal path, not the RS485 path.

## Sources

- Waveshare wiki (official): https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3B
- Waveshare product page (official): https://www.waveshare.com/esp32-s3-touch-lcd-4.3b.htm
- Waveshare docs mirror (official): https://docs.waveshare.net/ESP32-S3-Touch-LCD-4.3B/
- Amazon listing you provided: https://www.amazon.com/Waveshare-Capacitive-Development-Resolution-Dual-Core/dp/B0DCNSRT31
