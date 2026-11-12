#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <Adafruit_BMP280.h>
#include <esp_system.h>

#include "iot_common.h"
#include "iot_display.h"
#include "iot_pins.h"
#include "iot_provision.h"
#include "plant_config.h"
#include "moisture_sensor.h"

// HARDWARE-READY, PENDING CALIBRATION AND PHYSICAL VALIDATION.
// Use SETDRY and SETWET after measuring the installed sensor in the real pot.

struct Credentials {
  String wifiSsid;
  String wifiPass;
  String mqttHost;
  String mqttUser;
  String mqttPass;
};

Preferences prefs;
Credentials creds;
WiFiClientSecure net;
PubSubClient mqtt(net);
Adafruit_BMP280 bmp;

String clientId;
String topicTelemetry = String("iot/") + PlantConfig::TEAM_ID + "/plant/telemetry";
String topicStatus = String("iot/") + PlantConfig::TEAM_ID + "/plant/status";
String topicAlert = String("iot/") + PlantConfig::TEAM_ID + "/plant/alert";

MoistureReading moisture;
String soilStatus = "UNCALIBRATED";
String resetReasonText;

int rawDry = 0;
int rawWet = 0;
bool bmpOk = false;
bool alertSilenced = false;
bool previousButton = HIGH;
uint8_t consecutiveDry = 0;
uint32_t sequenceNumber = 0;
uint32_t bootCount = 0;
uint32_t wifiReconnects = 0;
uint32_t mqttReconnects = 0;
uint32_t publishOk = 0;
uint32_t publishFail = 0;
uint32_t lastSoilSampleMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastWifiTryMs = 0;
uint32_t lastMqttTryMs = 0;
uint32_t mqttBackoffMs = PlantConfig::MQTT_BACKOFF_START_MS;

String resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON_RESET";
    case ESP_RST_EXT: return "EXTERNAL_RESET";
    case ESP_RST_SW: return "SOFTWARE_RESET";
    case ESP_RST_PANIC: return "PANIC_RESET";
    case ESP_RST_INT_WDT: return "INT_WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
    case ESP_RST_WDT: return "OTHER_WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP_RESET";
    case ESP_RST_BROWNOUT: return "BROWNOUT_RESET";
    default: return "UNKNOWN_RESET";
  }
}

bool loadCredentials() {
  prefs.begin("plant-net", true);
  creds.wifiSsid = prefs.getString("wifi_ssid", "");
  creds.wifiPass = prefs.getString("wifi_pass", "");
  creds.mqttHost = prefs.getString("mqtt_host", "");
  creds.mqttUser = prefs.getString("mqtt_user", "");
  creds.mqttPass = prefs.getString("mqtt_pass", "");
  prefs.end();
  return creds.wifiSsid.length() && creds.mqttHost.length() && creds.mqttUser.length();
}

void saveCredentials() {
  prefs.begin("plant-net", false);
  prefs.putString("wifi_ssid", creds.wifiSsid);
  prefs.putString("wifi_pass", creds.wifiPass);
  prefs.putString("mqtt_host", creds.mqttHost);
  prefs.putString("mqtt_user", creds.mqttUser);
  prefs.putString("mqtt_pass", creds.mqttPass);
  prefs.end();
}

void forgetCredentials() {
  prefs.begin("plant-net", false);
  prefs.clear();
  prefs.end();
}

void printCredentialsSummary() {
  Serial.print("Wi-Fi SSID: "); Serial.println(creds.wifiSsid);
  Serial.print("Wi-Fi password: "); Serial.println(iotMasked(creds.wifiPass));
  Serial.print("MQTT host: "); Serial.println(creds.mqttHost);
  Serial.print("MQTT username: "); Serial.println(creds.mqttUser);
  Serial.print("MQTT password: "); Serial.println(iotMasked(creds.mqttPass));
}

void runProvisioning() {
  Serial.println("Provisioning credentials. Values are stored in NVS, not source code.");
  creds.wifiSsid = iotAsk("Wi-Fi SSID:");
  creds.wifiPass = iotAsk("Wi-Fi password (may be empty for an open network):");
  creds.mqttHost = iotCleanHost(iotAsk("EMQX MQTT host, without scheme or port:"));
  creds.mqttUser = iotAsk("MQTT username:");
  creds.mqttPass = iotAsk("MQTT password:");

  if (creds.wifiSsid.isEmpty() || creds.mqttHost.isEmpty() || creds.mqttUser.isEmpty()) {
    Serial.println("ERROR: SSID, MQTT host and MQTT username are required.");
    return;
  }

  saveCredentials();
  Serial.println("Credentials saved.");
  printCredentialsSummary();
}

