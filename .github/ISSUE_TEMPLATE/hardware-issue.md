---
name: MicroPilot hardware or PFD issue
about: Report a ForgeUI MicroPilot hardware, control, build, or flight-display problem
title: "[MicroPilot] "
labels: hardware
assignees: ""
---

## Hardware

- ESP32 model and board:
- ST7789 display/controller:
- PCB or module marking:

## Display wiring

| Display signal | Connected pin / voltage |
| --- | --- |
| GND | |
| VCC | |
| SCL / SCLK | |
| SDA / MOSI | |
| RES / RST | |
| DC | |
| CS | |
| BLK | |

State whether MISO is connected. For the tested 1.54-inch square module, BLK is wired to 3.3V.

## Joystick wiring and calibration

| Joystick signal | Connected pin / voltage |
| --- | --- |
| SW | |
| VRy | |
| VRx | |
| Supply | |
| GND | |

- Observed centre calibration values (`Joystick centre X=... Y=...`):
- Bank response:
- Pitch response:

## MicroPilot PFD behavior

- Artificial-horizon behavior:
- Pitch-ladder behavior:
- IAS behavior:
- Altitude behavior:
- Vertical-speed behavior:
- Heading behavior:
- AP LEVEL toggle behavior:
- Autopilot levelling behavior:
- Warning annunciations (bank/pitch):

## Software and results

- PlatformIO version:
- `espressif32` platform version:
- Arduino_GFX version:
- Build: PASS / FAIL
- Flash: PASS / FAIL

## Evidence

Attach a physical photo of the board, display, joystick, and wiring. Include short relevant build, upload, or serial logs; remove unrelated output and secrets.
