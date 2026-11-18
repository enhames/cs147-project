#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "iot_pins.h"

inline void iotStartSerial(const char* activityName) {
  Serial.begin(115200);
  delay(600);
  Serial.println();
  Serial.println("========================================");
  Serial.print("IoT@UCI Activity: ");
  Serial.println(activityName);
  Serial.println("========================================");
}

inline void iotInitI2C() {
  Wire.begin(IoTPins::I2C_SDA, IoTPins::I2C_SCL);  // Start the shared 100 kHz I2C bus used by BMP280 and BH1750.
}

inline void iotInitSPI() {
  SPI.begin(IoTPins::SPI_SCK, IoTPins::SPI_MISO, IoTPins::SPI_MOSI);  // Start the separate SPI bus used by the OLED.
}

inline void iotPrintDeviceId() {
  uint64_t chipid = ESP.getEfuseMac();
  Serial.print("ESP32 chip id: 0x");
  Serial.println((uint32_t)(chipid & 0xFFFFFFFF), HEX);
}

inline bool iotEvery(uint32_t intervalMs, uint32_t& lastMs) {
  const uint32_t now = millis();
  if (now - lastMs >= intervalMs) {
    lastMs = now;
    return true;
  }
  return false;
}

inline void iotI2CScan() {
  Serial.println("Scanning I2C bus...");
  int count = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);  // Address-only probe asks each possible I2C device for an ACK.
    if (Wire.endTransmission() == 0) {
      Serial.print("Found I2C device at 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      count++;
    }
  }
  Serial.print("I2C devices found: ");
  Serial.println(count);
}
