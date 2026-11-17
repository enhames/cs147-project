#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "iot_common.h"
#include "iot_display.h"
#include "iot_network.h"

// Activity 7 STARTER: MQTT Classroom Command Center. Complete TODO 1-4.
//
// Your ESP32 is an MQTT CLIENT. The instructor's laptop runs the BROKER.
//   - Button 1 PUBLISHES your telemetry to   <base>/team/<TEAM_ID>/telemetry
//   - Your kit SUBSCRIBES to the class topic <base>/command
//   - The instructor publishes ALERT or NORMAL once, and every kit reacts.
//   - Extension: Button 2 publishes HELLO to <base>/team/<TEAM_ID>/chat
// <base> is MQTT_TOPIC_BASE in include/secrets.h ("iot/class").
//
// Rule for the whole activity: no delay() in loop().

// CHANGE ME: your two-digit team number from Canvas, e.g. "07".
const String TEAM_ID = "24";

constexpr uint32_t BUTTON_DEBOUNCE_MS = 180;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 3000;
constexpr uint32_t DISPLAY_REFRESH_INTERVAL_MS = 500;
constexpr uint32_t SCREEN_HOLD_MS = 2500;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

struct Button {
  uint8_t pin;
  bool previous = HIGH;
  uint32_t lastAcceptedMs = 0;

  Button(uint8_t buttonPin) : pin(buttonPin) {}  // Initialize the button with its GPIO pin. (Not part of original code).
};
Button button1{IoTPins::BUTTON_1};
Button button2{IoTPins::BUTTON_2};

bool oledReady = false;
bool alertActive = false;
bool subscribedToCommands = false;
uint32_t lastMqttReconnectAttemptMs = 0;
uint32_t lastDisplayRefreshMs = 0;
uint32_t holdScreenUntilMs = 0;   // keep a result on the OLED for a moment
uint32_t sequenceNumber = 0;
String lastCommand = "NONE";

String commandTopic() {
  return String(MQTT_TOPIC_BASE) + "/command";
}

String telemetryTopic() {
  return String(MQTT_TOPIC_BASE) + "/team/" + TEAM_ID + "/telemetry";
}

String chatTopic() {
  return String(MQTT_TOPIC_BASE) + "/team/" + TEAM_ID + "/chat";
}

// Every MQTT client needs a unique ID. Two kits with the same ID kick each other off the broker.
String mqttClientId() {
  char chip[9];
  snprintf(chip, sizeof(chip), "%08lX", (unsigned long)(ESP.getEfuseMac() & 0xFFFFFFFF));
  return "iot-team" + TEAM_ID + "-" + String(chip);
}

int readSensorPercent() {
  const int rawReading = analogRead(IoTPins::POT_ADC);
  return constrain(map(rawReading, 0, 4095, 0, 100), 0, 100);
}

// Keep the current OLED screen for SCREEN_HOLD_MS before the periodic refresh redraws it.
void holdScreen() {
  holdScreenUntilMs = millis() + SCREEN_HOLD_MS;
}

bool screenHeld() {
  return static_cast<int32_t>(millis() - holdScreenUntilMs) < 0;
}

void showNormalScreen(const String& statusLine = "Button 1: publish") {
  if (!oledReady || alertActive) {
    return;
  }
  iotDisplayLines("MQTT Pub/Sub",
                  "Team: " + TEAM_ID,
                  "Sensor: " + String(readSensorPercent()) + "%",
                  statusLine);
}

void activateAlert() {
  alertActive = true;
  lastCommand = "ALERT";
  digitalWrite(IoTPins::LED_1, HIGH);   // red on
  digitalWrite(IoTPins::LED_3, LOW);    // green off

  if (oledReady) {
    iotDisplayLines("CLASS ALERT", "MQTT command", "received on:", commandTopic());
  }
}

void restoreNormalMode() {
  alertActive = false;
  lastCommand = "NORMAL";
  digitalWrite(IoTPins::LED_1, LOW);    // red off
  digitalWrite(IoTPins::LED_3, HIGH);   // green on
  showNormalScreen("Mode: NORMAL");
  holdScreen();
}

