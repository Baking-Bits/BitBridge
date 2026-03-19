#include <Arduino.h>
#include <esp_system.h>

static const int kPulsePins[] = {45, 42, 21, 47, 48, 38, 39, 40, 41};

static void setPins(bool on) {
  for (size_t i = 0; i < sizeof(kPulsePins) / sizeof(kPulsePins[0]); i++) {
    int pin = kPulsePins[i];
    if (pin < 0 || pin > 48) continue;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, on ? HIGH : LOW);
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && (millis() - start) < 2500) {
    delay(10);
  }

  for (size_t i = 0; i < sizeof(kPulsePins) / sizeof(kPulsePins[0]); i++) {
    int pin = kPulsePins[i];
    if (pin < 0 || pin > 48) continue;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  Serial.println("=== S3 LIFESIGN START ===");
  Serial.printf("reset_reason=%d\n", (int)esp_reset_reason());
  Serial.println("Pulsing pins: 45 42 21 47 48 38 39 40 41");
}

void loop() {
  static uint32_t last = 0;
  static bool state = false;
  if (millis() - last >= 400) {
    last = millis();
    state = !state;
    setPins(state);
    Serial.printf("tick %lu state=%d\n", (unsigned long)(millis() / 1000), state ? 1 : 0);
  }
  delay(5);
}
