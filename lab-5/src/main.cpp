#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include "iot_common.h"
#include "iot_display.h"
#include "iot_provision.h"

// Lab 5: Cloud communication with MQTT over TLS.
// Complete TODOs 1-6 (search for TODO). The project compiles before you start.
// Pipeline:
//   BMP280 + pot --> ESP32 --(Wi-Fi, TLS, MQTT, port 8883)--> EMQX broker
//                                                               |
//                          Node-RED on your laptop  <-----------+  (subscribes to iot/<TEAM_ID>/#)
//                          dashboard + offline alert
//
// Topics (replace T00 with your team ID):
//   iot/T00/telemetry  JSON, every PERIOD_MS, QoS 0, not retained
//   iot/T00/status     "online" / "offline", retained. "offline" is the Last Will:
//                      the BROKER publishes it for you when your device disappears.
//
// Secrets (Wi-Fi password, MQTT password) are NOT in this file. They are typed once in the
// Serial Monitor and stored in NVS flash with the Preferences library.
//
// Serial commands at startup:  PROVISION (re-enter credentials), FORGET (erase them)
// Serial commands while running: HELP, STATUS, PUB, PROVISION, FORGET

// ---------------- Team settings (not secret) ----------------
#define TEAM_ID "T24"   // your team number from Canvas, e.g. "T07"

constexpr uint16_t MQTT_PORT        = 8883;    // MQTT over TLS
constexpr uint16_t MQTT_KEEPALIVE_S = 15;      // broker declares us dead after ~1.5 x this
constexpr uint32_t WIFI_RETRY_MS    = 20000;   // call WiFi.begin() again if still down
constexpr uint32_t BACKOFF_START_MS = 1000;    // first MQTT retry after 1 s ...
constexpr uint32_t BACKOFF_MAX_MS   = 30000;   // ... doubling up to 30 s

// TODO 6 (part 1): choose the publish period and JUSTIFY it in this comment (or your README).
//   Think about: how fast does room temperature really change? How many messages per day
//   does your choice create (86,400,000 / PERIOD_MS)? What does the 72-hour reliability
//   test cost at this rate? How quickly must the dashboard react?
//   Chosen rate: every 10 s = 8,640 messages/day and 25,920 messages over 72 hours.
//   Room temperature and pressure change slowly, while 10 s still makes the dashboard feel live.
//   Offline detection is controlled by the 15 s keepalive, not by publishing every second.
constexpr uint32_t PERIOD_MS = 10000;

// ---------------- Globals ----------------
struct Credentials {
  String wifiSsid, wifiPass;   // network
  String mqttHost;             // e.g. abc123.s1.eu.hivemq.cloud (no "mqtts://", no port)
  String mqttUser, mqttPass;   // EMQX credential (Access Management tab)
};
Credentials creds;

Preferences prefs;
constexpr const char* NVS_NAMESPACE = "iot-lab5";   // max 15 characters

WiFiClientSecure net;          // TLS socket
PubSubClient mqtt(net);        // MQTT client running on top of the TLS socket

const String TOPIC_TELEMETRY = String("iot/") + TEAM_ID + "/telemetry";
const String TOPIC_STATUS    = String("iot/") + TEAM_ID + "/status";
String clientId;               // "iot-T00-<chip id>", unique per board

Adafruit_BMP280 bmp(&Wire);
bool bmpOk = false;

uint32_t backoffMs   = BACKOFF_START_MS;
uint32_t lastMqttTry = 0;
uint32_t seqNo       = 0;      // message counter: gaps in seq = lost messages
uint32_t pubOk = 0, pubFail = 0, reconnects = 0;

// ============================================================================================
// Part B: credentials in NVS (Preferences)
// ============================================================================================

// Load credentials from NVS into `creds`. Returns true if the required ones are present.
bool loadCredentials() {
  // TODO 1: open the NVS namespace READ-ONLY: prefs.begin(NVS_NAMESPACE, true);
  //         read the five keys into creds with prefs.getString("key", ""):
  //           "wifi_ssid", "wifi_pass", "mqtt_host", "mqtt_user", "mqtt_pass"
  //         then prefs.end(). Return true only if wifi_ssid, mqtt_host and mqtt_user are
  //         not empty (an open Wi-Fi network may have an empty password).
  prefs.begin(NVS_NAMESPACE, true);  // Read-only mode protects stored values while loading.
  creds.wifiSsid = prefs.getString("wifi_ssid", "");
  creds.wifiPass = prefs.getString("wifi_pass", "");
  creds.mqttHost = prefs.getString("mqtt_host", "");
  creds.mqttUser = prefs.getString("mqtt_user", "");
  creds.mqttPass = prefs.getString("mqtt_pass", "");
  prefs.end();

  return creds.wifiSsid.length() > 0 &&
         creds.mqttHost.length() > 0 &&
         creds.mqttUser.length() > 0;
}

