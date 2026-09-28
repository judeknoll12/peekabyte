// Peekabyte - a pocket pet made of two very expressive eyes.
//
// ESP32 + 0.96" I2C OLED + MPU6050/6500 motion sensor. Feed it, pet it, play with
// it and teach it tricks from your phone over Bluetooth (open the Peekabyte app in
// Chrome on Android or in the Bluefy browser on iPhone).
//
// Wiring: OLED and MPU share I2C: SDA -> GPIO21, SCL -> GPIO22, VCC -> 3V3, GND -> GND.
// Libraries: U8g2. Board: "ESP32 Dev Module", Partition Scheme: "Huge APP (3MB No OTA)".

#include "app.h"

void setup() { app::setup(); }

void loop() { app::loop(); }
