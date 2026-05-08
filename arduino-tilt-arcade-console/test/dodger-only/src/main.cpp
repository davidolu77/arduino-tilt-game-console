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
constexpr uint8_t CALIBRATION_SAMPLES = 40;

constexpr uint8_t DODGER_TOP = 12;
constexpr uint8_t SHIP_Y = 58;
constexpr uint8_t ASTEROID_COUNT = 7;
constexpr float SHIP_SPEED = 3.3f;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 25;
constexpr unsigned long LONG_PRESS_MS = 700;

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

uint8_t imuAddress = 0;
float neutralAccelYg = 0.0f;
float shipX = SCREEN_WIDTH / 2.0f;
Asteroid asteroids[ASTEROID_COUNT];
uint8_t score = 0;
uint8_t best = 0;
bool gameOver = false;
unsigned long startedMs = 0;
unsigned long lastScoreMs = 0;

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
  showText(F("Dodger only test"), F("Hold still..."));
  delay(700);

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
    sumY += y;
    delay(15);
  }

  neutralAccelYg = (sumY / static_cast<float>(CALIBRATION_SAMPLES)) / ACCEL_LSB_PER_G;
}

float readTiltX() {
  int16_t rawX = 0;
  int16_t rawY = 0;
  int16_t rawZ = 0;
  if (!readAccel(rawX, rawY, rawZ)) {
    return 0.0f;
  }

  const float accelYg = rawY / ACCEL_LSB_PER_G;
  return constrain((accelYg - neutralAccelYg) * SCREEN_X_SIGN, -1.0f, 1.0f);
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

void resetAsteroid(uint8_t index, bool aboveScreen) {
  asteroids[index].x = random(5, SCREEN_WIDTH - 5);
  asteroids[index].size = random(2, 5);
  asteroids[index].speed = random(1, 3);
  asteroids[index].y = aboveScreen ? -random(8, 80) : random(DODGER_TOP, SCREEN_HEIGHT / 2);
}

void resetGame() {
  shipX = SCREEN_WIDTH / 2.0f;
  score = 0;
  gameOver = false;
  startedMs = millis();
  lastScoreMs = millis();

  for (uint8_t i = 0; i < ASTEROID_COUNT; ++i) {
    resetAsteroid(i, true);
  }

  beep(1500, 70);
}

bool overlap(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
             int16_t bx, int16_t by, int16_t bw, int16_t bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void updateGame() {
  const float tiltX = readTiltX();
  if (fabs(tiltX) > 0.05f) {
    shipX += tiltX * SHIP_SPEED;
  }
  shipX = constrain(shipX, 7.0f, SCREEN_WIDTH - 8.0f);

  const uint8_t speedLevel = (millis() - startedMs) / 18000UL;
  const uint8_t bonusSpeed = speedLevel > 2 ? 2 : speedLevel;

  for (uint8_t i = 0; i < ASTEROID_COUNT; ++i) {
    asteroids[i].y += asteroids[i].speed + bonusSpeed;
    if (asteroids[i].y > SCREEN_HEIGHT + 5) {
      resetAsteroid(i, true);
    }

    const int16_t size = asteroids[i].size * 2 + 1;
    if (overlap(static_cast<int16_t>(shipX) - 5, SHIP_Y - 4, 11, 8,
                asteroids[i].x - asteroids[i].size,
                asteroids[i].y - asteroids[i].size,
                size,
                size)) {
      gameOver = true;
      beep(450, 180);
      return;
    }
  }

  if (millis() - lastScoreMs >= 1000) {
    lastScoreMs += 1000;
    if (score < 255) {
      ++score;
    }
    if (score > best) {
      best = score;
    }
  }
}

void drawGame() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Dodger S"));
  display.print(score);
  display.print(F(" B"));
  display.print(best);
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);

  for (uint8_t i = 0; i < ASTEROID_COUNT; ++i) {
    display.fillCircle(asteroids[i].x, asteroids[i].y, asteroids[i].size, SSD1306_WHITE);
  }

  const int16_t sx = static_cast<int16_t>(shipX);
  display.fillTriangle(sx, SHIP_Y - 6, sx - 7, SHIP_Y + 4, sx + 7, SHIP_Y + 4, SSD1306_WHITE);

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

  showText(F("OLED OK"), F("Dodger loading"));
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
    updateGame();
  }

  drawGame();
  delay(20);
}
