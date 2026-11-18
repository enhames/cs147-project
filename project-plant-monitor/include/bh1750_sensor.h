#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "iot_pins.h"

struct LightReading {
  uint16_t raw = 0;       // Two BH1750 result bytes combined into one 16-bit count.
  float lux = NAN;        // Physical illuminance after applying the datasheet's /1.2 conversion.
  bool valid = false;     // False when the sensor is absent or an I2C read is incomplete.
};

inline bool bh1750SendCommand(uint8_t address, uint8_t command) {
  Wire.beginTransmission(address);  // Address the BH1750 already found on the shared I2C bus.
  Wire.write(command);              // Send exactly one opcode; the datasheet requires a STOP per opcode.
  return Wire.endTransmission() == 0;  // I2C status 0 means the sensor acknowledged the command.
}

inline bool bh1750AddressResponds(uint8_t address) {
  Wire.beginTransmission(address);  // Address-only transaction safely probes for an ACK.
  return Wire.endTransmission() == 0;
}

inline bool bh1750Begin(uint8_t& activeAddress) {
  // ADDR low selects 0x23; ADDR high selects 0x5C. Supporting both avoids a wiring-only failure.
  if (bh1750AddressResponds(IoTPins::BH1750_ADDR_LOW)) {
    activeAddress = IoTPins::BH1750_ADDR_LOW;
  } else if (bh1750AddressResponds(IoTPins::BH1750_ADDR_HIGH)) {
    activeAddress = IoTPins::BH1750_ADDR_HIGH;
  } else {
    return false;
  }

  // Command 0x10 starts continuous high-resolution measurements at about 1 lx resolution.
  return bh1750SendCommand(activeAddress, IoTPins::BH1750_CONTINUOUS_HIGH_RES_MODE);
}

inline LightReading bh1750Read(uint8_t activeAddress) {
  LightReading result;
  uint8_t received = Wire.requestFrom(activeAddress, uint8_t(2));  // The measurement is high byte then low byte.
  if (received != 2) return result;  // Reject partial I2C data instead of publishing a false light level.

  uint8_t highByte = Wire.read();  // Bits 15:8 arrive first according to the BH1750 read format.
  uint8_t lowByte = Wire.read();   // Bits 7:0 complete the 16-bit measurement.
  result.raw = (uint16_t(highByte) << 8) | lowByte;
  result.lux = result.raw / 1.2f;  // Default H-resolution conversion from the ROHM datasheet.
  result.valid = true;
  return result;
}
