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
  constexpr uint8_t SOIL_MOISTURE_ADC = 36; // VP, ADC1 input-only; remains usable with Wi-Fi

  constexpr uint8_t I2C_SDA = 21;
  constexpr uint8_t I2C_SCL = 22;
  constexpr uint8_t BMP280_ADDR = 0x76;
  constexpr uint8_t MPU6050_ADDR = 0x68;
  constexpr uint8_t ATECC608A_ADDR = 0x60;
  constexpr uint8_t BH1750_ADDR_LOW = 0x23;   // ADDR tied low/GND selects binary 0100011.
  constexpr uint8_t BH1750_ADDR_HIGH = 0x5C;  // ADDR tied high selects binary 1011100.
  constexpr uint8_t BH1750_CONTINUOUS_HIGH_RES_MODE = 0x10; // Continuous 1 lx mode, typically 120 ms.

  constexpr uint8_t SPI_SCK = 18;
  constexpr uint8_t SPI_MISO = 19;
  constexpr uint8_t SPI_MOSI = 23;
  constexpr uint8_t OLED_CS = 5;
  constexpr uint8_t OLED_DC = 16;
  constexpr uint8_t OLED_RST = 17;
}
