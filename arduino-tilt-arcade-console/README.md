# Arduino Tilt Arcade Console

A tilt-controlled mini arcade console built with an Arduino Nano, a 128x64 OLED display, an MPU-6500-family motion sensor, a push button, and a buzzer.

The final build includes two playable games:

- `Tilt Snake`
- `Space Dodger`

## Project Overview

This project turns the same breadboard hardware from the Arduino Tilt Game Console into a compact two-game arcade system. The player tilts the console to control each game, uses one button for menu navigation, pause, restart, and calibration, and gets simple audio feedback from a passive buzzer.

The system boots into a menu, shows saved best scores, lets the player choose a game, and runs one of two experiences:

- `Tilt Snake`: guide a snake around the playfield, eat blinking food, grow longer, and avoid walls or your own tail
- `Space Dodger`: steer a small ship left and right while dodging falling asteroids

## Features

- Tilt-based control using an MPU-6500-family motion sensor
- OLED menu with two games
- Single-button short press / long press / very-long press controls
- Per-game tilt calibration
- Saved calibration values in EEPROM
- Saved high scores in EEPROM
- Startup countdown before each game
- Pause and resume during gameplay
- Buzzer feedback for menu moves, selections, scoring, pause, game over, and new best scores
- Idle title screen after the menu is left alone
- Playable `Tilt Snake`
- Playable `Space Dodger`

## Hardware Used

- Arduino Nano (`ATmega328P`)
- 0.96" I2C OLED display (`128x64`, SSD1306-style)
- MPU-6500-family motion sensor module
- Passive buzzer
- Push button
- Breadboard
- Jumper wires
- USB cable or power bank

## Wiring Summary

Use the same wiring as the original tilt game console project.

### OLED

- `GND -> GND`
- `VCC -> 5V`
- `SDA -> A4`
- `SCL -> A5`

### Motion Sensor

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

## Controls

### Menu

- short press: move to the next game
- long press: start the selected game
- very-long press: recalibrate the selected game and save that calibration

### Gameplay

- tilt: control the active game
- short press: pause or resume
- long press: return to the menu

### Game Over

- short press: restart the current game
- long press: return to the menu

## How It Works

At startup, the Arduino:

1. Initializes the OLED, buzzer, button, and I2C bus
2. Loads saved high scores from EEPROM
3. Detects and wakes the IMU
4. Loads saved per-game calibration values from EEPROM, or calibrates once if none exist
5. Opens the arcade menu

During use:

- the menu shows both games and their saved best scores
- a countdown appears before each game starts
- tilting the console controls the snake or ship
- short press during a game pauses and resumes
- game-over screens show score and best score
- new high scores trigger a special beep pattern and are saved automatically

## Games

### Tilt Snake

The player tilts the console to steer the snake around a chunky grid. The food blinks between filled and outlined states so it is easier to spot. Eating food increases the score, grows the snake, and gradually speeds up the game. Hitting the wall or the snake's own body ends the round.

### Space Dodger

The player tilts left and right to steer a small ship across the bottom of the OLED. Asteroids fall from the top of the screen and get harder over time. The spawner tries to avoid unfair asteroid clusters, and larger asteroids include small crater pixels.

## Calibration

Each game has its own saved neutral tilt position.

To recalibrate:

1. Select the game in the menu.
2. Hold the console in the position you want to use for that game.
3. Very-long press the button.
4. Wait for the recalibration message to finish.

The calibration is saved in EEPROM, so it survives unplugging and reconnecting the Arduino.

## Software / Libraries

This repository is set up as a PlatformIO project.

### PlatformIO dependencies

- `adafruit/Adafruit SSD1306`
- `adafruit/Adafruit GFX Library`

### Built-in Arduino / framework libraries used

- `EEPROM`
- `Wire`

## Getting Started

### Requirements

- Visual Studio Code
- PlatformIO IDE extension
- Arduino Nano connected by USB

### Build

```bash
pio run
```

### Upload

For the newer Nano bootloader profile:

```bash
pio run -e nanoatmega328new -t upload --upload-port COM3
```

If upload fails because your Nano uses the old bootloader, try:

```bash
pio run -e nanoatmega328old -t upload --upload-port COM3
```

### Serial Monitor

```bash
pio device monitor
```

## Project Structure

```text
src/main.cpp                         Main arcade logic, menu, games, IMU, EEPROM, rendering, sounds
platformio.ini                       PlatformIO configuration and dependencies
README.md                            Project overview and setup instructions
test/oled-imu-diagnostic/            Standalone OLED and IMU diagnostic sketch
test/snake-only/                     Standalone Tilt Snake test sketch
test/dodger-only/                    Standalone Space Dodger test sketch
test/combined-minimal/               Earlier stable combined test sketch
```

## Notes / Limitations

- The Arduino Nano has limited RAM and flash, so the project avoids heavy graphics, large arrays, and extra libraries.
- The OLED buffer already uses a large part of the Nano's available RAM.
- Calibration depends on how the console is held during setup or recalibration.
- The saved calibration and high scores use EEPROM, so they remain after power is removed.
- Test sketches are kept in the `test/` folder as known-good fallbacks while developing.

## Future Improvements

- Add a simple settings screen
- Add difficulty choices
- Add more polished sound patterns
- Add screenshots or photos of the final build
- Move old test sketches into an archive folder once the final version is fully settled
