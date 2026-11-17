#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <SparkFun_ATECCX08a_Arduino_Library.h>
#include <mbedtls/md.h>
#include "iot_common.h"
#include "iot_display.h"
#include "iot_provision.h"
#include "iot_ca_certs.h"

// Lab 6 STARTER. Harden It. Complete TODOs 1-6 (TODO 1 is in include/iot_ca_certs.h).
// Starts from Lab 5 (BMP280 + pot -> HiveMQ Cloud, MQTT over TLS, credentials in NVS,
// Last Will on iot/<TEAM_ID>/status) and hardens it:
//   Part A: TLS with the broker certificate VERIFIED against ISRG Root X1 (setCACert).
//   Part B: credentials stay in NVS, typed once over Serial, never in the repository.
//   Part C: every reading is signed with the device key inside the ATECC608A (slot 0).
//           This code only READS the chip's configuration. It never writes the config zone
//           and never locks anything: a chip that is not provisioned is reported, not "fixed".
//
// Topics (replace T00 with your team ID):
//   iot/T00/telemetry      JSON, every PERIOD_MS (the exact bytes that get signed)
//   iot/T00/telemetry/sig  {"seq":42,"alg":"ES256","sig":"<128 hex chars = r||s>"}
//   iot/T00/pubkey         <128 hex chars = X||Y>, retained: the P-256 public key of slot 0
//   iot/T00/status         "online" / "offline", retained, "offline" is the Last Will
//
// Serial commands at startup:  PROVISION (re-enter credentials), FORGET (erase them)
// Serial commands while running: HELP, STATUS, PUB, PUBKEY, CA GOOD, CA WRONG, PROVISION, FORGET

// ---------------- Team settings (not secret) ----------------
#define TEAM_ID "T24"   // your team number from Canvas, e.g. "T07"

constexpr uint16_t MQTT_PORT        = 8883;    // MQTT over TLS
constexpr uint16_t MQTT_KEEPALIVE_S = 15;
constexpr uint32_t WIFI_RETRY_MS    = 20000;
constexpr uint32_t BACKOFF_START_MS = 1000;
constexpr uint32_t BACKOFF_MAX_MS   = 30000;
constexpr uint32_t PERIOD_MS        = 10000;   // same rate as Lab 5 (justified there)

// ---------------- Globals ----------------
struct Credentials {
  String wifiSsid, wifiPass;
  String mqttHost;
  String mqttUser, mqttPass;
};
Credentials creds;

Preferences prefs;
// Same namespace as Lab 5, so a board provisioned in Lab 5 keeps its credentials.
constexpr const char* NVS_NAMESPACE = "iot-lab5";

WiFiClientSecure net;          // TLS socket
PubSubClient mqtt(net);        // MQTT on top of the TLS socket

const String TOPIC_TELEMETRY = String("iot/") + TEAM_ID + "/telemetry";
const String TOPIC_SIG       = String("iot/") + TEAM_ID + "/telemetry/sig";
const String TOPIC_PUBKEY    = String("iot/") + TEAM_ID + "/pubkey";
const String TOPIC_STATUS    = String("iot/") + TEAM_ID + "/status";
String clientId;

Adafruit_BMP280 bmp(&Wire);
bool bmpOk = false;

ATECCX08A atecc;
bool eccSigning = false;       // true only if the chip is locked (provisioned) and we read the key
uint8_t devicePubKey[64];      // X||Y of the slot 0 key
uint32_t sigOk = 0, sigFail = 0;

bool useWrongCa = false;       // "CA WRONG" demo: verification must make the handshake fail

uint32_t backoffMs   = BACKOFF_START_MS;
uint32_t lastMqttTry = 0;
uint32_t seqNo       = 0;
uint32_t pubOk = 0, pubFail = 0, reconnects = 0;

// Bytes -> lowercase hex. out needs 2*len+1 chars.
void toHex(const uint8_t* data, size_t len, char* out) {
  static const char* digits = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[2 * i]     = digits[data[i] >> 4];
    out[2 * i + 1] = digits[data[i] & 0x0F];
  }
  out[2 * len] = '\0';
}

// ============================================================================================
// Part B: credentials in NVS (from Lab 5, unchanged)
// ============================================================================================

bool loadCredentials() {
  prefs.begin(NVS_NAMESPACE, true);    // read-only
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
  Serial.println("Values are stored in this board's NVS flash, never in your source code.");
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
}

// ============================================================================================
// Part A: MQTT over TLS, broker certificate verified
// ============================================================================================

