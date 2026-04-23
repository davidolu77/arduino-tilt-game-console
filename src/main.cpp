#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {

constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_RESET = 255;
constexpr uint8_t OLED_ADDRESS = 0x3C;

constexpr uint8_t BUTTON_PIN = 2;
constexpr uint8_t BUZZER_PIN = 9;

constexpr uint8_t IMU_ADDR_PRIMARY = 0x68;
constexpr uint8_t IMU_ADDR_SECONDARY = 0x69;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x75;

constexpr int BALL_RADIUS = 2;
constexpr float BALL_START_X = SCREEN_WIDTH / 2.0f;
constexpr float BALL_START_Y = SCREEN_HEIGHT / 2.0f;
constexpr float TILT_DEADZONE = 0.04f;
constexpr float TILT_SPEED = 4.8f;
constexpr float ACCEL_LSB_PER_G = 16384.0f;
constexpr float SCREEN_X_SIGN = -1.0f;
constexpr float SCREEN_Y_SIGN = -1.0f;
constexpr uint8_t CALIBRATION_SAMPLES = 40;
constexpr uint8_t EEPROM_MAGIC = 0x42;
constexpr int EEPROM_MAGIC_ADDR = 0;
constexpr int EEPROM_HIGHSCORE_ADDR = 1;

constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;
constexpr unsigned long LONG_PRESS_MS = 700;
constexpr unsigned long FRAME_DELAY_MS = 20;
constexpr unsigned long DEBUG_PRINT_MS = 200;
constexpr unsigned long GAME_OVER_HOLD_MS = 350;
constexpr int16_t PLAYFIELD_TOP = 11;
constexpr int16_t COIN_COLLECTOR_PLAYFIELD_TOP = 19;
constexpr int16_t PLAYFIELD_LEFT = 0;
constexpr int16_t PLAYFIELD_RIGHT = SCREEN_WIDTH - 1;
constexpr int16_t PLAYFIELD_BOTTOM = SCREEN_HEIGHT - 1;
constexpr int16_t TILT_MAZE_FOOTER_TOP = 56;
constexpr unsigned long COIN_COLLECTOR_TIME_LIMIT_MS = 20000;
constexpr uint8_t COIN_COLLECTOR_BASE_TARGET = 8;
constexpr int COIN_RADIUS = 2;

enum class AppState {
  Menu,
  Playing,
  GameOver,
};

enum class ButtonEvent {
  None,
  ShortPress,
  LongPress,
};

enum class PlayResult {
  Ongoing,
  Won,
  Lost,
};

struct ButtonTracker {
  bool stableState = HIGH;
  bool previousRawState = HIGH;
  unsigned long lastDebounceMs = 0;
  unsigned long pressStartedMs = 0;
};

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

constexpr size_t GAME_COUNT = 2;
const char *const GAME_NAMES[GAME_COUNT] = {
    "Tilt Maze",
    "Coin Collector",
};
constexpr size_t TILT_MAZE_INDEX = 0;
constexpr size_t COIN_COLLECTOR_INDEX = 1;

constexpr Rect TILT_MAZE_WALLS[] = {
    // Slightly tighter layout for a bit more challenge while staying playable.
    {22, 20, 5, 24},
    {22, 44, 26, 5},
    {48, 20, 5, 16},
    {48, 20, 24, 5},
    {72, 20, 5, 24},
    {72, 44, 24, 5},
    {96, 28, 5, 21},
    {34, 30, 14, 4},
    {60, 36, 12, 4},
    {84, 30, 12, 4},
};
constexpr Rect TILT_MAZE_GOAL = {108, 28, 14, 14};
constexpr float TILT_MAZE_START_X = 7.0f;
constexpr float TILT_MAZE_START_Y = 22.0f;

float ballX = BALL_START_X;
float ballY = BALL_START_Y;
uint8_t imuAddress = 0;
uint8_t imuWhoAmI = 0;
float neutralAccelXg = 0.0f;
float neutralAccelYg = 0.0f;
float neutralAccelZg = 0.0f;
unsigned long lastSerialPrintMs = 0;
unsigned long gameOverAtMs = 0;
bool lastRoundWon = false;
uint8_t highScore = 0;
uint8_t currentScore = 0;
uint8_t currentTarget = COIN_COLLECTOR_BASE_TARGET;
unsigned long roundEndsAtMs = 0;
float coinX = 0.0f;
float coinY = 0.0f;

