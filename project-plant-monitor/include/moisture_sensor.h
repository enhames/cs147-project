#pragma once
#include <Arduino.h>
#include "iot_pins.h"
#include "plant_config.h"

struct MoistureReading {
  int raw = 0;
  float percent = NAN;
  bool calibrated = false;
};

inline void sortSamples(int* values, uint8_t count) {
  for (uint8_t i = 1; i < count; ++i) {
    int key = values[i];
    int j = i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
}

inline int readSoilMedian() {
  int samples[PlantConfig::ADC_SAMPLES];
  for (uint8_t i = 0; i < PlantConfig::ADC_SAMPLES; ++i) {
    samples[i] = analogRead(IoTPins::SOIL_MOISTURE_ADC);
  }
  sortSamples(samples, PlantConfig::ADC_SAMPLES);
  return samples[PlantConfig::ADC_SAMPLES / 2];
}

inline MoistureReading readMoisture(int rawDry, int rawWet) {
  MoistureReading result;
  result.raw = readSoilMedian();
  result.calibrated = abs(rawWet - rawDry) >= PlantConfig::MIN_CALIBRATION_SPAN;
  if (!result.calibrated) return result;

  // This formula works whether the wet reference is numerically higher or lower.
  result.percent = 100.0f * (result.raw - rawDry) / float(rawWet - rawDry);
  result.percent = constrain(result.percent, 0.0f, 100.0f);
  return result;
}