void loadPlantSettings() {
  prefs.begin("plant-cfg", false);
  rawDry = prefs.getInt("raw_dry", 0);
  rawWet = prefs.getInt("raw_wet", 0);
  bootCount = prefs.getUInt("boot_count", 0) + 1;
  prefs.putUInt("boot_count", bootCount);
  prefs.end();
}

void saveCalibration() {
  prefs.begin("plant-cfg", false);
  prefs.putInt("raw_dry", rawDry);
  prefs.putInt("raw_wet", rawWet);
  prefs.end();
}

void configureMqtt() {
  // Lab 5 course pattern: traffic is encrypted, but setInsecure() does not verify broker identity.
  // Replace this with a CA certificate before claiming production-grade server authentication.
  net.setInsecure();
  mqtt.setServer(creds.mqttHost.c_str(), PlantConfig::MQTT_PORT);
  mqtt.setKeepAlive(PlantConfig::MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(5);
  mqtt.setBufferSize(768);
}

void startWiFi() {
  if (creds.wifiSsid.isEmpty()) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(creds.wifiSsid.c_str(), creds.wifiPass.c_str());
  Serial.print("Wi-Fi: connecting to ");
  Serial.println(creds.wifiSsid);
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t now = millis();
  if (now - lastWifiTryMs < PlantConfig::WIFI_RETRY_MS) return;
  lastWifiTryMs = now;
  wifiReconnects++;
  WiFi.disconnect();
  startWiFi();
}

bool connectMqtt() {
  Serial.print("MQTT: connecting to ");
  Serial.println(creds.mqttHost);
  bool connected = mqtt.connect(clientId.c_str(),
                                creds.mqttUser.c_str(),
                                creds.mqttPass.c_str(),
                                topicStatus.c_str(),
                                1,
                                true,
                                "offline"); // Retained LWT marks an unclean disconnect.
  if (!connected) return false;

  String online = String("{\"state\":\"online\",\"device_id\":\"") +
                  PlantConfig::DEVICE_ID + "\",\"boot_count\":" + bootCount + "}";
  return mqtt.publish(topicStatus.c_str(), online.c_str(), true);
}

void maintainMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  uint32_t now = millis();
  if (now - lastMqttTryMs < mqttBackoffMs) return;
  lastMqttTryMs = now;

  if (connectMqtt()) {
    Serial.println("MQTT: connected");
    mqttReconnects++;
    mqttBackoffMs = PlantConfig::MQTT_BACKOFF_START_MS;
  } else {
    Serial.print("MQTT: connect failed, state ");
    Serial.println(mqtt.state());
    mqttBackoffMs = min(mqttBackoffMs * 2U, PlantConfig::MQTT_BACKOFF_MAX_MS);
    mqttBackoffMs += millis() % 500; // Jitter prevents many devices retrying simultaneously.
  }
}

String classifySoil(float percent) {
  if (isnan(percent)) return "UNCALIBRATED";
  if (percent < PlantConfig::DRY_THRESHOLD_PCT) return "DRY";
  if (percent < PlantConfig::GETTING_DRY_THRESHOLD_PCT) return "GETTING_DRY";
  if (percent >= PlantConfig::WET_THRESHOLD_PCT) return "WET";
  return "NORMAL";
}

void setIndicators(const String& status) {
  digitalWrite(IoTPins::LED_1, status == "DRY" ? HIGH : LOW);
  digitalWrite(IoTPins::LED_2, status == "GETTING_DRY" || status == "UNCALIBRATED" ? HIGH : LOW);
  digitalWrite(IoTPins::LED_3, status == "NORMAL" || status == "WET" ? HIGH : LOW);
}

void publishAlert(const String& eventName) {
  if (!mqtt.connected()) return;
  char payload[240];
  snprintf(payload, sizeof payload,
           "{\"team\":\"%s\",\"device_id\":\"%s\",\"event\":\"%s\",\"moisture_raw\":%d,\"moisture_pct\":%.1f,\"uptime_s\":%lu}",
           PlantConfig::TEAM_ID,
           PlantConfig::DEVICE_ID,
           eventName.c_str(),
           moisture.raw,
           isnan(moisture.percent) ? -1.0f : moisture.percent,
           (unsigned long)(millis() / 1000));
  mqtt.publish(topicAlert.c_str(), payload, false);
}