AppState appState = AppState::Menu;
ButtonTracker buttonTracker;
size_t selectedGameIndex = 0;
size_t activeGameIndex = 0;

void playTone(uint16_t frequency, uint16_t durationMs) {
  tone(BUZZER_PIN, frequency, durationMs);
}

void playStartupBeep() {
  playTone(1200, 120);
  delay(150);
  playTone(1600, 120);
  delay(150);
  noTone(BUZZER_PIN);
}

void playMenuMoveBeep() {
  playTone(1400, 50);
}

void playSelectBeep() {
  playTone(1800, 80);
}

void playGameOverBeep() {
  playTone(700, 120);
  delay(140);
  playTone(500, 170);
}

void playCoinBeep() {
  playTone(1700, 45);
  delay(55);
  playTone(2200, 55);
}

void showFatalError(const __FlashStringHelper *message) {
  Serial.println(message);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Startup failed"));
  display.println();
  display.println(message);
  display.display();

  while (true) {
    delay(100);
  }
}

void initButtonAndBuzzer() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
}

void loadHighScore() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC) {
    EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    EEPROM.update(EEPROM_HIGHSCORE_ADDR, 0);
  }

  highScore = EEPROM.read(EEPROM_HIGHSCORE_ADDR);
}

void saveHighScore(uint8_t score) {
  highScore = score;
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
  EEPROM.update(EEPROM_HIGHSCORE_ADDR, highScore);
}

void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    while (true) {
      delay(100);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 12);
  display.println(F("Tilt Game"));
  display.setCursor(22, 34);
  display.println(F("Console"));
  display.display();
}

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool writeRegister(uint8_t deviceAddress, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(deviceAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t deviceAddress, uint8_t startReg, uint8_t *buffer, size_t length) {
  Wire.beginTransmission(deviceAddress);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = Wire.requestFrom(static_cast<int>(deviceAddress), static_cast<int>(length));
  if (received != length) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    buffer[i] = Wire.read();
  }

  return true;
}

int16_t joinBytes(uint8_t highByte, uint8_t lowByte) {
  return static_cast<int16_t>((static_cast<uint16_t>(highByte) << 8) | lowByte);
}

void printI2cScan() {
  Serial.println(F("Scanning I2C bus..."));
  for (uint8_t address = 1; address < 127; ++address) {
    if (i2cDevicePresent(address)) {
      Serial.print(F("Found I2C device at 0x"));
      if (address < 16) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
    }
  }
}

bool detectImu() {
  const uint8_t candidateAddresses[] = {IMU_ADDR_PRIMARY, IMU_ADDR_SECONDARY};

  for (uint8_t address : candidateAddresses) {
    if (!i2cDevicePresent(address)) {
      continue;
    }

    uint8_t whoAmI = 0;
    if (readRegisters(address, REG_WHO_AM_I, &whoAmI, 1)) {
      imuAddress = address;
      imuWhoAmI = whoAmI;
      return true;
    }
  }

  return false;
}

void printImuIdentity() {
  Serial.print(F("IMU found at 0x"));
  if (imuAddress < 16) {
    Serial.print('0');
  }
  Serial.print(imuAddress, HEX);
  Serial.print(F(", WHO_AM_I = 0x"));
  if (imuWhoAmI < 16) {
    Serial.print('0');
  }
  Serial.println(imuWhoAmI, HEX);

  if (imuWhoAmI == 0x70) {
    Serial.println(F("Looks like an MPU-6500 family device."));
  } else if (imuWhoAmI == 0x71) {
    Serial.println(F("Looks like an MPU-9250 family device."));
  } else if (imuWhoAmI == 0x73) {
    Serial.println(F("Looks like an MPU-9255-style device."));
  } else {
    Serial.println(F("Unknown WHO_AM_I value, but register reads are working."));
  }
}

void showCalibrationMessage() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Hold console"));
  display.println(F("in neutral"));
  display.println(F("position..."));
  display.display();
}

void initImu() {
  printI2cScan();

  if (!detectImu()) {
    showFatalError(F("IMU not found"));
  }

  printImuIdentity();

  if (!writeRegister(imuAddress, REG_PWR_MGMT_1, 0x00)) {
    showFatalError(F("IMU wake failed"));
  }

  delay(100);

  if (!writeRegister(imuAddress, REG_ACCEL_CONFIG, 0x00)) {
    showFatalError(F("Accel config failed"));
  }
}

