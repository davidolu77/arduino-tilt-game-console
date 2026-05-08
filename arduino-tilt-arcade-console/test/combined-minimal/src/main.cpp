#include <Arduino.h>
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
constexpr float ACCEL_LSB_PER_G = 16384.0f;
constexpr float SCREEN_X_SIGN = -1.0f;
constexpr float SCREEN_Y_SIGN = -1.0f;
constexpr uint8_t CALIBRATION_SAMPLES = 36;

constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;
constexpr unsigned long LONG_PRESS_MS = 700;
constexpr unsigned long FRAME_DELAY_MS = 22;
constexpr unsigned long GAME_OVER_HOLD_MS = 350;

constexpr uint8_t SNAKE_CELL = 5;
constexpr uint8_t SNAKE_COLS = 25;
constexpr uint8_t SNAKE_ROWS = 10;
constexpr uint8_t SNAKE_TOP = 14;
constexpr uint8_t SNAKE_MAX_LEN = 80;
constexpr unsigned long SNAKE_STEP_MS = 210;

constexpr uint8_t SHIP_Y = 57;
constexpr uint8_t ASTEROID_COUNT = 6;
constexpr float SHIP_SPEED = 3.2f;

enum class AppState { Menu, Playing, GameOver };
enum class ButtonEvent { None, ShortPress, LongPress };
enum class Direction { Up, Right, Down, Left };

struct ButtonTracker {
  bool stableState = HIGH;
  bool previousRawState = HIGH;
  unsigned long lastDebounceMs = 0;
  unsigned long pressStartedMs = 0;
};

struct Asteroid {
  int16_t x;
  int16_t y;
  int8_t speed;
  uint8_t size;
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ButtonTracker buttonTracker;

const char *const GAME_NAMES[] = {"Tilt Snake", "Space Dodger"};
constexpr uint8_t GAME_SNAKE = 0;
constexpr uint8_t GAME_DODGER = 1;
constexpr uint8_t GAME_COUNT = 2;

AppState appState = AppState::Menu;
uint8_t selectedGame = 0;
uint8_t activeGame = 0;
unsigned long gameOverAtMs = 0;

uint8_t imuAddress = 0;
float neutralAccelXg = 0.0f;
float neutralAccelYg = 0.0f;

uint8_t snakeX[SNAKE_MAX_LEN];
uint8_t snakeY[SNAKE_MAX_LEN];
uint8_t snakeLength = 0;
uint8_t foodX = 0;
uint8_t foodY = 0;
uint8_t snakeScore = 0;
uint8_t snakeBest = 0;
Direction snakeDir = Direction::Right;
unsigned long lastSnakeStepMs = 0;

float shipX = SCREEN_WIDTH / 2.0f;
Asteroid asteroids[ASTEROID_COUNT];
uint8_t dodgerScore = 0;
uint8_t dodgerBest = 0;
unsigned long dodgerStartedMs = 0;
unsigned long lastDodgerScoreMs = 0;

void beep(uint16_t frequency, uint16_t durationMs) {
  tone(BUZZER_PIN, frequency, durationMs);
}

void showText(const __FlashStringHelper *a, const __FlashStringHelper *b = nullptr) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(a);
  if (b != nullptr) {
    display.println(b);
  }
  display.display();
}

bool i2cPresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool readRegister(uint8_t address, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(address), 1) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool detectImu() {
  const uint8_t addresses[] = {IMU_ADDR_PRIMARY, IMU_ADDR_SECONDARY};
  for (uint8_t i = 0; i < sizeof(addresses); ++i) {
    if (!i2cPresent(addresses[i])) {
      continue;
    }
    uint8_t who = 0;
    if (readRegister(addresses[i], REG_WHO_AM_I, who)) {
      imuAddress = addresses[i];
      return true;
    }
  }
  return false;
}

bool readAccel(int16_t &x, int16_t &y, int16_t &z) {
  Wire.beginTransmission(imuAddress);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(imuAddress), 6) != 6) {
    return false;
  }
  x = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
  y = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
  z = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
  return true;
}

void initImu() {
  showText(F("IMU check..."));
  if (!detectImu()) {
    showText(F("IMU not found"), F("Check wiring"));
    while (true) {
      beep(350, 70);
      delay(900);
    }
  }
  writeRegister(imuAddress, REG_PWR_MGMT_1, 0x00);
  delay(100);
  writeRegister(imuAddress, REG_ACCEL_CONFIG, 0x00);
  showText(F("IMU OK"));
  beep(1500, 70);
  delay(500);
}