// PubSubClient calls this from mqttClient.loop() for every message on a subscribed topic.
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }
  message.trim();

  Serial.println();
  Serial.println("----- MQTT Message Received -----");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(message);

  // TODO 4: Handle the two class commands.
  //   "ALERT"  -> activateAlert()
  //   "NORMAL" -> restoreNormalMode()
  if (message == "ALERT") {
    activateAlert();  // Apply the class-wide alert received from the subscribed MQTT topic.
  } else if (message == "NORMAL") {
    restoreNormalMode();  // Restore the LEDs and OLED after the broker sends NORMAL.
  }
}

bool buttonPressed(Button& button) {
  const bool currentState = digitalRead(button.pin);
  const uint32_t now = millis();
  bool pressed = false;

  if (button.previous == HIGH && currentState == LOW &&
      now - button.lastAcceptedMs >= BUTTON_DEBOUNCE_MS) {
    button.lastAcceptedMs = now;
    pressed = true;
  }

  button.previous = currentState;
  return pressed;
}

// Tries to (re)connect at most once every MQTT_RECONNECT_INTERVAL_MS, so loop() never blocks for long.
bool connectToMqttBroker() {
  if (mqttClient.connected() && subscribedToCommands) {
    return true;
  }

  const uint32_t now = millis();
  if (now - lastMqttReconnectAttemptMs < MQTT_RECONNECT_INTERVAL_MS) {
    return false;
  }
  lastMqttReconnectAttemptMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("MQTT connection skipped: Wi-Fi is disconnected.");
    return false;
  }

  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }

  const String clientId = mqttClientId();
  Serial.print("Connecting to MQTT broker as ");
  Serial.print(clientId);
  Serial.print("...");

  if (!mqttClient.connect(clientId.c_str())) {
    Serial.print(" failed, state=");
    Serial.println(mqttClient.state());
    subscribedToCommands = false;
    return false;
  }

  Serial.println(" connected.");
  const String topic = commandTopic();

  bool subscribed = false;

  // TODO 2: Subscribe to the class command topic stored in topic.
  // Use mqttClient.subscribe(...) and store the result in subscribed.
  subscribed = mqttClient.subscribe(topic.c_str());  // Ask the broker to forward class commands to this kit.

  subscribedToCommands = subscribed;

  if (!subscribed) {
    Serial.print("Subscription failed for topic: ");
    Serial.println(topic);
    mqttClient.disconnect();
    return false;
  }

  Serial.print("Subscribed to: ");
  Serial.println(topic);
  showNormalScreen("MQTT connected");
  return true;
}

bool mqttReady() {
  if (mqttClient.connected() && subscribedToCommands) {
    return true;
  }
  Serial.println("Cannot publish: MQTT is not connected.");
  if (oledReady && !alertActive) {
    iotDisplayLines("MQTT offline", "Check Wi-Fi/broker", "See Serial Monitor", "Press to retry");
    holdScreen();
  }
  return false;
}

void publishTelemetry() {
  if (!mqttReady()) {
    return;
  }

  const int sensorPercent = readSensorPercent();
  sequenceNumber++;

  String topic = "";
  String payload = "";
  bool published = false;

  // TODO 3: Build and publish the telemetry message.
  //   topic:   telemetryTopic()   ->  <base>/team/<TEAM_ID>/telemetry
  //   payload: TEAM=<team>;SEQ=<sequence>;SENSOR=<percent>
  // Publish with mqttClient.publish(...) and store the result in published.
  topic = telemetryTopic();
  payload = "TEAM=" + TEAM_ID +
            ";SEQ=" + String(sequenceNumber) +
            ";SENSOR=" + String(sensorPercent);
  published = mqttClient.publish(topic.c_str(), payload.c_str());  // QoS 0 is the PubSubClient publish default.

  Serial.println();
  Serial.println("----- MQTT Telemetry Publish -----");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(payload);
  Serial.print("Publish result: ");
  Serial.println(published ? "SUCCESS" : "FAILED");

  if (oledReady && !alertActive) {
    if (published) {
      iotDisplayLines("Telemetry published",
                      "Team " + TEAM_ID + "  Seq " + String(sequenceNumber),
                      "Sensor: " + String(sensorPercent) + "%",
                      "Waiting for command");
    } else {
      iotDisplayLines("Publish failed", "Check broker", "See Serial Monitor", "Press to retry");
    }
    holdScreen();
  }
}

