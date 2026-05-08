# Arduino OLED IMU Projects

A collection of Arduino Nano projects built around the same small handheld hardware setup:

- Arduino Nano (`ATmega328P`)
- 128x64 I2C OLED display
- MPU-6500-family IMU motion sensor
- single push button
- passive buzzer

The projects use tilt input from the IMU, simple OLED graphics, and PlatformIO for repeatable builds and uploads.

## Projects

### Arduino Tilt Game Console

Folder: [`arduino-tilt-game-console`](arduino-tilt-game-console/)

The original two-game tilt console. It includes:

- `Tilt Maze`
- `Coin Collector`
- OLED menu
- IMU tilt control
- button navigation
- buzzer feedback
- EEPROM high score saving for Coin Collector

See the project README:

[`arduino-tilt-game-console/README.md`](arduino-tilt-game-console/README.md)

### Arduino Tilt Arcade Console

Folder: [`arduino-tilt-arcade-console`](arduino-tilt-arcade-console/)

A newer two-game arcade console using the same hardware. It includes:

- `Tilt Snake`
- `Space Dodger`
- per-game tilt calibration
- saved calibration values
- saved high scores
- pause mode
- countdowns
- idle title screen
- improved game-over screens
- known-good diagnostic/test sketches

See the project README:

[`arduino-tilt-arcade-console/README.md`](arduino-tilt-arcade-console/README.md)

## Repository Layout

```text
arduino-oled-imu-projects/
  README.md
  .gitignore
  arduino-tilt-game-console/
    README.md
    platformio.ini
    src/
    docs/
  arduino-tilt-arcade-console/
    README.md
    platformio.ini
    src/
    test/
```

## Hardware Wiring

Both finished projects use the same wiring.

### OLED

- `GND -> GND`
- `VCC -> 5V`
- `SDA -> A4`
- `SCL -> A5`

### IMU

- `GND -> GND`
- `VCC -> 5V`
- `SDA -> A4`
- `SCL -> A5`

### Push Button

- one leg -> `D2`
- other leg -> `GND`
- code uses `INPUT_PULLUP`, so pressed = `LOW`

### Passive Buzzer

- positive -> `D9`
- negative -> `GND`

## Building A Project

Open the project folder you want to build, then run PlatformIO from inside that folder.

Example:

```bash
cd arduino-tilt-arcade-console
pio run
```

## Uploading

For newer Arduino Nano boards or clones using the new bootloader:

```bash
pio run -e nanoatmega328new -t upload --upload-port COM3
```

If upload fails because your Nano uses the old bootloader:

```bash
pio run -e nanoatmega328old -t upload --upload-port COM3
```

If the board appears on a different port, check connected devices:

```bash
pio device list
```

## Notes

- The unfinished fluid simulator experiment is intentionally ignored for now.
- Each finished project remains its own PlatformIO project with its own `platformio.ini`.
- Keeping projects separate avoids breaking working sketches while still grouping them in one GitHub repository.
