## Keyboard expansion firmware

This firmware runs on the Fri3D camp 2024 communicator expansion board, which is powered by [LANA_TNY](https://phyx.be/LANA_TNY).

The firmware outputs [HID report packets](https://files.microscan.com/helpfiles/ms4_help_file/ms-4_help-02-46.html) (8 bytes) on USB, I2C (address ```0x38```) and UART.

The first byte indicates the modifier keys that have been pressed:

| Bit | Modifier Key |
|-|-|
| 0 | LEFT CTRL |
| 1 | LEFT SHIFT |
| 2 | LEFT ALT |
| 3 | LEFT GUI |
| 4 | RIGHT CTRL |
| 5 | RIGHT SHIFT |
| 6 | RIGHT ALT |
| 7 | RIGHT GUI |

The second byte is reserved, the remaining 6 bytes can contain a [HID keycode](https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2).

## Building

First install the RISCV GCC compiler:

```
https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/tag/v12.2.0-3/
```

Make a build directory:
```
mkdir build
```

And call the makefile:
```
make build -C build -f ../config/makefile TOOLPREFIX=/path/to/gnu_riscv_xpack_toolchain_12.2.0_3_64b/bin/riscv-none-elf-
```

## Flashing

The application is can be flashed using ```wchisp``` over USB, or OpenOCD using WCHLink:

```
wchisp flash build/application.hex
```

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