#pragma once
#include <Arduino.h>

namespace IoTPins {
  constexpr uint8_t LED_1 = 2;             // Red
  constexpr uint8_t LED_2 = 4;             // Yellow
  constexpr uint8_t LED_3 = 15;            // Green
  constexpr uint8_t BUZZER = 27;
  constexpr uint8_t BUTTON_1 = 32;          // Alert acknowledge, INPUT_PULLUP
  constexpr uint8_t BUTTON_2 = 33;
  constexpr uint8_t POT_ADC = 34;
  constexpr uint8_t SOIL_MOISTURE_ADC = 35; // VP, ADC1 input-only; remains usable with Wi-Fi

  constexpr uint8_t I2C_SDA = 21;
  constexpr uint8_t I2C_SCL = 22;
  constexpr uint8_t BMP280_ADDR = 0x76;
  constexpr uint8_t MPU6050_ADDR = 0x68;
  constexpr uint8_t ATECC608A_ADDR = 0x60;

  constexpr uint8_t SPI_SCK = 18;
  constexpr uint8_t SPI_MISO = 19;
  constexpr uint8_t SPI_MOSI = 23;
  constexpr uint8_t OLED_CS = 5;
  constexpr uint8_t OLED_DC = 16;
  constexpr uint8_t OLED_RST = 17;
}