void calibrate() {
  showText(F("Hold still"), F("Calibrating..."));
  delay(600);

  long sumX = 0;
  long sumY = 0;
  for (uint8_t i = 0; i < CALIBRATION_SAMPLES; ++i) {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    if (!readAccel(x, y, z)) {
      showText(F("Accel read failed"));
      while (true) {
        delay(100);
      }
    }
    sumX += x;
    sumY += y;
    delay(15);
  }
  neutralAccelXg = (sumX / static_cast<float>(CALIBRATION_SAMPLES)) / ACCEL_LSB_PER_G;
  neutralAccelYg = (sumY / static_cast<float>(CALIBRATION_SAMPLES)) / ACCEL_LSB_PER_G;
}

void readTilt(float &tiltX, float &tiltY) {
  int16_t rawX = 0;
  int16_t rawY = 0;
  int16_t rawZ = 0;
  if (!readAccel(rawX, rawY, rawZ)) {
    tiltX = 0.0f;
    tiltY = 0.0f;
    return;
  }
  const float accelXg = rawX / ACCEL_LSB_PER_G;
  const float accelYg = rawY / ACCEL_LSB_PER_G;
  tiltX = constrain((accelYg - neutralAccelYg) * SCREEN_X_SIGN, -1.0f, 1.0f);
  tiltY = constrain((accelXg - neutralAccelXg) * SCREEN_Y_SIGN, -1.0f, 1.0f);
}

ButtonEvent readButtonEvent() {
  const bool rawState = digitalRead(BUTTON_PIN);
  const unsigned long now = millis();
  if (rawState != buttonTracker.previousRawState) {
    buttonTracker.lastDebounceMs = now;
    buttonTracker.previousRawState = rawState;
  }
  if ((now - buttonTracker.lastDebounceMs) < BUTTON_DEBOUNCE_MS || rawState == buttonTracker.stableState) {
    return ButtonEvent::None;
  }
  buttonTracker.stableState = rawState;
  if (buttonTracker.stableState == LOW) {
    buttonTracker.pressStartedMs = now;
    return ButtonEvent::None;
  }
  return (now - buttonTracker.pressStartedMs) >= LONG_PRESS_MS ? ButtonEvent::LongPress : ButtonEvent::ShortPress;
}

bool snakeOccupies(uint8_t x, uint8_t y) {
  for (uint8_t i = 0; i < snakeLength; ++i) {
    if (snakeX[i] == x && snakeY[i] == y) {
      return true;
    }
  }
  return false;
}

void spawnFood() {
  for (uint8_t attempt = 0; attempt < 70; ++attempt) {
    const uint8_t x = random(SNAKE_COLS);
    const uint8_t y = random(SNAKE_ROWS);
    if (!snakeOccupies(x, y)) {
      foodX = x;
      foodY = y;
      return;
    }
  }
  foodX = 20;
  foodY = 5;
}

void resetSnake() {
  snakeLength = 4;
  snakeScore = 0;
  snakeDir = Direction::Right;
  for (uint8_t i = 0; i < snakeLength; ++i) {
    snakeX[i] = 8 - i;
    snakeY[i] = SNAKE_ROWS / 2;
  }
  spawnFood();
  lastSnakeStepMs = millis();
}

bool opposite(Direction a, Direction b) {
  return (a == Direction::Up && b == Direction::Down) ||
         (a == Direction::Down && b == Direction::Up) ||
         (a == Direction::Left && b == Direction::Right) ||
         (a == Direction::Right && b == Direction::Left);
}

void updateSnakeDirection() {
  float tiltX = 0.0f;
  float tiltY = 0.0f;
  readTilt(tiltX, tiltY);
  Direction requested = snakeDir;
  if (fabs(tiltX) > fabs(tiltY) && fabs(tiltX) > 0.18f) {
    requested = tiltX > 0.0f ? Direction::Right : Direction::Left;
  } else if (fabs(tiltY) > 0.18f) {
    requested = tiltY > 0.0f ? Direction::Down : Direction::Up;
  }
  if (!opposite(requested, snakeDir)) {
    snakeDir = requested;
  }
}