void updateSoilState() {
  moisture = readMoisture(rawDry, rawWet);
  String candidate = classifySoil(moisture.percent);

  if (candidate == "DRY") {
    if (consecutiveDry < 255) consecutiveDry++;
    if (consecutiveDry < PlantConfig::DRY_CONFIRMATION_SAMPLES) candidate = soilStatus;
  } else {
    consecutiveDry = 0;
  }

  String oldStatus = soilStatus;
  soilStatus = candidate;
  setIndicators(soilStatus);

  if (soilStatus != oldStatus) {
    Serial.print("Soil status: ");
    Serial.print(oldStatus);
    Serial.print(" -> ");
    Serial.println(soilStatus);
    publishAlert(String("status_") + soilStatus);

    if (soilStatus == "DRY" && !alertSilenced) {
      tone(IoTPins::BUZZER, PlantConfig::BUZZER_FREQUENCY_HZ,
           PlantConfig::BUZZER_DURATION_MS);
    }
    if (soilStatus != "DRY") alertSilenced = false;
  }

  Serial.print("Soil raw=");
  Serial.print(moisture.raw);
  Serial.print(" pct=");
  if (isnan(moisture.percent)) Serial.print("uncalibrated");
  else Serial.print(moisture.percent, 1);
  Serial.print(" status=");
  Serial.println(soilStatus);
}

bool publishTelemetry() {
  if (!mqtt.connected()) return false;

  char temp[16] = "null";
  if (bmpOk) snprintf(temp, sizeof temp, "%.2f", bmp.readTemperature());

  char moisturePct[16] = "null";
  if (!isnan(moisture.percent)) snprintf(moisturePct, sizeof moisturePct, "%.1f", moisture.percent);

  char payload[640];
  snprintf(payload, sizeof payload,
           "{\"team\":\"%s\",\"device_id\":\"%s\",\"boot_count\":%lu,\"seq\":%lu,\"uptime_s\":%lu,\"moisture_raw\":%d,\"moisture_pct\":%s,\"soil_status\":\"%s\",\"temp_c\":%s,\"rssi\":%d,\"wifi_reconnects\":%lu,\"mqtt_reconnects\":%lu,\"reset_reason\":\"%s\",\"raw_dry\":%d,\"raw_wet\":%d}",
           PlantConfig::TEAM_ID,
           PlantConfig::DEVICE_ID,
           (unsigned long)bootCount,
           (unsigned long)++sequenceNumber,
           (unsigned long)(millis() / 1000),
           moisture.raw,
           moisturePct,
           soilStatus.c_str(),
           temp,
           WiFi.RSSI(),
           (unsigned long)wifiReconnects,
           (unsigned long)mqttReconnects,
           resetReasonText.c_str(),
           rawDry,
           rawWet);

  bool ok = mqtt.publish(topicTelemetry.c_str(), payload, false);
  Serial.print(ok ? "MQTT: published " : "MQTT: publish failed ");
  Serial.println(payload);
  return ok;
}

void updateDisplay() {
  String line1 = "Plant T24 " + soilStatus;
  String line2 = "Raw " + String(moisture.raw);
  String line3 = isnan(moisture.percent)
      ? "Moisture: CALIBRATE"
      : "Moisture: " + String(moisture.percent, 1) + "%";
  String line4 = String(WiFi.status() == WL_CONNECTED ? "WiFi " : "NoWiFi ") +
                 (mqtt.connected() ? "MQTT" : "NoMQTT");
  iotDisplayLines(line1, line2, line3, line4);
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  HELP                show this list");
  Serial.println("  STATUS              print sensor, network and reliability status");
  Serial.println("  RAW                 take and print a soil reading now");
  Serial.println("  PUB                 publish telemetry now");
  Serial.println("  SETDRY <raw>        save the measured dry-soil median");
  Serial.println("  SETWET <raw>        save the measured watered-soil median");
  Serial.println("  ACK                 silence the current dry alert");
  Serial.println("  PROVISION           replace Wi-Fi and MQTT credentials");
  Serial.println("  FORGET              erase credentials and restart");
}

void printStatus() {
  Serial.print("Calibration dry/wet: "); Serial.print(rawDry); Serial.print("/"); Serial.println(rawWet);
  Serial.print("Raw/percent/status: "); Serial.print(moisture.raw); Serial.print("/");
  if (isnan(moisture.percent)) Serial.print("null"); else Serial.print(moisture.percent, 1);
  Serial.print("/"); Serial.println(soilStatus);
  Serial.print("Wi-Fi: "); Serial.print(WiFi.status() == WL_CONNECTED ? "connected" : "offline");
  Serial.print("  RSSI: "); Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  Serial.print("MQTT: "); Serial.println(mqtt.connected() ? "connected" : "offline");
  Serial.print("Boot/seq/uptime: "); Serial.print(bootCount); Serial.print("/");
  Serial.print(sequenceNumber); Serial.print("/"); Serial.println(millis() / 1000);
  Serial.print("Reconnects WiFi/MQTT: "); Serial.print(wifiReconnects); Serial.print("/"); Serial.println(mqttReconnects);
}

