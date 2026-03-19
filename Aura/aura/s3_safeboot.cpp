#include <Arduino.h>
#include <esp_system.h>

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("=== S3 SAFEBOOT START ===");
  Serial.printf("reset_reason=%d\n", (int)esp_reset_reason());
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    Serial.printf("safe alive %lu\n", (unsigned long)(millis() / 1000));
  }
  delay(10);
}
