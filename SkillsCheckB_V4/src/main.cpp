#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <Wire.h>
#include "iot_common.h"
#include "iot_provision.h"
#include "iot_ca_certs.h"

// IoT@UCI Skills Check B: light-level node.
// Reads a BH1750 ambient light sensor over I2C (Wire only), publishes lux to your team's
// HiveMQ Cloud cluster over MQTT/TLS, and moves the servo when it receives a command.
// It reuses the credentials your board already has in NVS from Labs 5 and 6.
//
// Wiring (you do this): BH1750 / GY-302 board -> kit. Use its datasheet and the board labels.
//
// Topics (replace T00 with your team ID):
//   iot/T00/lux      {"seq":..,"lux":..}        every PERIOD_MS
//   iot/T00/cmd      "servo:<angle>"           moves the servo
//   iot/T00/status   "online" / "offline"      retained, "offline" is the Last Will
//
// Serial commands: HELP, STATUS, LUX, PROVISION, FORGET
//
// This device has ONE defect. Find it, fix it, and keep the device working.

// ---------------- Team settings (not secret) ----------------
#define TEAM_ID "T24"   // your team number from Canvas, e.g. "T07"

constexpr uint16_t MQTT_PORT        = 8883;    // MQTT over TLS
constexpr uint16_t MQTT_KEEPALIVE_S = 15;
constexpr uint32_t WIFI_RETRY_MS    = 20000;
constexpr uint32_t BACKOFF_START_MS = 1000;
constexpr uint32_t BACKOFF_MAX_MS   = 30000;
constexpr uint32_t PERIOD_MS        = 5000;

// ---------------- BH1750 (see its datasheet) ----------------
constexpr uint8_t BH1750_ADDR      = 0x23;     // ADDR pin low or floating
constexpr uint8_t BH1750_POWER_ON  = 0x01;
constexpr uint8_t BH1750_MODE      = 0x13;     // Continuously H-Resolution Mode (1 lx, 120 ms)

// ---------------- Servo (LEDC PWM, 50 Hz) ----------------
constexpr uint8_t  SERVO_CH     = 0;
constexpr uint32_t SERVO_HZ     = 50;
constexpr uint8_t  SERVO_BITS   = 16;
constexpr uint32_t SERVO_MIN_US = 500;
constexpr uint32_t SERVO_MAX_US = 2500;

struct Credentials {
  String wifiSsid, wifiPass;
  String mqttHost;
  String mqttUser, mqttPass;
};
Credentials creds;

Preferences prefs;
constexpr const char* NVS_NAMESPACE = "iot-lab5";   // same as Labs 5 and 6

WiFiClientSecure net;
PubSubClient mqtt(net);

const String TOPIC_LUX    = String("iot/") + TEAM_ID + "/lux";
const String TOPIC_CMD    = String("iot/") + TEAM_ID + "/cmd";
const String TOPIC_STATUS = String("iot/") + TEAM_ID + "/status";
String clientId;

bool  bhOk = false;
float lastLux = -1;
int   servoAngle = 90;

uint32_t backoffMs   = BACKOFF_START_MS;
uint32_t lastMqttTry = 0;
uint32_t seqNo = 0, pubOk = 0, pubFail = 0, cmdCount = 0;

// ============================================================================================
// BH1750 light sensor (Wire only)
// ============================================================================================

bool bhWrite(uint8_t opcode) {
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write(opcode);
  return Wire.endTransmission() == 0;
}

bool bhBegin() {
  if (!bhWrite(BH1750_POWER_ON)) return false;
  if (!bhWrite(BH1750_MODE)) return false;
  delay(180);   // first measurement (max 180 ms)
  return true;
}

// Returns lux, or -1 if the sensor did not answer.
float bhReadLux() {
  if (Wire.requestFrom(BH1750_ADDR, (uint8_t)2) != 2) return -1;
  uint16_t raw = ((uint16_t)Wire.read() << 8) | Wire.read();
  return raw;          // count is lux
}

// ============================================================================================
// Servo
// ============================================================================================

void servoWrite(int angle) {
  servoAngle = angle;
  const uint32_t us = SERVO_MIN_US + (uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * angle / 180;
  const uint32_t duty = (uint32_t)((uint64_t)us * ((1UL << SERVO_BITS) - 1) * SERVO_HZ / 1000000UL);
  ledcWrite(SERVO_CH, duty);
}

// ============================================================================================
// Credentials in NVS (from Lab 5)
// ============================================================================================

bool loadCredentials() {
  prefs.begin(NVS_NAMESPACE, true);
  creds.wifiSsid = prefs.getString("wifi_ssid", "");
  creds.wifiPass = prefs.getString("wifi_pass", "");
  creds.mqttHost = prefs.getString("mqtt_host", "");
  creds.mqttUser = prefs.getString("mqtt_user", "");
  creds.mqttPass = prefs.getString("mqtt_pass", "");
  prefs.end();

  return creds.wifiSsid.length() > 0 && creds.mqttHost.length() > 0 &&
         creds.mqttUser.length() > 0;
}

void saveCredentials() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("wifi_ssid", creds.wifiSsid);
  prefs.putString("wifi_pass", creds.wifiPass);
  prefs.putString("mqtt_host", creds.mqttHost);
  prefs.putString("mqtt_user", creds.mqttUser);
  prefs.putString("mqtt_pass", creds.mqttPass);
  prefs.end();
}

