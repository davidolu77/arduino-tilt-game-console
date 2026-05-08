#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {

constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_RESET = 255;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint8_t BUZZER_PIN = 9;

constexpr uint8_t IMU_ADDR_PRIMARY = 0x68;
constexpr uint8_t IMU_ADDR_SECONDARY = 0x69;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x75;
constexpr float ACCEL_LSB_PER_G = 16384.0f;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint8_t imuAddress = 0;
uint8_t imuWhoAmI = 0;
unsigned long lastDrawMs = 0;

void beep(uint16_t frequency, uint16_t durationMs) {
  tone(BUZZER_PIN, frequency, durationMs);
  delay(durationMs + 30);
  noTone(BUZZER_PIN);
}

void showLines(const __FlashStringHelper *a,
               const __FlashStringHelper *b = nullptr,
               const __FlashStringHelper *c = nullptr) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(a);
  if (b != nullptr) {
    display.println(b);
  }
  if (c != nullptr) {
    display.println(c);
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
      imuWhoAmI = who;
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

void drawI2cScan() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("I2C scan"));

  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    if (!i2cPresent(address)) {
      continue;
    }

    Serial.print(F("Found 0x"));
    if (address < 16) {
      Serial.print('0');
    }
    Serial.println(address, HEX);

    display.print(F("0x"));
    if (address < 16) {
      display.print('0');
    }
    display.print(address, HEX);
    display.print(' ');
    ++found;
  }

  display.println();
  display.print(F("Found: "));
  display.print(found);
  display.display();
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  Wire.begin();
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    while (true) {
      beep(300, 80);
      delay(600);
    }
  }

  showLines(F("OLED OK"), F("Starting scan..."));
  beep(1200, 80);
  delay(700);

  drawI2cScan();
  delay(1800);

  showLines(F("Checking IMU..."));
  if (!detectImu()) {
    showLines(F("IMU not found"), F("Expected 0x68/0x69"));
    while (true) {
      beep(400, 80);
      delay(850);
    }
  }

  writeRegister(imuAddress, REG_PWR_MGMT_1, 0x00);
  delay(100);
  writeRegister(imuAddress, REG_ACCEL_CONFIG, 0x00);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("IMU OK"));
  display.print(F("Addr 0x"));
  if (imuAddress < 16) {
    display.print('0');
  }
  display.println(imuAddress, HEX);
  display.print(F("WHO 0x"));
  if (imuWhoAmI < 16) {
    display.print('0');
  }
  display.println(imuWhoAmI, HEX);
  display.println(F("Live values next"));
  display.display();
  beep(1600, 80);
  delay(1600);
}

void loop() {
  if (millis() - lastDrawMs < 180) {
    return;
  }
  lastDrawMs = millis();

  int16_t rawX = 0;
  int16_t rawY = 0;
  int16_t rawZ = 0;
  const bool ok = readAccel(rawX, rawY, rawZ);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("OLED + IMU TEST"));
  display.drawLine(0, 9, SCREEN_WIDTH - 1, 9, SSD1306_WHITE);

  if (!ok) {
    display.setCursor(0, 18);
    display.println(F("Accel read failed"));
  } else {
    display.setCursor(0, 14);
    display.print(F("X "));
    display.println(rawX / ACCEL_LSB_PER_G, 2);
    display.print(F("Y "));
    display.println(rawY / ACCEL_LSB_PER_G, 2);
    display.print(F("Z "));
    display.println(rawZ / ACCEL_LSB_PER_G, 2);
    display.setCursor(0, 54);
    display.print(F("Tilt board: values move"));
  }

  display.display();
}