void configureMqtt() {
  // TODO 1 is in include/iot_ca_certs.h: the ISRG Root X1 certificate (PEM).

  // TODO 2: verify the broker. Give the TLS socket the root CA that signed the broker's
  //         chain with net.setCACert(...). Delete the Lab 5 insecure line completely.
  //         The ESP32 then checks the chain up to this root AND that the certificate name
  //         matches creds.mqttHost. If either check fails, the handshake fails.
  //         Use WRONG_CA_PEM instead when useWrongCa is true (console command CA WRONG),
  //         and print which CA you are using.
  // From Lab 5: encrypted but the broker is NOT verified. Replace this line.
  if (useWrongCa) {
    Serial.println("TLS: using WRONG CA (DigiCert Global Root G2); connection should fail");
    net.setCACert(WRONG_CA_PEM);  // Trust the intentionally unrelated root so the TLS handshake proves verification can reject the broker.
  } else {
    Serial.println("TLS: using ISRG Root X1");
    net.setCACert(ISRG_ROOT_X1_PEM);  // Trust ISRG Root X1 so TLS can authenticate the intended HiveMQ/Let's Encrypt certificate chain.
  }

  mqtt.setServer(creds.mqttHost.c_str(), MQTT_PORT);   // PubSubClient keeps this host pointer and connects to MQTT-over-TLS port 8883.
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);  // Periodic MQTT keepalives let the broker detect an unexpectedly disconnected device.
  mqtt.setSocketTimeout(5);  // Stop a failed network operation after five seconds instead of blocking the device for too long.
  mqtt.setBufferSize(512);   // Reserve enough MQTT packet space for telemetry and the 128-character hexadecimal signature.
}