void forgetCredentials() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
  creds = Credentials();
  Serial.println("Credentials erased from NVS.");
}

void printCredentials() {
  Serial.print("  Wi-Fi SSID    : "); Serial.println(creds.wifiSsid);
  Serial.print("  Wi-Fi password: "); Serial.println(iotMasked(creds.wifiPass));
  Serial.print("  MQTT host     : "); Serial.println(creds.mqttHost);
  Serial.print("  MQTT username : "); Serial.println(creds.mqttUser);
  Serial.print("  MQTT password : "); Serial.println(iotMasked(creds.mqttPass));
}

void runProvisioning() {
  Serial.println();
  Serial.println("=== Provisioning ===");
  do { creds.wifiSsid = iotAsk("Wi-Fi SSID:"); } while (creds.wifiSsid.length() == 0);
  creds.wifiPass = iotAsk("Wi-Fi password (leave empty for an open network):");
  do {
    creds.mqttHost = iotCleanHost(iotAsk("MQTT host (HiveMQ Cluster URL):"));
  } while (creds.mqttHost.length() == 0);
  do { creds.mqttUser = iotAsk("MQTT username:"); } while (creds.mqttUser.length() == 0);
  creds.mqttPass = iotAsk("MQTT password:");
  saveCredentials();
  Serial.println("Saved to NVS:");
  printCredentials();
}

// ============================================================================================
// MQTT over TLS
// ============================================================================================

void onMessage(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len && i < 64; i++) msg += (char)payload[i];
  msg.trim();
  Serial.print("CMD ");
  Serial.print(topic);
  Serial.print("  ");
  Serial.println(msg);
  if (TOPIC_CMD != topic) return;                      // only our own command topic
  if (!msg.startsWith("servo:")) { Serial.println("CMD rejected: unknown command"); return; }
  const String arg = msg.substring(6);
  for (size_t i = 0; i < arg.length(); i++) {
    if (!isDigit(arg[i])) { Serial.println("CMD rejected: angle is not a number"); return; }
  }
  const int angle = arg.toInt();
  if (arg.length() == 0 || angle < 0 || angle > 180) { Serial.println("CMD rejected: angle out of 0..180"); return; }
  servoWrite(angle);
  cmdCount++;
}

void configureMqtt() {
  net.setCACert(EMQX_ROOT_CA_PEM);   // verify the broker certificate (ISRG Root X1)
  mqtt.setServer(creds.mqttHost.c_str(), MQTT_PORT);
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(5);
  mqtt.setCallback(onMessage);
}

bool mqttConnect() {
  Serial.print("MQTT: connecting to ");
  Serial.print(creds.mqttHost);
  Serial.print(":");
  Serial.println(MQTT_PORT);
  bool ok = mqtt.connect(clientId.c_str(), creds.mqttUser.c_str(), creds.mqttPass.c_str(),
                         TOPIC_STATUS.c_str(), 1, true, "offline");
  if (!ok) {
    char err[128];
    int code = net.lastError(err, sizeof err);
    if (code != 0) { Serial.print("TLS error "); Serial.print(code); Serial.print(": "); Serial.println(err); }
    return false;
  }
  mqtt.publish(TOPIC_STATUS.c_str(), "online", true);
  mqtt.subscribe(TOPIC_CMD.c_str(), 1);
  return true;
}

void maintainWiFi() {
  static uint32_t lastBegin = 0;
  static bool started = false, wasUp = false;
  const bool up = (WiFi.status() == WL_CONNECTED);
  if (up && !wasUp) { Serial.print("Wi-Fi: connected, IP "); Serial.println(WiFi.localIP()); }
  if (!up && wasUp) Serial.println("Wi-Fi: connection lost");
  wasUp = up;
  if (!up && (!started || millis() - lastBegin >= WIFI_RETRY_MS)) {
    started = true;
    lastBegin = millis();
    Serial.print("Wi-Fi: connecting to ");
    Serial.println(creds.wifiSsid);
    WiFi.begin(creds.wifiSsid.c_str(), creds.wifiPass.c_str());
  }
}

void maintainMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  const uint32_t now = millis();
  if (now - lastMqttTry < backoffMs) return;
  lastMqttTry = now;
  if (mqttConnect()) {
    Serial.println("MQTT: connected");
    backoffMs = BACKOFF_START_MS;
  } else {
    Serial.print("MQTT: failed, state ");
    Serial.println(mqtt.state());
    backoffMs = std::min<uint32_t>(backoffMs * 2, BACKOFF_MAX_MS) + (millis() % 500);
  }
}

bool publishLux() {
  if (!mqtt.connected()) return false;
  char buf[96];
  if (lastLux < 0) snprintf(buf, sizeof buf, "{\"seq\":%lu,\"lux\":null}", (unsigned long)(++seqNo));
  else snprintf(buf, sizeof buf, "{\"seq\":%lu,\"lux\":%.1f}", (unsigned long)(++seqNo), lastLux);
  bool ok = mqtt.publish(TOPIC_LUX.c_str(), buf, false);
  Serial.print(ok ? "PUB " : "PUB FAILED ");
  Serial.print(TOPIC_LUX);
  Serial.print("  ");
  Serial.println(buf);
  return ok;
}

// ============================================================================================
// Serial console
// ============================================================================================

void printHelp() { Serial.println("Commands: HELP | STATUS | LUX | PROVISION | FORGET"); }

void printStatus() {
  Serial.print("Team "); Serial.print(TEAM_ID); Serial.print("  client id "); Serial.println(clientId);
  printCredentials();
  Serial.print("  Wi-Fi : "); Serial.println(WiFi.status() == WL_CONNECTED ? "connected" : "down");
  Serial.print("  MQTT  : "); Serial.println(mqtt.connected() ? "connected" : "down");
  Serial.print("  BH1750: "); Serial.println(bhOk ? "ok" : "not found");
  Serial.print("  lux   : "); Serial.println(lastLux, 1);
  Serial.print("  servo : "); Serial.println(servoAngle);
  Serial.print("  published ok/failed: "); Serial.print(pubOk); Serial.print("/"); Serial.print(pubFail);
  Serial.print("  commands: "); Serial.println(cmdCount);
}

void goOfflineCleanly() {
  if (mqtt.connected()) { mqtt.publish(TOPIC_STATUS.c_str(), "offline", true); mqtt.disconnect(); }
}

void handleCommand(String line) {
  line.trim();
  line.toUpperCase();
  if (line.length() == 0) return;
  if (line == "HELP")   { printHelp(); return; }
  if (line == "STATUS") { printStatus(); return; }
  if (line == "LUX")    { lastLux = bhReadLux(); Serial.print("lux "); Serial.println(lastLux, 1); return; }
  if (line == "FORGET") { goOfflineCleanly(); forgetCredentials(); ESP.restart(); return; }
  if (line == "PROVISION") { goOfflineCleanly(); runProvisioning(); ESP.restart(); return; }
  Serial.println("ERR unknown command. Type HELP");
}

void pollConsole() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { handleCommand(buf); buf = ""; continue; }
    if (buf.length() < 60) buf += c;
  }
}

// ============================================================================================

void setup() {
  iotStartSerial("Skills Check B");
  iotInitI2C();

  ledcSetup(SERVO_CH, SERVO_HZ, SERVO_BITS);
  ledcAttachPin(IoTPins::SERVO_PWM, SERVO_CH);
  servoWrite(90);

  bhOk = bhBegin();
  Serial.println(bhOk ? "BH1750 OK at 0x23" : "BH1750 not found: check your wiring");

  uint64_t chip = ESP.getEfuseMac();
  char idbuf[32];
  snprintf(idbuf, sizeof idbuf, "iot-%s-%08lX", TEAM_ID, (unsigned long)(chip & 0xFFFFFFFF));
  clientId = idbuf;

  Serial.println("Type PROVISION (new credentials) or FORGET (erase) within 3 s...");
  String cmd = iotReadLine(3000);
  cmd.toUpperCase();
  if (cmd == "FORGET") forgetCredentials();

  bool haveCreds = loadCredentials();
  if (cmd == "PROVISION" || !haveCreds) {
    if (!haveCreds) Serial.println("No credentials in NVS yet.");
    runProvisioning();
  } else {
    Serial.println("Credentials loaded:");
    printCredentials();
  }

  WiFi.mode(WIFI_STA);
  configureMqtt();
  printHelp();
}

void loop() {
  pollConsole();
  maintainWiFi();
  maintainMqtt();
  mqtt.loop();

  static uint32_t lastPub = 0;
  if (iotEvery(PERIOD_MS, lastPub)) {
    lastLux = bhOk ? bhReadLux() : -1;
    if (mqtt.connected()) { if (publishLux()) pubOk++; else pubFail++; }
  }
}
