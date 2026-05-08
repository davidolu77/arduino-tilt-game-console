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
constexpr uint8_t CALIBRATION_SAMPLES = 40;

constexpr uint8_t SNAKE_CELL = 4;
constexpr uint8_t SNAKE_COLS = 32;
constexpr uint8_t SNAKE_ROWS = 13;
constexpr uint8_t SNAKE_TOP = 12;
constexpr uint8_t SNAKE_MAX_LEN = 96;
constexpr unsigned long SNAKE_STEP_MS = 165;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;
constexpr unsigned long LONG_PRESS_MS = 700;

enum class Direction {
  Up,
  Right,
  Down,
  Left,
};

struct ButtonTracker {
  bool stableState = HIGH;
  bool previousRawState = HIGH;
  unsigned long lastDebounceMs = 0;
  unsigned long pressStartedMs = 0;
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ButtonTracker buttonTracker;

uint8_t imuAddress = 0;
float neutralAccelXg = 0.0f;
float neutralAccelYg = 0.0f;

uint8_t snakeX[SNAKE_MAX_LEN];
uint8_t snakeY[SNAKE_MAX_LEN];
uint8_t snakeLength = 0;
uint8_t foodX = 0;
uint8_t foodY = 0;
uint8_t score = 0;
uint8_t best = 0;
Direction dir = Direction::Right;
unsigned long lastStepMs = 0;
bool gameOver = false;

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
}

void calibrate() {
  showText(F("Snake only test"), F("Hold still..."));
  delay(700);

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

bool shortButtonPressed() {
  const bool rawState = digitalRead(BUTTON_PIN);
  const unsigned long now = millis();

  if (rawState != buttonTracker.previousRawState) {
    buttonTracker.lastDebounceMs = now;
    buttonTracker.previousRawState = rawState;
  }

  if ((now - buttonTracker.lastDebounceMs) < BUTTON_DEBOUNCE_MS || rawState == buttonTracker.stableState) {
    return false;
  }

  buttonTracker.stableState = rawState;
  if (buttonTracker.stableState == LOW) {
    buttonTracker.pressStartedMs = now;
    return false;
  }

  return (now - buttonTracker.pressStartedMs) < LONG_PRESS_MS;
}

bool occupies(uint8_t x, uint8_t y) {
  for (uint8_t i = 0; i < snakeLength; ++i) {
    if (snakeX[i] == x && snakeY[i] == y) {
      return true;
    }
  }
  return false;
}

void spawnFood() {
  for (uint8_t attempt = 0; attempt < 80; ++attempt) {
    const uint8_t x = random(SNAKE_COLS);
    const uint8_t y = random(SNAKE_ROWS);
    if (!occupies(x, y)) {
      foodX = x;
      foodY = y;
      return;
    }
  }
  foodX = 26;
  foodY = 6;
}

void resetGame() {
  snakeLength = 5;
  score = 0;
  dir = Direction::Right;
  gameOver = false;

  for (uint8_t i = 0; i < snakeLength; ++i) {
    snakeX[i] = 10 - i;
    snakeY[i] = SNAKE_ROWS / 2;
  }

  spawnFood();
  lastStepMs = millis();
  beep(1400, 70);
}

bool opposite(Direction a, Direction b) {
  return (a == Direction::Up && b == Direction::Down) ||
         (a == Direction::Down && b == Direction::Up) ||
         (a == Direction::Left && b == Direction::Right) ||
         (a == Direction::Right && b == Direction::Left);
}

void updateDirection() {
  float tiltX = 0.0f;
  float tiltY = 0.0f;
  readTilt(tiltX, tiltY);

  Direction requested = dir;
  if (fabs(tiltX) > fabs(tiltY) && fabs(tiltX) > 0.18f) {
    requested = tiltX > 0.0f ? Direction::Right : Direction::Left;
  } else if (fabs(tiltY) > 0.18f) {
    requested = tiltY > 0.0f ? Direction::Down : Direction::Up;
  }

  if (!opposite(requested, dir)) {
    dir = requested;
  }
}

void stepSnake() {
  updateDirection();

  if (millis() - lastStepMs < SNAKE_STEP_MS) {
    return;
  }
  lastStepMs = millis();

  int8_t nextX = snakeX[0];
  int8_t nextY = snakeY[0];
  if (dir == Direction::Up) {
    --nextY;
  } else if (dir == Direction::Down) {
    ++nextY;
  } else if (dir == Direction::Left) {
    --nextX;
  } else {
    ++nextX;
  }

  if (nextX < 0 || nextX >= SNAKE_COLS || nextY < 0 || nextY >= SNAKE_ROWS) {
    gameOver = true;
    beep(450, 180);
    return;
  }

  const bool eating = nextX == foodX && nextY == foodY;
  const uint8_t checkLength = eating ? snakeLength : snakeLength - 1;
  for (uint8_t i = 0; i < checkLength; ++i) {
    if (snakeX[i] == nextX && snakeY[i] == nextY) {
      gameOver = true;
      beep(450, 180);
      return;
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
    ++score;
    if (score > best) {
      best = score;
    }
    beep(1800, 45);
    spawnFood();
  }
}

void drawGame() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Snake S"));
  display.print(score);
  display.print(F(" B"));
  display.print(best);
  display.drawRect(0, SNAKE_TOP, SCREEN_WIDTH, SCREEN_HEIGHT - SNAKE_TOP, SSD1306_WHITE);

  display.fillRect(foodX * SNAKE_CELL + 1, SNAKE_TOP + foodY * SNAKE_CELL + 1, 3, 3, SSD1306_WHITE);

  for (uint8_t i = 0; i < snakeLength; ++i) {
    const uint8_t inset = i == 0 ? 0 : 1;
    display.fillRect(snakeX[i] * SNAKE_CELL + inset,
                     SNAKE_TOP + snakeY[i] * SNAKE_CELL + inset,
                     SNAKE_CELL - inset,
                     SNAKE_CELL - inset,
                     SSD1306_WHITE);
  }

  if (gameOver) {
    display.fillRect(20, 24, 88, 22, SSD1306_BLACK);
    display.drawRect(20, 24, 88, 22, SSD1306_WHITE);
    display.setCursor(38, 29);
    display.print(F("Game Over"));
    display.setCursor(29, 39);
    display.print(F("Tap restart"));
  }

  display.display();
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
      beep(300, 70);
      delay(800);
    }
  }

  showText(F("OLED OK"), F("Snake loading"));
  delay(600);
  initImu();
  calibrate();
  resetGame();
}

void loop() {
  if (shortButtonPressed()) {
    resetGame();
  }

  if (!gameOver) {
    stepSnake();
  }

  drawGame();
  delay(20);
}