bool stepSnake() {
  updateSnakeDirection();
  if (millis() - lastSnakeStepMs < SNAKE_STEP_MS) {
    return true;
  }
  lastSnakeStepMs = millis();

  int8_t nextX = snakeX[0];
  int8_t nextY = snakeY[0];
  if (snakeDir == Direction::Up) {
    --nextY;
  } else if (snakeDir == Direction::Down) {
    ++nextY;
  } else if (snakeDir == Direction::Left) {
    --nextX;
  } else {
    ++nextX;
  }

  if (nextX < 0 || nextX >= SNAKE_COLS || nextY < 0 || nextY >= SNAKE_ROWS) {
    return false;
  }

  const bool eating = nextX == foodX && nextY == foodY;
  const uint8_t checkLength = eating ? snakeLength : snakeLength - 1;
  for (uint8_t i = 0; i < checkLength; ++i) {
    if (snakeX[i] == nextX && snakeY[i] == nextY) {
      return false;
    }
  }

  if (eating && snakeLength < SNAKE_MAX_LEN) {
    ++snakeLength;
  }
  for (int16_t i = snakeLength - 1; i > 0; --i) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }
  snakeX[0] = nextX;
  snakeY[0] = nextY;

  if (eating) {
    ++snakeScore;
    if (snakeScore > snakeBest) {
      snakeBest = snakeScore;
    }
    beep(1800, 45);
    spawnFood();
  }
  return true;
}

void resetAsteroid(uint8_t index) {
  asteroids[index].x = random(5, SCREEN_WIDTH - 5);
  asteroids[index].size = random(2, 5);
  asteroids[index].speed = random(1, 3);
  asteroids[index].y = -random(8, 80);
}

void resetDodger() {
  shipX = SCREEN_WIDTH / 2.0f;
  dodgerScore = 0;
  dodgerStartedMs = millis();
  lastDodgerScoreMs = millis();
  for (uint8_t i = 0; i < ASTEROID_COUNT; ++i) {
    resetAsteroid(i);
  }
}

bool overlap(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
             int16_t bx, int16_t by, int16_t bw, int16_t bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

bool updateDodger() {
  float tiltX = 0.0f;
  float tiltY = 0.0f;
  readTilt(tiltX, tiltY);
  if (fabs(tiltX) > 0.05f) {
    shipX += tiltX * SHIP_SPEED;
  }
  shipX = constrain(shipX, 8.0f, SCREEN_WIDTH - 9.0f);

  const uint8_t speedLevel = (millis() - dodgerStartedMs) / 18000UL;
  const uint8_t bonusSpeed = speedLevel > 2 ? 2 : speedLevel;
  for (uint8_t i = 0; i < ASTEROID_COUNT; ++i) {
    asteroids[i].y += asteroids[i].speed + bonusSpeed;
    if (asteroids[i].y > SCREEN_HEIGHT + 5) {
      resetAsteroid(i);
    }
    const int16_t size = asteroids[i].size * 2 + 1;
    if (overlap(static_cast<int16_t>(shipX) - 6, SHIP_Y - 6, 13, 10,
                asteroids[i].x - asteroids[i].size, asteroids[i].y - asteroids[i].size, size, size)) {
      return false;
    }
  }
  if (millis() - lastDodgerScoreMs >= 1000) {
    lastDodgerScoreMs += 1000;
    if (dodgerScore < 255) {
      ++dodgerScore;
    }
    if (dodgerScore > dodgerBest) {
      dodgerBest = dodgerScore;
    }
  }
  return true;
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Arcade Menu"));
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  for (uint8_t i = 0; i < GAME_COUNT; ++i) {
    const int16_t y = 20 + i * 16;
    if (i == selectedGame) {
      display.fillRect(0, y - 2, 128, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(5, y);
    display.print(i == selectedGame ? F("> ") : F("  "));
    display.print(GAME_NAMES[i]);
  }
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print(F("Tap next Hold play"));
  display.display();
}

void drawSnake() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Snake S"));
  display.print(snakeScore);
  display.print(F(" B"));
  display.print(snakeBest);
  display.drawRect(0, SNAKE_TOP, SNAKE_COLS * SNAKE_CELL + 2, SNAKE_ROWS * SNAKE_CELL + 2, SSD1306_WHITE);
  display.fillRect(foodX * SNAKE_CELL + 1, SNAKE_TOP + foodY * SNAKE_CELL + 1, SNAKE_CELL, SNAKE_CELL, SSD1306_WHITE);
  for (uint8_t i = 0; i < snakeLength; ++i) {
    const uint8_t inset = i == 0 ? 0 : 1;
    display.fillRect(snakeX[i] * SNAKE_CELL + 1 + inset,
                     SNAKE_TOP + snakeY[i] * SNAKE_CELL + 1 + inset,
                     SNAKE_CELL - inset,
                     SNAKE_CELL - inset,
                     SSD1306_WHITE);
  }
  display.display();
}

void drawShip(int16_t x, int16_t y) {
  display.drawFastVLine(x, y - 7, 12, SSD1306_WHITE);
  display.fillRect(x - 2, y - 3, 5, 8, SSD1306_WHITE);
  display.drawLine(x - 3, y + 1, x - 8, y + 5, SSD1306_WHITE);
  display.drawLine(x + 3, y + 1, x + 8, y + 5, SSD1306_WHITE);
  display.drawPixel(x, y - 8, SSD1306_WHITE);
  display.drawPixel(x - 1, y + 6, SSD1306_WHITE);
  display.drawPixel(x + 1, y + 6, SSD1306_WHITE);
}

void drawDodger() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Dodger S"));
  display.print(dodgerScore);
  display.print(F(" B"));
  display.print(dodgerBest);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  for (uint8_t i = 0; i < ASTEROID_COUNT; ++i) {
    display.fillCircle(asteroids[i].x, asteroids[i].y, asteroids[i].size, SSD1306_WHITE);
  }
  drawShip(static_cast<int16_t>(shipX), SHIP_Y);
  display.display();
}

void drawGameOver() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(36, 8);
  display.print(F("Game Over"));
  display.drawLine(12, 18, 116, 18, SSD1306_WHITE);
  display.setCursor(12, 26);
  display.print(GAME_NAMES[activeGame]);
  display.setCursor(12, 38);
  display.print(F("Tap restart"));
  display.setCursor(12, 50);
  display.print(F("Hold menu"));
  display.display();
}