// Write `creds` to NVS.
void saveCredentials() {
  // TODO 2: open the namespace read-write (prefs.begin(NVS_NAMESPACE, false)), store the
  //         five values with prefs.putString("key", value) using the SAME key names as
  //         in TODO 1, then prefs.end().
  prefs.begin(NVS_NAMESPACE, false);  // Read-write mode is required during provisioning.
  prefs.putString("wifi_ssid", creds.wifiSsid);
  prefs.putString("wifi_pass", creds.wifiPass);
  prefs.putString("mqtt_host", creds.mqttHost);
  prefs.putString("mqtt_user", creds.mqttUser);
  prefs.putString("mqtt_pass", creds.mqttPass);
  prefs.end();
}

// Erase every key in our namespace (FORGET command). Given.
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

// Ask for every value over Serial and save it. Blocking on purpose (setup / on request). Given.
void runProvisioning() {
  Serial.println();
  Serial.println("=== Provisioning ===");
  Serial.println("Values are stored in this board's NVS flash, never in your source code.");
  Serial.println("Type each value and press Enter.");
  do {
    creds.wifiSsid = iotAsk("Wi-Fi SSID:");
  } while (creds.wifiSsid.length() == 0);
  creds.wifiPass = iotAsk("Wi-Fi password (leave empty for an open network):");
  do {
    creds.mqttHost = iotCleanHost(iotAsk("MQTT host (HiveMQ Cluster URL, e.g. abc123.s1.eu.hivemq.cloud):"));
  } while (creds.mqttHost.length() == 0);
  do {
    creds.mqttUser = iotAsk("MQTT username:");
  } while (creds.mqttUser.length() == 0);
  creds.mqttPass = iotAsk("MQTT password:");

  saveCredentials();
  Serial.println("Saved to NVS:");
  printCredentials();
  Serial.println("Tip: type FORGET at startup to erase them (do this before you return the kit).");
}

// ============================================================================================
// Part C: MQTT over TLS
// ============================================================================================

void configureMqtt() {
  // TODO 3: set up the TLS socket and the MQTT client.
  //   (a) net.setInsecure();  with a LOUD comment saying what it means (see the handout).
  //   (b) mqtt.setServer(creds.mqttHost.c_str(), MQTT_PORT);
  //       (PubSubClient keeps the POINTER, so creds.mqttHost must not change afterwards.)
  //   (c) mqtt.setKeepAlive(MQTT_KEEPALIVE_S); mqtt.setSocketTimeout(5); mqtt.setBufferSize(512);

  // !!! WARNING: LAB 5 ONLY, NOT SECURE !!!
  // setInsecure() means the TLS connection is ENCRYPTED, but we are NOT VERIFYING
  // that the server really is our EMQX broker. Anyone running a fake hotspot could
  // pretend to be the broker and collect our MQTT username and password.
  // Lab 6 fixes this by checking the broker's CA certificate.
  net.setInsecure();

  mqtt.setServer(creds.mqttHost.c_str(), MQTT_PORT);  // EMQX TLS endpoint on port 8883.
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);               // Broker can detect a dead client after keepalive timeout.
  mqtt.setSocketTimeout(5);
  mqtt.setBufferSize(512);
}

const char* mqttStateText(int s) {
  switch (s) {
    case MQTT_CONNECTION_TIMEOUT:      return "-4 CONNECTION_TIMEOUT (no answer: host/port/firewall?)";
    case MQTT_CONNECTION_LOST:         return "-3 CONNECTION_LOST (network dropped)";
    case MQTT_CONNECT_FAILED:          return "-2 CONNECT_FAILED (TCP/TLS failed: host name? Wi-Fi blocks 8883?)";
    case MQTT_DISCONNECTED:            return "-1 DISCONNECTED";
    case MQTT_CONNECTED:               return "0 CONNECTED";
    case MQTT_CONNECT_BAD_PROTOCOL:    return "1 BAD_PROTOCOL";
    case MQTT_CONNECT_BAD_CLIENT_ID:   return "2 BAD_CLIENT_ID";
    case MQTT_CONNECT_UNAVAILABLE:     return "3 UNAVAILABLE (broker busy)";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "4 BAD_CREDENTIALS (username/password)";
    case MQTT_CONNECT_UNAUTHORIZED:    return "5 UNAUTHORIZED (credential lacks permission)";
    default:                           return "unknown";
  }
}