const char* mqttStateText(int s) {
  switch (s) {
    case MQTT_CONNECTION_TIMEOUT:      return "-4 CONNECTION_TIMEOUT (no answer: host/port/firewall?)";
    case MQTT_CONNECTION_LOST:         return "-3 CONNECTION_LOST (network dropped)";
    case MQTT_CONNECT_FAILED:          return "-2 CONNECT_FAILED (TCP/TLS failed: see the TLS error line)";
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

bool mqttConnect() {
  Serial.print("MQTT: connecting to ");
  Serial.print(creds.mqttHost);
  Serial.print(":");
  Serial.print(MQTT_PORT);
  Serial.print(" as ");
  Serial.println(clientId);
  bool ok = mqtt.connect(clientId.c_str(),
                         creds.mqttUser.c_str(), creds.mqttPass.c_str(),
                         TOPIC_STATUS.c_str(), 1, true, "offline");
  if (!ok) {
    // Why did TLS fail? With the wrong CA this prints an X509 verification error.
    char err[128];
    int code = net.lastError(err, sizeof err);
    if (code != 0) {
      Serial.print("TLS error ");
      Serial.print(code);
      Serial.print(": ");
      Serial.println(err);
    }
    return false;
  }
  mqtt.publish(TOPIC_STATUS.c_str(), "online", true);
  if (eccSigning) {
    char hex[129];
    toHex(devicePubKey, 64, hex);
    mqtt.publish(TOPIC_PUBKEY.c_str(), hex, true);   // retained, so a verifier can find it
  }
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
    Serial.println("MQTT: connected over TLS, broker certificate verified");
    backoffMs = BACKOFF_START_MS;
    reconnects++;
  } else {
    Serial.print("MQTT: failed, state ");
    Serial.println(mqttStateText(mqtt.state()));
    backoffMs = std::min<uint32_t>(backoffMs * 2, BACKOFF_MAX_MS) + (millis() % 500);
    Serial.print("MQTT: next try in ");
    Serial.print(backoffMs / 1000.0f, 1);
    Serial.println(" s");
  }
}

// ============================================================================================
// Part C: device key on the ATECC608A (read-only use, never lock anything)
// ============================================================================================

void initSecureElement() {
  if (!atecc.begin(IoTPins::ATECC608A_ADDR, Wire, Serial)) {
    Serial.println("ATECC608A not found at 0x60 (check the Qwiic cable). Continuing without signing.");
    return;
  }

  // TODO 3: read the configuration zone (a READ, it changes nothing) with
  //         atecc.readConfigZone(false), then check atecc.configLockStatus and
  //         atecc.dataOTPLockStatus. If either is false, print exactly
  //         "ATECC608A NOT PROVISIONED — ask a TA (do not lock it yourself)" and return.
  //         NEVER call writeConfigSparkFun(), lockConfig(), lockDataAndOTP(), lockDataSlot0(),
  //         lock() or createNewKeyPair(): locking is permanent and the kit is reused.
  //         Also print the serial number (atecc.serialNumber, SERIAL_NUMBER_SIZE bytes, use toHex)
  //         and the three lock flags (configLockStatus, dataOTPLockStatus, slot0LockStatus).
  //         Replace the two placeholder lines below.
  if (!atecc.readConfigZone(false)) {  // Read the secure-element configuration without changing keys, slots, or irreversible lock bits.
    Serial.println("ATECC608A config read failed. Continuing without signing.");
    return;
  }

  char serialHex[SERIAL_NUMBER_SIZE * 2 + 1];
  toHex(atecc.serialNumber, SERIAL_NUMBER_SIZE, serialHex);  // Convert the chip identity bytes into printable hexadecimal.
  Serial.print("ATECC608A serial ");
  Serial.println(serialHex);
  Serial.print("  config zone locked: ");
  Serial.println(atecc.configLockStatus ? "yes" : "no");
  Serial.print("  data/OTP locked   : ");
  Serial.println(atecc.dataOTPLockStatus ? "yes" : "no");
  Serial.print("  slot 0 locked     : ");
  Serial.println(atecc.slot0LockStatus ? "yes" : "no");

  if (!atecc.configLockStatus || !atecc.dataOTPLockStatus) {  // Refuse signing when staff provisioning and permanent protection are incomplete.
    Serial.println("ATECC608A NOT PROVISIONED - ask a TA; do not lock it yourself.");
    return;
  }

  // TODO 4: read the public key that matches the private key in slot 0 with
  //         atecc.generatePublicKey(0, false). It lands in atecc.publicKey64Bytes (X||Y).
  //         Copy it to devicePubKey, print it as hex, and set eccSigning = true.
  //         (generatePublicKey computes the public half; the private key never leaves the chip.)
  if (!atecc.generatePublicKey(0, false)) {  // Derive the public half of slot 0 without exporting or replacing its private key.
    Serial.println("ATECC608A public-key read failed. Continuing without signing.");
    return;
  }
  memcpy(devicePubKey, atecc.publicKey64Bytes, sizeof devicePubKey);  // Save the 64-byte P-256 X||Y public key for MQTT publication.
  char pubKeyHex[129];
  toHex(devicePubKey, sizeof devicePubKey, pubKeyHex);  // Encode binary key bytes because MQTT payloads and the verifier use hexadecimal text.
  Serial.println("Device public key (P-256, X|Y, slot 0):");
  Serial.println(pubKeyHex);
  eccSigning = true;  // Enable signed telemetry only after the protected chip and slot-0 public key pass inspection.

}

// Sign the exact payload bytes. sigHex needs 129 chars. Returns false on any error.
bool signPayload(const char* payload, char* sigHex) {
  // TODO 5: hash the payload: SHA-256 over strlen(payload) bytes into digest[32], with
  //         mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), in, len, digest).
  //         It returns 0 on success.
  uint8_t digest[32] = {0};
  const mbedtls_md_info_t* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);  // Select the SHA-256 algorithm required for P-256 ECDSA signing.
  if (sha256 == nullptr ||
      mbedtls_md(sha256,
                 reinterpret_cast<const unsigned char*>(payload),
                 strlen(payload),
                 digest) != 0) {
    return false;
  }

  // TODO 6: let the chip sign the digest with the slot 0 key: atecc.createSignature(digest, 0).
  //         The 64-byte signature (r||s) is in atecc.signature; convert it to hex in sigHex.
  if (!atecc.createSignature(digest, 0)) return false;  // Ask slot 0 to sign the digest internally; the private key never leaves the chip.
  toHex(atecc.signature, 64, sigHex);  // Convert the 64-byte raw r||s signature into 128 hexadecimal characters.
  return true;
}

// ============================================================================================
// Telemetry
// ============================================================================================

int readPotPercent() {
  return constrain((int)map(analogRead(IoTPins::POT_ADC), 0, 4095, 0, 100), 0, 100);
}

bool publishTelemetry() {
  if (!mqtt.connected()) return false;

  char temp[16] = "null", press[16] = "null";
  if (bmpOk) {
    snprintf(temp,  sizeof temp,  "%.2f", bmp.readTemperature());
    snprintf(press, sizeof press, "%.2f", bmp.readPressure() / 100.0f);
  }
  char buf[192];
  snprintf(buf, sizeof buf,
           "{\"seq\":%lu,\"temp_c\":%s,\"press_hpa\":%s,\"pot_pct\":%d,\"rssi\":%d,\"uptime_s\":%lu}",
           (unsigned long)(++seqNo), temp, press, readPotPercent(), (int)WiFi.RSSI(),
           (unsigned long)(millis() / 1000));
  bool ok = mqtt.publish(TOPIC_TELEMETRY.c_str(), buf, false);
  Serial.print(ok ? "PUB " : "PUB FAILED ");
  Serial.print(TOPIC_TELEMETRY);
  Serial.print("  ");
  Serial.println(buf);

  if (ok && eccSigning) {
    char sigHex[129];
    if (signPayload(buf, sigHex)) {
      char sigMsg[200];
      snprintf(sigMsg, sizeof sigMsg, "{\"seq\":%lu,\"alg\":\"ES256\",\"sig\":\"%s\"}",
               (unsigned long)seqNo, sigHex);
      mqtt.publish(TOPIC_SIG.c_str(), sigMsg, false);
      Serial.print("SIG ");
      Serial.println(sigHex);
      sigOk++;
    } else {
      Serial.println("SIG FAILED (ATECC608A did not sign)");
      sigFail++;
    }
  }
  return ok;
}

