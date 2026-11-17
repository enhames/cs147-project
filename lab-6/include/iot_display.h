#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "iot_pins.h"

constexpr int IOT_SCREEN_WIDTH = 128;
constexpr int IOT_SCREEN_HEIGHT = 64;

inline Adafruit_SSD1306& iotDisplay() {
  static Adafruit_SSD1306 display(
      IOT_SCREEN_WIDTH,
      IOT_SCREEN_HEIGHT,
      &SPI,
      IoTPins::OLED_DC,
      IoTPins::OLED_RST,
      IoTPins::OLED_CS);
  return display;
}

inline bool iotInitDisplay() {
  auto& display = iotDisplay();
  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("OLED init failed. Check SPI wiring and pin map.");
    return false;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("IoT Kit");
  display.display();
  return true;
}

inline void iotDisplayLines(const String& line1,
                              const String& line2 = "",
                              const String& line3 = "",
                              const String& line4 = "") {
  auto& display = iotDisplay();
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(line1);
  if (line2.length()) display.println(line2);
  if (line3.length()) display.println(line3);
  if (line4.length()) display.println(line4);
  display.display();
}