bool readAccelRaw(int16_t &rawX, int16_t &rawY, int16_t &rawZ) {
  uint8_t buffer[6];
  if (!readRegisters(imuAddress, REG_ACCEL_XOUT_H, buffer, sizeof(buffer))) {
    return false;
  }

  rawX = joinBytes(buffer[0], buffer[1]);
  rawY = joinBytes(buffer[2], buffer[3]);
  rawZ = joinBytes(buffer[4], buffer[5]);
  return true;
}

void showSensorReadError() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Sensor read failed"));
  display.println(F("Check MPU wiring"));
  display.display();
}

void resetBall() {
  if (activeGameIndex == TILT_MAZE_INDEX) {
    ballX = TILT_MAZE_START_X;
    ballY = TILT_MAZE_START_Y;
    return;
  }

  ballX = BALL_START_X;
  ballY = BALL_START_Y;
}

bool circlesOverlap(float ax, float ay, int ar, float bx, float by, int br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float combined = ar + br;
  return (dx * dx + dy * dy) <= (combined * combined);
}

uint8_t targetScoreForNextRound() {
  const uint8_t beatHighScoreTarget = static_cast<uint8_t>(highScore + 1);
  return beatHighScoreTarget > COIN_COLLECTOR_BASE_TARGET ? beatHighScoreTarget : COIN_COLLECTOR_BASE_TARGET;
}

void spawnCoin() {
  const float minX = 10.0f;
  const float maxX = SCREEN_WIDTH - 10.0f;
  const float minY = COIN_COLLECTOR_PLAYFIELD_TOP + 7.0f;
  const float maxY = SCREEN_HEIGHT - 8.0f;

  for (uint8_t attempt = 0; attempt < 40; ++attempt) {
    const float candidateX = random(static_cast<long>(minX), static_cast<long>(maxX));
    const float candidateY = random(static_cast<long>(minY), static_cast<long>(maxY));

    if (!circlesOverlap(ballX, ballY, BALL_RADIUS + 4, candidateX, candidateY, COIN_RADIUS)) {
      coinX = candidateX;
      coinY = candidateY;
      return;
    }
  }

  coinX = SCREEN_WIDTH - 16.0f;
  coinY = SCREEN_HEIGHT / 2.0f;
}

void prepareGameRound() {
  resetBall();

  if (activeGameIndex == COIN_COLLECTOR_INDEX) {
    currentScore = 0;
    currentTarget = targetScoreForNextRound();
    roundEndsAtMs = millis() + COIN_COLLECTOR_TIME_LIMIT_MS;
    spawnCoin();
  }
}

void calibrateNeutralTilt() {
  showCalibrationMessage();
  delay(700);

  long sumX = 0;
  long sumY = 0;
  long sumZ = 0;

  for (uint8_t i = 0; i < CALIBRATION_SAMPLES; ++i) {
    int16_t rawX = 0;
    int16_t rawY = 0;
    int16_t rawZ = 0;

    if (!readAccelRaw(rawX, rawY, rawZ)) {
      showFatalError(F("Calibration failed"));
    }

    sumX += rawX;
    sumY += rawY;
    sumZ += rawZ;
    delay(20);
  }

  neutralAccelXg = (sumX / static_cast<float>(CALIBRATION_SAMPLES)) / ACCEL_LSB_PER_G;
  neutralAccelYg = (sumY / static_cast<float>(CALIBRATION_SAMPLES)) / ACCEL_LSB_PER_G;
  neutralAccelZg = (sumZ / static_cast<float>(CALIBRATION_SAMPLES)) / ACCEL_LSB_PER_G;

  resetBall();

  Serial.print(F("Neutral X(g): "));
  Serial.print(neutralAccelXg, 2);
  Serial.print(F("  Y(g): "));
  Serial.print(neutralAccelYg, 2);
  Serial.print(F("  Z(g): "));
  Serial.println(neutralAccelZg, 2);
}

float applyDeadzone(float value) {
  if (fabs(value) < TILT_DEADZONE) {
    return 0.0f;
  }
  return value;
}

