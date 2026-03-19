#include <Arduino.h>

void setup() {
  pinMode(42, OUTPUT);
}

void loop() {
  digitalWrite(42, HIGH);
  delay(500);
  digitalWrite(42, LOW);
  delay(500);
}
