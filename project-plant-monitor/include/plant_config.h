#pragma once
#include <Arduino.h>

namespace PlantConfig {
  constexpr char TEAM_ID[] = "T24";
  constexpr char DEVICE_ID[] = "plant-monitor-01";

  constexpr uint32_t SOIL_SAMPLE_MS = 5000;
  constexpr uint32_t DISPLAY_UPDATE_MS = 1000;
  constexpr uint32_t TELEMETRY_PERIOD_MS = 60000;
  constexpr uint32_t WIFI_RETRY_MS = 10000;
  constexpr uint32_t MQTT_BACKOFF_START_MS = 1000;
  constexpr uint32_t MQTT_BACKOFF_MAX_MS = 30000;
  constexpr uint16_t MQTT_KEEPALIVE_S = 15;
  constexpr uint16_t MQTT_PORT = 8883;

  constexpr uint8_t ADC_SAMPLES = 15;
  constexpr int MIN_CALIBRATION_SPAN = 100;

  constexpr float DRY_THRESHOLD_PCT = 30.0f;
  constexpr float GETTING_DRY_THRESHOLD_PCT = 40.0f;
  constexpr float WET_THRESHOLD_PCT = 80.0f;
  constexpr uint8_t DRY_CONFIRMATION_SAMPLES = 5;
  constexpr uint16_t BUZZER_FREQUENCY_HZ = 2400;
  constexpr uint16_t BUZZER_DURATION_MS = 180;
}