// One connection attempt. Returns true when connected and "online" was published.
bool mqttConnect() {
  Serial.print("MQTT: connecting to "); Serial.print(creds.mqttHost);
  Serial.print(":"); Serial.print(MQTT_PORT); Serial.print(" as "); Serial.println(clientId);

  // TODO 4: connect with a Last Will and Testament (LWT), then announce online.
  //   (a) mqtt.connect(clientId, user, password, willTopic, willQos, willRetain, willMessage)
  //       willTopic = TOPIC_STATUS, willQos = 1, willRetain = true, willMessage = "offline".
  //   (b) publish "online" to TOPIC_STATUS with retained = true, so a dashboard that starts
  //       later still sees the current state. (PubSubClient publishes at QoS 0 only.)
  bool connected = mqtt.connect(clientId.c_str(),
                                creds.mqttUser.c_str(),
                                creds.mqttPass.c_str(),
                                TOPIC_STATUS.c_str(),
                                1,
                                true,
                                "offline");  // Broker publishes this retained Will after an unclean disconnect.
  if (!connected) return false;

  if (!mqtt.publish(TOPIC_STATUS.c_str(), "online", true)) {  // Retain current state for late subscribers.
    mqtt.disconnect();
    return false;
  }
  return true;
}

// Called every pass of loop(). Never blocks for long, except inside one connect attempt.
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

  // TODO 5: reconnect with exponential backoff.
  //   If less than backoffMs has passed since lastMqttTry, return (do NOT block).
  //   Otherwise remember the time, call mqttConnect().
  //     success -> print "MQTT: connected", reset backoffMs to BACKOFF_START_MS, reconnects++
  //     failure -> print mqttStateText(mqtt.state()), double backoffMs (max BACKOFF_MAX_MS)
  //                and add a little jitter, e.g. + (millis() % 500), so 40 teams do not all
  //                retry at the same instant after the router reboots.
  uint32_t now = millis();
  if (now - lastMqttTry < backoffMs) return;  // Non-blocking retry delay keeps mqtt.loop() and display responsive.
  lastMqttTry = now;

  if (mqttConnect()) {
    Serial.println("MQTT: connected");
    backoffMs = BACKOFF_START_MS;
    reconnects++;
  } else {
    Serial.print("MQTT: failed, state ");
    Serial.println(mqttStateText(mqtt.state()));
    backoffMs = min(backoffMs * 2U, BACKOFF_MAX_MS) + (millis() % 500);  // Jitter prevents reconnect storms.
  }
}

int readPotPercent() {
  return constrain((int)map(analogRead(IoTPins::POT_ADC), 0, 4095, 0, 100), 0, 100);
}

// Build one JSON message and publish it. Returns true if PubSubClient accepted it.
bool publishTelemetry() {
  if (!mqtt.connected()) return false;

  char temp[16] = "null", press[16] = "null";   // JSON null if the sensor is missing
  if (bmpOk) {
    snprintf(temp,  sizeof temp,  "%.2f", bmp.readTemperature());
    snprintf(press, sizeof press, "%.2f", bmp.readPressure() / 100.0f);
  }

  // TODO 6 (part 2): format the JSON payload into buf with snprintf, e.g.
  //   {"seq":42,"temp_c":23.41,"press_hpa":1012.60,"pot_pct":37,"rssi":-61,"uptime_s":420}
  //   (temp and press are already strings; use readPotPercent(), WiFi.RSSI(), millis()/1000,
  //    and ++seqNo). Then publish it to TOPIC_TELEMETRY, NOT retained, and return the result.
  char buf[192];
  snprintf(buf, sizeof buf,
           "{\"seq\":%lu,\"temp_c\":%s,\"press_hpa\":%s,\"pot_pct\":%d,\"rssi\":%d,\"uptime_s\":%lu}",
           (unsigned long)++seqNo,
           temp,
           press,
           readPotPercent(),
           (int)WiFi.RSSI(),
           (unsigned long)(millis() / 1000));

  bool accepted = mqtt.publish(TOPIC_TELEMETRY.c_str(), buf, false);  // Telemetry is live data, so do not retain it.
  Serial.print(accepted ? "MQTT: published " : "MQTT: publish failed ");
  Serial.println(buf);
  return accepted;
}