void startSelectedGame() {
  activeGame = selectedGame;
  if (activeGame == GAME_SNAKE) {
    resetSnake();
  } else {
    resetDodger();
  }
  appState = AppState::Playing;
  beep(1600, 70);
}

void triggerGameOver() {
  appState = AppState::GameOver;
  gameOverAtMs = millis();
  beep(450, 160);
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  Wire.begin();
  Wire.setClock(100000);
  randomSeed(micros());

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    while (true) {
      beep(300, 80);
      delay(800);
    }
  }
  showText(F("OLED OK"), F("Minimal combo"));
  beep(1200, 70);
  delay(600);
  initImu();
  calibrate();
  showText(F("Menu loading"), F("Ready"));
  delay(500);
  drawMenu();
}

void loop() {
  const ButtonEvent event = readButtonEvent();

  if (appState == AppState::Menu) {
    if (event == ButtonEvent::ShortPress) {
      selectedGame = (selectedGame + 1) % GAME_COUNT;
      beep(1300, 45);
    } else if (event == ButtonEvent::LongPress) {
      startSelectedGame();
    }
    drawMenu();
  } else if (appState == AppState::Playing) {
    if (event == ButtonEvent::LongPress) {
      appState = AppState::Menu;
      selectedGame = activeGame;
      drawMenu();
      delay(FRAME_DELAY_MS);
      return;
    }
    const bool alive = activeGame == GAME_SNAKE ? stepSnake() : updateDodger();
    if (!alive) {
      triggerGameOver();
      drawGameOver();
    } else if (activeGame == GAME_SNAKE) {
      drawSnake();
    } else {
      drawDodger();
    }
  } else {
    if ((millis() - gameOverAtMs) >= GAME_OVER_HOLD_MS) {
      if (event == ButtonEvent::ShortPress) {
        if (activeGame == GAME_SNAKE) {
          resetSnake();
        } else {
          resetDodger();
        }
        appState = AppState::Playing;
      } else if (event == ButtonEvent::LongPress) {
        appState = AppState::Menu;
        selectedGame = activeGame;
      }
    }
    if (appState == AppState::GameOver) {
      drawGameOver();
    } else if (appState == AppState::Menu) {
      drawMenu();
    }
  }

  delay(FRAME_DELAY_MS);
}