bool parseRawCommand(const String& line, const char* prefix, int& value) {
  String start(prefix);
  if (!line.startsWith(start)) return false;
  String number = line.substring(start.length());
  number.trim();
  if (number.isEmpty()) return false;
  value = number.toInt();
  return value >= 0 && value <= 4095;
}

void handleCommand(String line) {
  line.trim();
  String upper = line;
  upper.toUpperCase();
  if (upper.isEmpty()) return;

  if (upper == "HELP") { printHelp(); return; }
  if (upper == "STATUS") { printStatus(); return; }
  if (upper == "RAW") { updateSoilState(); return; }
  if (upper == "PUB") {
    if (publishTelemetry()) publishOk++; else publishFail++;
    return;
  }
  if (upper == "ACK") {
    alertSilenced = true;
    noTone(IoTPins::BUZZER);
    Serial.println("OK dry alert silenced until soil leaves DRY");
    return;
  }

  int value = 0;
  if (parseRawCommand(upper, "SETDRY ", value)) {
    rawDry = value;
    saveCalibration();
    Serial.println("OK dry calibration saved");
    updateSoilState();
    return;
  }
  if (parseRawCommand(upper, "SETWET ", value)) {
    rawWet = value;
    saveCalibration();
    Serial.println("OK wet calibration saved");
    updateSoilState();
    return;
  }

  if (upper == "PROVISION") {
    if (mqtt.connected()) mqtt.disconnect();
    runProvisioning();
    ESP.restart();
  }
  if (upper == "FORGET") {
    if (mqtt.connected()) mqtt.disconnect();
    forgetCredentials();
    ESP.restart();
  }
  Serial.println("ERR unknown command. Type HELP");
}

void pollConsole() {
  static String buffer;
  while (Serial.available()) {
    char c = char(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      handleCommand(buffer);
      buffer = "";
    } else if (buffer.length() < 100) {
      buffer += c;
    }
  }
}

void pollButton() {
  bool current = digitalRead(IoTPins::BUTTON_1);
  if (previousButton == HIGH && current == LOW) {
    alertSilenced = true;
    noTone(IoTPins::BUZZER);
    Serial.println("Button 1: dry alert acknowledged");
  }
  previousButton = current;
}

void setup() {
  iotStartSerial("Team 24 Plant Monitor");
  iotInitI2C();
  iotInitSPI();
  iotInitDisplay();

  pinMode(IoTPins::SOIL_MOISTURE_ADC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(IoTPins::SOIL_MOISTURE_ADC, ADC_11db);
  pinMode(IoTPins::LED_1, OUTPUT);
  pinMode(IoTPins::LED_2, OUTPUT);
  pinMode(IoTPins::LED_3, OUTPUT);
  pinMode(IoTPins::BUZZER, OUTPUT);
  pinMode(IoTPins::BUTTON_1, INPUT_PULLUP);

  loadPlantSettings();
  resetReasonText = resetReasonName(esp_reset_reason());
  bmpOk = bmp.begin(IoTPins::BMP280_ADDR);

  uint64_t chip = ESP.getEfuseMac();
  char id[40];
  snprintf(id, sizeof id, "plant-%s-%08lX", PlantConfig::TEAM_ID,
           (unsigned long)(chip & 0xFFFFFFFF));
  clientId = id;

  Serial.print("Client ID: "); Serial.println(clientId);
  Serial.print("Boot count: "); Serial.println(bootCount);
  Serial.print("Reset reason: "); Serial.println(resetReasonText);
  Serial.println(bmpOk ? "BMP280 OK" : "BMP280 not found; temperature will be null");

  updateSoilState();
  updateDisplay();

  if (!loadCredentials()) {
    Serial.println("No credentials stored. Starting provisioning.");
    runProvisioning();
  } else {
    Serial.println("Credentials loaded from NVS. Type PROVISION to replace them.");
  }

  configureMqtt();
  startWiFi();
  printHelp();
}

void loop() {
  pollConsole();
  pollButton();
  maintainWiFi();
  maintainMqtt();
  mqtt.loop(); // PubSubClient needs frequent loop calls for keepalive and socket processing.

  uint32_t now = millis();
  if (now - lastSoilSampleMs >= PlantConfig::SOIL_SAMPLE_MS) {
    lastSoilSampleMs = now;
    updateSoilState();
  }
  if (now - lastDisplayMs >= PlantConfig::DISPLAY_UPDATE_MS) {
    lastDisplayMs = now;
    updateDisplay();
  }
  if (now - lastTelemetryMs >= PlantConfig::TELEMETRY_PERIOD_MS) {
    lastTelemetryMs = now;
    if (publishTelemetry()) publishOk++; else publishFail++;
  }
}