// ============================================================================================
// Serial console (given)
// ============================================================================================

void printHelp() {
  Serial.println("Commands: HELP | STATUS | PUB (publish now) | PROVISION | FORGET");
}

void printStatus() {
  Serial.print("Team "); Serial.print(TEAM_ID); Serial.print("  client id "); Serial.println(clientId);
  printCredentials();
  Serial.print("  Wi-Fi: "); Serial.println(WiFi.status() == WL_CONNECTED ? "connected" : "down");
  Serial.print("  MQTT : "); Serial.println(mqttStateText(mqtt.state()));
  Serial.print("  published ok/failed: "); Serial.print(pubOk); Serial.print("/"); Serial.print(pubFail);
  Serial.print("  reconnects: "); Serial.println(reconnects);
  Serial.print("  period: "); Serial.print(PERIOD_MS); Serial.println(" ms");
}

// A clean DISCONNECT tells the broker "I left on purpose", so it does NOT send the Last Will.
// That is why we publish "offline" ourselves before a planned disconnect.
void goOfflineCleanly() {
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_STATUS.c_str(), "offline", true);
    mqtt.disconnect();
  }
}

void handleCommand(String line) {
  line.trim();
  line.toUpperCase();
  if (line.length() == 0) return;
  if (line == "HELP")   { printHelp(); return; }
  if (line == "STATUS") { printStatus(); return; }
  if (line == "PUB") {
    if (!mqtt.connected()) { Serial.println("ERR not connected to the broker"); return; }
    if (publishTelemetry()) pubOk++; else pubFail++;
    return;
  }
  if (line == "FORGET") { goOfflineCleanly(); forgetCredentials(); Serial.println("Rebooting..."); ESP.restart(); return; }
  if (line == "PROVISION") { goOfflineCleanly(); runProvisioning(); Serial.println("Rebooting..."); ESP.restart(); return; }
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
  iotStartSerial("Lab 5: Cloud Communication");
  iotInitI2C();
  iotInitSPI();
  iotInitDisplay();
  pinMode(IoTPins::POT_ADC, INPUT);

  bmpOk = bmp.begin(IoTPins::BMP280_ADDR);
  Serial.println(bmpOk ? "BMP280 OK at 0x76" : "BMP280 not found (temp/pressure will be null)");

  uint64_t chip = ESP.getEfuseMac();
  char idbuf[32];
  snprintf(idbuf, sizeof idbuf, "iot-%s-%08lX", TEAM_ID, (unsigned long)(chip & 0xFFFFFFFF));
  clientId = idbuf;

  // Startup window: 3 s to type PROVISION or FORGET.
  Serial.println("Type PROVISION (new credentials) or FORGET (erase) within 3 s...");
  iotDisplayLines("Lab 5 cloud", "PROVISION/FORGET?", "(3 s, Serial)");
  String cmd = iotReadLine(3000);
  cmd.toUpperCase();
  if (cmd == "FORGET") forgetCredentials();

  bool haveCreds = loadCredentials();
  if (cmd == "PROVISION" || !haveCreds) {
    if (!haveCreds) Serial.println("No credentials in NVS yet.");
    iotDisplayLines("Lab 5 cloud", "Provisioning:", "use Serial Monitor");
    runProvisioning();
  } else {
    Serial.println("Credentials loaded from NVS:");
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
  mqtt.loop();   // EVERY pass: sends keepalive pings, reads incoming packets

  static uint32_t lastPub = 0;
  if (iotEvery(PERIOD_MS, lastPub)) {
    if (mqtt.connected()) {
      if (publishTelemetry()) pubOk++; else pubFail++;
    }
  }

  static uint32_t lastDraw = 0;
  if (iotEvery(1000, lastDraw)) {
    const bool w = (WiFi.status() == WL_CONNECTED);
    String t = bmpOk ? String(bmp.readTemperature(), 1) + " C" : String("no BMP280");
    iotDisplayLines(String("Lab 5  ") + TEAM_ID,
                    w ? "WiFi " + String((int)WiFi.RSSI()) + " dBm" : String("WiFi: down"),
                    mqtt.connected() ? String("MQTT: online") : String("MQTT: retrying"),
                    t + "  sent " + String((unsigned long)pubOk));
  }
}