ButtonEvent readButtonEvent() {
  const bool rawState = digitalRead(BUTTON_PIN);

  if (rawState != buttonTracker.previousRawState) {
    buttonTracker.lastDebounceMs = millis();
    buttonTracker.previousRawState = rawState;
  }

  if ((millis() - buttonTracker.lastDebounceMs) < BUTTON_DEBOUNCE_MS) {
    return ButtonEvent::None;
  }

  if (rawState == buttonTracker.stableState) {
    return ButtonEvent::None;
  }

  buttonTracker.stableState = rawState;

  if (buttonTracker.stableState == LOW) {
    buttonTracker.pressStartedMs = millis();
    return ButtonEvent::None;
  }

  const unsigned long heldMs = millis() - buttonTracker.pressStartedMs;
  if (heldMs >= LONG_PRESS_MS) {
    return ButtonEvent::LongPress;
  }

  return ButtonEvent::ShortPress;
}

bool circleTouchesRect(float cx, float cy, int16_t radius, const Rect &rect) {
  const float closestX = constrain(cx, static_cast<float>(rect.x), static_cast<float>(rect.x + rect.w));
  const float closestY = constrain(cy, static_cast<float>(rect.y), static_cast<float>(rect.y + rect.h));
  const float dx = cx - closestX;
  const float dy = cy - closestY;
  return (dx * dx + dy * dy) <= (radius * radius);
}

bool ballTouchesGoal(float cx, float cy) {
  return circleTouchesRect(cx, cy, BALL_RADIUS, TILT_MAZE_GOAL);
}

bool ballTouchesMazeWall(float cx, float cy) {
  for (const Rect &wall : TILT_MAZE_WALLS) {
    if (circleTouchesRect(cx, cy, BALL_RADIUS, wall)) {
      return true;
    }
  }
  return false;
}

PlayResult updateTiltBall() {
  int16_t rawX = 0;
  int16_t rawY = 0;
  int16_t rawZ = 0;

  if (!readAccelRaw(rawX, rawY, rawZ)) {
    showSensorReadError();
    delay(250);
    return PlayResult::Ongoing;
  }

  const float accelXg = rawX / ACCEL_LSB_PER_G;
  const float accelYg = rawY / ACCEL_LSB_PER_G;
  const float accelZg = rawZ / ACCEL_LSB_PER_G;

  const float tiltX = applyDeadzone((accelYg - neutralAccelYg) * SCREEN_X_SIGN);
  const float tiltY = applyDeadzone((accelXg - neutralAccelXg) * SCREEN_Y_SIGN);

  float nextBallX = ballX + tiltX * TILT_SPEED;
  float nextBallY = ballY + tiltY * TILT_SPEED;

  const float minX = (PLAYFIELD_LEFT + 1) + BALL_RADIUS;
  const float maxX = (PLAYFIELD_RIGHT - 1) - BALL_RADIUS;
  const float minY = ((activeGameIndex == COIN_COLLECTOR_INDEX ? COIN_COLLECTOR_PLAYFIELD_TOP : PLAYFIELD_TOP) + 1) + BALL_RADIUS;
  const float maxY = ((activeGameIndex == TILT_MAZE_INDEX ? TILT_MAZE_FOOTER_TOP : PLAYFIELD_BOTTOM) - 1) - BALL_RADIUS;

  const bool hitBoundary = (nextBallX <= minX || nextBallX >= maxX || nextBallY <= minY || nextBallY >= maxY);

  nextBallX = constrain(nextBallX, minX, maxX);
  nextBallY = constrain(nextBallY, minY, maxY);

  if (activeGameIndex == TILT_MAZE_INDEX) {
    if (!ballTouchesMazeWall(nextBallX, ballY)) {
      ballX = nextBallX;
    }

    if (!ballTouchesMazeWall(ballX, nextBallY)) {
      ballY = nextBallY;
    }

    if (ballTouchesGoal(ballX, ballY)) {
      return PlayResult::Won;
    }
  } else if (activeGameIndex == COIN_COLLECTOR_INDEX) {
    ballX = nextBallX;
    ballY = nextBallY;

    if (circlesOverlap(ballX, ballY, BALL_RADIUS, coinX, coinY, COIN_RADIUS + 1)) {
      ++currentScore;
      playCoinBeep();

      if (currentScore > highScore) {
        saveHighScore(currentScore);
      }

      if (currentScore >= currentTarget) {
        return PlayResult::Won;
      }

      spawnCoin();
    }

    if (millis() >= roundEndsAtMs) {
      return PlayResult::Lost;
    }
  } else {
    ballX = nextBallX;
    ballY = nextBallY;

    if (hitBoundary) {
      return PlayResult::Lost;
    }
  }

  if (millis() - lastSerialPrintMs >= DEBUG_PRINT_MS) {
    lastSerialPrintMs = millis();
    Serial.print(F("Accel X(g): "));
    Serial.print(accelXg, 2);
    Serial.print(F("  Y(g): "));
    Serial.print(accelYg, 2);
    Serial.print(F("  Z(g): "));
    Serial.print(accelZg, 2);
    Serial.print(F("  Tilt X: "));
    Serial.print(tiltX, 2);
    Serial.print(F("  Tilt Y: "));
    Serial.print(tiltY, 2);
    Serial.print(F("  Ball X: "));
    Serial.print(ballX, 1);
    Serial.print(F("  Ball Y: "));
    Serial.println(ballY, 1);
  }

  return PlayResult::Ongoing;
}

void drawMenuScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Tilt Game Console"));
  display.drawLine(0, 9, SCREEN_WIDTH - 1, 9, SSD1306_WHITE);

  for (size_t i = 0; i < GAME_COUNT; ++i) {
    const int16_t rowY = 16 + static_cast<int16_t>(i) * 15;

    if (i == selectedGameIndex) {
      display.fillRect(0, rowY - 1, SCREEN_WIDTH, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(4, rowY);
      display.print('>');
      display.print(' ');
      display.print(GAME_NAMES[i]);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(4, rowY);
      display.print(' ');
      display.print(' ');
      display.print(GAME_NAMES[i]);
    }
  }

  display.setCursor(0, 56);
  display.print(F("Tap:Next Hold:Play"));
  display.display();
}

void drawPlayingScreen() {
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (activeGameIndex == TILT_MAZE_INDEX) {
    display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);
    display.setCursor(3, 1);
    display.print(GAME_NAMES[activeGameIndex]);

    for (const Rect &wall : TILT_MAZE_WALLS) {
      display.fillRect(wall.x, wall.y, wall.w, wall.h, SSD1306_WHITE);
    }

    display.drawRect(TILT_MAZE_GOAL.x, TILT_MAZE_GOAL.y, TILT_MAZE_GOAL.w, TILT_MAZE_GOAL.h, SSD1306_WHITE);
    display.drawLine(0, TILT_MAZE_FOOTER_TOP, SCREEN_WIDTH - 1, TILT_MAZE_FOOTER_TOP, SSD1306_WHITE);
    display.setCursor(2, 57);
    display.print(F("Goal"));
  } else if (activeGameIndex == COIN_COLLECTOR_INDEX) {
    const unsigned long msRemaining = roundEndsAtMs > millis() ? roundEndsAtMs - millis() : 0;
    const uint8_t secondsRemaining = static_cast<uint8_t>(msRemaining / 1000);

    display.setCursor(2, 1);
    display.print(F("Coin Collector"));
    display.drawLine(0, 9, SCREEN_WIDTH - 1, 9, SSD1306_WHITE);

    display.setCursor(2, 11);
    display.print(F("S"));
    display.print(currentScore);
    display.print(F("/"));
    display.print(currentTarget);
    display.setCursor(40, 11);
    display.print(F("H"));
    display.print(highScore);
    display.setCursor(72, 11);
    display.print(F("T"));
    if (secondsRemaining < 10) {
      display.print('0');
    }
    display.print(secondsRemaining);
    display.drawLine(0, COIN_COLLECTOR_PLAYFIELD_TOP, SCREEN_WIDTH - 1, COIN_COLLECTOR_PLAYFIELD_TOP, SSD1306_WHITE);

    display.drawCircle(static_cast<int16_t>(coinX), static_cast<int16_t>(coinY), COIN_RADIUS + 1, SSD1306_WHITE);
    display.fillCircle(static_cast<int16_t>(coinX), static_cast<int16_t>(coinY), COIN_RADIUS, SSD1306_WHITE);
  } else {
    display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);
    display.setCursor(3, 1);
    display.print(GAME_NAMES[activeGameIndex]);
  }

  display.fillCircle(static_cast<int16_t>(ballX),
                     static_cast<int16_t>(ballY),
                     BALL_RADIUS,
                     SSD1306_WHITE);

  if (activeGameIndex == COIN_COLLECTOR_INDEX) {
    display.setCursor(0, 56);
    display.print(F("Target"));
    display.print(currentTarget);
    display.print(F(" in 20s"));
  } else if (activeGameIndex != TILT_MAZE_INDEX) {
    display.setCursor(0, 56);
    display.print(F("Placeholder"));
  }
  display.display();
}

void drawGameOverScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(34, 8);
  if (activeGameIndex == COIN_COLLECTOR_INDEX && !lastRoundWon) {
    display.println(F("Time Up"));
  } else {
    display.println(lastRoundWon ? F("You Win!") : F("Game Over"));
  }
  display.drawLine(12, 18, 116, 18, SSD1306_WHITE);
  display.setCursor(12, 24);
  if (activeGameIndex == COIN_COLLECTOR_INDEX) {
    display.print(F("Coin Collector"));
  } else {
    display.print(GAME_NAMES[activeGameIndex]);
  }
  if (activeGameIndex == COIN_COLLECTOR_INDEX) {
    display.setCursor(12, 34);
    display.print(F("Score "));
    display.print(currentScore);
    display.print(F(" High "));
    display.print(highScore);
    display.setCursor(12, 44);
    display.println(F("Tap: Restart"));
    display.setCursor(12, 54);
    display.println(F("Hold: Menu"));
  } else {
    display.setCursor(18, 40);
    display.println(F("Short: Restart"));
    display.setCursor(18, 50);
    display.println(F("Long: Menu"));
  }
  display.display();
}

void startSelectedGame() {
  activeGameIndex = selectedGameIndex;
  prepareGameRound();
  appState = AppState::Playing;
  lastSerialPrintMs = 0;
  playSelectBeep();
  Serial.print(F("Starting game: "));
  Serial.println(GAME_NAMES[activeGameIndex]);
}

void restartCurrentGame() {
  prepareGameRound();
  appState = AppState::Playing;
  lastSerialPrintMs = 0;
  playSelectBeep();
  Serial.print(F("Restarting game: "));
  Serial.println(GAME_NAMES[activeGameIndex]);
}

void goToMenu() {
  appState = AppState::Menu;
  selectedGameIndex = activeGameIndex;
  playMenuMoveBeep();
  Serial.println(F("Returned to menu"));
}

void triggerGameOver(bool won) {
  appState = AppState::GameOver;
  gameOverAtMs = millis();
  lastRoundWon = won;

  if (won) {
    playTone(1400, 100);
    delay(120);
    playTone(1800, 140);
    Serial.println(F("Tilt Maze complete"));
  } else {
    playGameOverBeep();
    Serial.println(F("Placeholder game over triggered by border hit"));
  }
}

void updateMenu(ButtonEvent event) {
  if (event == ButtonEvent::ShortPress) {
    selectedGameIndex = (selectedGameIndex + 1) % GAME_COUNT;
    playMenuMoveBeep();
  } else if (event == ButtonEvent::LongPress) {
    startSelectedGame();
  }

  drawMenuScreen();
}

void updatePlaying(ButtonEvent event) {
  if (event == ButtonEvent::LongPress) {
    goToMenu();
    drawMenuScreen();
    return;
  }

  const PlayResult result = updateTiltBall();
  if (result == PlayResult::Won) {
    triggerGameOver(true);
    drawGameOverScreen();
    return;
  }

  if (result == PlayResult::Lost) {
    triggerGameOver(false);
    drawGameOverScreen();
    return;
  }

  drawPlayingScreen();
}

void updateGameOver(ButtonEvent event) {
  if ((millis() - gameOverAtMs) < GAME_OVER_HOLD_MS) {
    drawGameOverScreen();
    return;
  }

  if (event == ButtonEvent::ShortPress) {
    restartCurrentGame();
    drawPlayingScreen();
    return;
  }

  if (event == ButtonEvent::LongPress) {
    goToMenu();
    drawMenuScreen();
    return;
  }

  drawGameOverScreen();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin();
  Wire.setClock(400000);
  randomSeed(micros());

  initButtonAndBuzzer();
  initDisplay();
  loadHighScore();
  initImu();
  calibrateNeutralTilt();

  playStartupBeep();
  delay(500);

  drawMenuScreen();
  Serial.println(F("Tilt Game Console Phase 3 ready"));
}

void loop() {
  const ButtonEvent event = readButtonEvent();

  switch (appState) {
    case AppState::Menu:
      updateMenu(event);
      break;
    case AppState::Playing:
      updatePlaying(event);
      break;
    case AppState::GameOver:
      updateGameOver(event);
      break;
  }

  delay(FRAME_DELAY_MS);
}