// EXTENSION: Button 2 publishes HELLO to <base>/team/<TEAM_ID>/chat.
void publishHello() {
  if (!mqttReady()) {
    return;
  }
  const String topic = chatTopic();
  bool published = false;
  // EXTENSION (optional): publish the text "HELLO" to topic with mqttClient.publish(...)
  // and store the result in published.
  published = mqttClient.publish(topic.c_str(), "HELLO");  // Button 2 sends a team-specific chat message through the broker.

  Serial.println();
  Serial.println("----- MQTT Chat Publish -----");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.println("Payload: HELLO");
  Serial.print("Publish result: ");
  Serial.println(published ? "SUCCESS" : "FAILED");
  showNormalScreen(published ? "HELLO sent" : "HELLO failed");
  holdScreen();
}

void setup() {
  iotStartSerial("Activity 7 - MQTT Classroom Command Center");
  iotPrintDeviceId();

  pinMode(IoTPins::BUTTON_1, INPUT_PULLUP);
  pinMode(IoTPins::BUTTON_2, INPUT_PULLUP);
  pinMode(IoTPins::POT_ADC, INPUT);
  pinMode(IoTPins::LED_1, OUTPUT);
  pinMode(IoTPins::LED_3, OUTPUT);
  digitalWrite(IoTPins::LED_1, LOW);
  digitalWrite(IoTPins::LED_3, HIGH);

  iotInitSPI();
  oledReady = iotInitDisplay();
  if (oledReady) {
    iotDisplayLines("MQTT Pub/Sub", "Connecting to Wi-Fi", "Team: " + TEAM_ID);
  }

  const bool wifiReady = iotConnectWiFi();

  // TODO 1: Configure the MQTT client.
  // Give mqttClient the broker address and port from secrets.h (setServer),
  // then register onMqttMessage as the callback (setCallback).
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);  // Configure the classroom MQTT broker endpoint.
  mqttClient.setCallback(onMqttMessage);       // Handle messages forwarded from subscribed topics.

  // Fail fast if the broker is unreachable (the library default is 15 s).
  mqttClient.setSocketTimeout(5);

  Serial.print("Team ID: ");
  Serial.println(TEAM_ID);
  if (TEAM_ID == "00") {
    Serial.println("WARNING: TEAM_ID is still 00. Set your team number in src/main.cpp.");
  }
  Serial.print("Telemetry topic: ");
  Serial.println(telemetryTopic());
  Serial.print("Command topic: ");
  Serial.println(commandTopic());

  if (!wifiReady) {
    Serial.println("Activity cannot continue until Wi-Fi is connected. Check secrets.h and reset.");
    if (oledReady) {
      iotDisplayLines("Wi-Fi failed", "Check secrets.h", "See Serial Monitor");
    }
    return;
  }

  // Let the first broker connection attempt run right away.
  lastMqttReconnectAttemptMs = millis() - MQTT_RECONNECT_INTERVAL_MS;
  connectToMqttBroker();
}

void loop() {
  if (!mqttClient.connected() || !subscribedToCommands) {
    connectToMqttBroker();
  }

  if (mqttClient.connected()) {
    mqttClient.loop();   // receives messages and keeps the connection alive
  }

  if (buttonPressed(button1)) {
    publishTelemetry();
  }

  if (buttonPressed(button2)) {
    publishHello();
  }

  if (!alertActive && !screenHeld() && iotEvery(DISPLAY_REFRESH_INTERVAL_MS, lastDisplayRefreshMs)) {
    showNormalScreen(mqttClient.connected() ? "Button 1: publish" : "MQTT reconnecting");
  }
}
