## Keyboard expansion firmware

This firmware runs on the Fri3D camp 2024 communicator expansion board, which is powered by [LANA_TNY](https://phyx.be/LANA_TNY).

The firmware outputs [HID report packets](https://files.microscan.com/helpfiles/ms4_help_file/ms-4_help-02-46.html) (8 bytes) on USB and UART.

The first byte indicates the modifier keys that have been pressed:

| Bit | Modifier Key |
|-|-|
| 7 | RIGHT GUI |
| 6 | RIGHT ALT |
| 5 | RIGHT SHIFT |
| 4 | RIGHT CTRL |
| 3 | LEFT GUI |
| 2 | LEFT ALT |
| 1 | LEFT SHIFT |
| 0 | LEFT CTRL |

The second byte is reserved, the remaining 6 bytes can contain a [HID keycode](https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2).

### I2C

The Fri3D badge 2024 and 2026 can communicate with the communicator through I2C (address ```0x38```). The following registers can be used to interface/control with the communicator:

| Address (hex) | Name | Access | Bytes | description |
|-|-|-|-|-|
| 0x00 | Version number | R | 3 | Reports the firmware version number |
| 0x03 | current HID report packet | R | 8 | An 8-byte HID report packet (see above) |
| 0x0b | Configuration | R | 1 | a 1-byte configuration register (see below) |
| 0x0c | Backlight | R/W | 2 | Keyboard backlight intensity (0-100) |
| 0x0e | LANA RGB LED | R/W | 3 | LANA module RGB LED value (R,G,B) (2) |
| 0x11 | CAPS Lock indicator | R/W | 1 | CAPS lock LED indicator (2) |
2. not available on the [Fri3D communicator 2026]()

The configuration is a 1-byte value with the following encoding:
| Bit | Name |
|-|-|
| \[7:2\] | reserved |
| 1 | reboot to bootloader |
| 0 | enable interrupt mode |

## Building

Use [platformio](https://platformio.org) to build this project. If you use the command line, build using:

```
pio run
```

To flash your device, unplug the USB cable, press and hold the reset button while plugging in the USB cable again. Then upload using the command:
```
pio run -t upload
```
It will use [wchisp](https://github.com/Community-PIO-CH32V/tool-wchisp) to flash the binary to the CH32V203 chip.

## Usage

The keyboard presents itself as a HID input device.
The ```Fn``` key can be used to trigger special functions:
 * ```Fn+F1```: Put LANA LED to red
 * ```Fn+F2```: Put LANA LED to orange
 * ```Fn+F3```: Put LANA LED to yellow
 * ```Fn+F4```: Put LANA LED to green
 * ```Fn+F5```: Put LANA LED to blue
 * ```Fn+F6```: Put LANA LED to purple
 * ```Fn+Windows```: Put LANA LED off
 * ```Fn+Backspace```: Delete
 * ```Fn+Up```: Page Up
 * ```Fn+Down```: Page Down
 * ```Fn+Left```: Home
 * ```Fn+Right```: End
 * ```Fn+Spacebar```: Toggle keyboard backlight
 * ```Fn+Right Shift```: Toggle Caps Lock