// ============================================================================================
// Serial console (given)
// ============================================================================================

void printHelp() {
  Serial.println("Commands: HELP | STATUS | PUB | PUBKEY | CA GOOD | CA WRONG | PROVISION | FORGET");
}

void printStatus() {
  Serial.print("Team "); Serial.print(TEAM_ID); Serial.print("  client id "); Serial.println(clientId);
  printCredentials();
  Serial.print("  Wi-Fi: "); Serial.println(WiFi.status() == WL_CONNECTED ? "connected" : "down");
  Serial.print("  MQTT : "); Serial.println(mqttStateText(mqtt.state()));
  Serial.print("  TLS CA: "); Serial.println(useWrongCa ? "WRONG CA (demo)" : "ISRG Root X1");
  Serial.print("  signing: "); Serial.println(eccSigning ? "ATECC608A slot 0" : "off");
  Serial.print("  published ok/failed: "); Serial.print(pubOk); Serial.print("/"); Serial.print(pubFail);
  Serial.print("  signed ok/failed: "); Serial.print(sigOk); Serial.print("/"); Serial.println(sigFail);
}

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
  if (line == "PUBKEY") {
    if (!eccSigning) { Serial.println("ERR no device key (chip missing or not provisioned)"); return; }
    char hex[129];
    toHex(devicePubKey, 64, hex);
    Serial.println(hex);
    return;
  }
  if (line == "CA WRONG" || line == "CA GOOD") {
    useWrongCa = (line == "CA WRONG");
    goOfflineCleanly();
    net.stop();
    configureMqtt();
    backoffMs = BACKOFF_START_MS;
    lastMqttTry = millis() - backoffMs;   // try again right away
    Serial.println("OK reconnecting with the selected CA");
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
  iotStartSerial("Lab 6: Harden It");
  iotInitI2C();   // default 100 kHz: the ATECC608A wake-up pulse relies on it
  iotInitSPI();
  iotInitDisplay();
  pinMode(IoTPins::POT_ADC, INPUT);

  bmpOk = bmp.begin(IoTPins::BMP280_ADDR);
  Serial.println(bmpOk ? "BMP280 OK at 0x76" : "BMP280 not found (temp/pressure will be null)");

  initSecureElement();

  uint64_t chip = ESP.getEfuseMac();
  char idbuf[32];
  snprintf(idbuf, sizeof idbuf, "iot-%s-%08lX", TEAM_ID, (unsigned long)(chip & 0xFFFFFFFF));
  clientId = idbuf;

  Serial.println("Type PROVISION (new credentials) or FORGET (erase) within 3 s...");
  iotDisplayLines("Lab 6 harden", "PROVISION/FORGET?", "(3 s, Serial)");
  String cmd = iotReadLine(3000);
  cmd.toUpperCase();
  if (cmd == "FORGET") forgetCredentials();

  bool haveCreds = loadCredentials();
  if (cmd == "PROVISION" || !haveCreds) {
    if (!haveCreds) Serial.println("No credentials in NVS yet.");
    iotDisplayLines("Lab 6 harden", "Provisioning:", "use Serial Monitor");
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
  mqtt.loop();

  static uint32_t lastPub = 0;
  if (iotEvery(PERIOD_MS, lastPub)) {
    if (mqtt.connected()) {
      if (publishTelemetry()) pubOk++; else pubFail++;
    }
  }

  static uint32_t lastDraw = 0;
  if (iotEvery(1000, lastDraw)) {
    const bool w = (WiFi.status() == WL_CONNECTED);
    iotDisplayLines(String("Lab 6  ") + TEAM_ID,
                    w ? String("WiFi ok") : String("WiFi: down"),
                    mqtt.connected() ? String("TLS verified") : String("MQTT: retrying"),
                    eccSigning ? "signed " + String((unsigned long)sigOk) : String("not signing"));
  }
}
