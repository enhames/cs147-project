#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
// Fall 2026 kit pin map for the ESP32 DevKit V1 (ESP32-WROOM-32).
// Avoid GPIO 6-11; these are normally connected to onboard flash.
// Avoid GPIO 34/35/36/39 for buttons because they are input-only and do
//   NOT support internal pull-up/down resistors.
// -----------------------------------------------------------------------------

namespace IoTPins {
  // Basic I/O
  constexpr uint8_t LED_1      = 2;    // Red LED (also the onboard LED on many boards)
  constexpr uint8_t LED_2      = 4;    // Yellow LED
  constexpr uint8_t LED_3      = 15;   // Green LED
  constexpr uint8_t BUZZER     = 27;   // Passive buzzer, driven with tone()
  constexpr uint8_t BUTTON_1   = 32;   // Use INPUT_PULLUP with button to GND
  constexpr uint8_t BUTTON_2   = 33;   // Use INPUT_PULLUP with button to GND
  constexpr uint8_t POT_ADC    = 34;   // ADC1 input-only pin
  constexpr uint8_t SERVO_PWM  = 13;   // Servo signal (orange wire); servo powered from the 5 V rail

  // I2C bus: BMP280, MPU6050, ATECC608A Qwiic breakout
  constexpr uint8_t I2C_SDA    = 21;
  constexpr uint8_t I2C_SCL    = 22;
  // Double check addresses
  constexpr uint8_t BMP280_ADDR = 0x76;
  constexpr uint8_t MPU6050_ADDR = 0x68;
  constexpr uint8_t ATECC608A_ADDR = 0x60;
  // Optional interrupt pin
  constexpr uint8_t MPU6050_INT = 35;  // input-only pin, fine for an interrupt

  // Shared VSPI bus: SPI OLED now, LoRa later
  constexpr uint8_t SPI_SCK    = 18;
  constexpr uint8_t SPI_MISO   = 19;   // OLED may not use MISO; LoRa uses it
  constexpr uint8_t SPI_MOSI   = 23;

  // SPI OLED display pins. Assumes 128x64 SSD1306 SPI OLED.
  constexpr uint8_t OLED_CS    = 5;
  constexpr uint8_t OLED_DC    = 16;
  constexpr uint8_t OLED_RST   = 17;

  // LoRa SX1276/RFM95-style module pins. Shares SCK/MISO/MOSI with OLED.
  constexpr uint8_t LORA_CS    = 14;
  constexpr uint8_t LORA_RST   = 26;
  constexpr uint8_t LORA_DIO0  = 25;
}
