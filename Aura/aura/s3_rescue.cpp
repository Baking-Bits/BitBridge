#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_system.h>

#ifndef TFT_BL
#define TFT_BL 45
#endif

#ifndef RGB_LED_PIN
#define RGB_LED_PIN 42
#endif

#ifndef S3_DIAG_MODE
#define S3_DIAG_MODE 0
#endif

TFT_eSPI tft = TFT_eSPI();

static void backlightOnAllCommonPins() {
  const int pins[] = {TFT_BL, 45, 21, 38, 46, 47, 48};
  for (size_t i = 0; i < (sizeof(pins) / sizeof(pins[0])); i++) {
    int pin = pins[i];
    if (pin < 0 || pin > 48) continue;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
}

static void setBacklightPulse(bool on) {
  const int pins[] = {TFT_BL, 45, 21};
  for (size_t i = 0; i < (sizeof(pins) / sizeof(pins[0])); i++) {
    int pin = pins[i];
    if (pin < 0 || pin > 48) continue;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, on ? HIGH : LOW);
  }
}

static void rgbAliveTick() {
  pinMode(RGB_LED_PIN, OUTPUT);
  static uint8_t phase = 0;
  digitalWrite(RGB_LED_PIN, (phase++ & 1) ? HIGH : LOW);
}

static void drawRescueScreen(uint8_t rotation, uint16_t bgColor, const char* title) {
  tft.setRotation(rotation);
  tft.fillScreen(bgColor);
  tft.setTextColor(TFT_WHITE, bgColor);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.print(title);
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.print("S3 rescue diagnostic");
  tft.setCursor(10, 66);
  tft.print("RGB42 + serial should tick");
}

void setup() {
  Serial.begin(115200);
  uint32_t serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 2500) {
    delay(10);
  }
  Serial.println("=== S3 RESCUE DIAG START ===");
  delay(1200);

  backlightOnAllCommonPins();

  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_DC, HIGH);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("S3 rescue boot, reset_reason=%d\n", (int)reason);

#if S3_DIAG_MODE
  for (int i = 0; i < 4; i++) {
    setBacklightPulse(false);
    rgbAliveTick();
    delay(120);
    setBacklightPulse(true);
    rgbAliveTick();
    delay(120);
  }
#endif

  tft.init();
  drawRescueScreen(0, TFT_BLACK, "S3 Rescue");
  delay(250);
  drawRescueScreen(1, TFT_BLUE, "S3 Rescue");
  delay(250);
  drawRescueScreen(2, TFT_GREEN, "S3 Rescue");
  delay(250);
  drawRescueScreen(3, TFT_RED, "S3 Rescue");
  delay(250);
  drawRescueScreen(0, TFT_BLACK, "S3 Rescue");

  Serial.println("S3 rescue booted");
}

void loop() {
  static uint32_t last = 0;
  static uint32_t lastRedraw = 0;
  static uint8_t rotation = 0;
  static uint16_t colors[] = {TFT_BLACK, TFT_BLUE, TFT_GREEN, TFT_RED};

  if (millis() - last >= 1000) {
    last = millis();
    rgbAliveTick();
    Serial.printf("alive %lu\n", (unsigned long)(millis() / 1000));
  }

#if S3_DIAG_MODE
  if (millis() - lastRedraw >= 2500) {
    lastRedraw = millis();
    setBacklightPulse(true);
    drawRescueScreen(rotation & 3, colors[rotation & 3], "S3 Rescue D");
    rotation++;
  }
#endif

  delay(10);
}
