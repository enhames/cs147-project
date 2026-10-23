// Lab 2: Traffic Controller
// Cycle: RED (10 s) -> RED + YELLOW (2 s) -> GREEN (until button, then 5 s) -> YELLOW (2 s)
#include <Arduino.h>
#include <esp_log.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const int LED_RED    = 2;   // blue LED standing in for red
const int LED_YELLOW = 4;
const int LED_GREEN  = 15;
const int PED_BUTTON = 32;  // button 1, INPUT_PULLUP: reads LOW when pressed
const int BUZZER     = 27;  // passive piezo
const int OLED_CS = 5, OLED_DC = 16, OLED_RST = 17;  // SCK 18, MOSI 23

Adafruit_SSD1306 oled(128, 64, &SPI, OLED_DC, OLED_RST, OLED_CS);
bool oledOk = false;
const unsigned long DISPLAY_REFRESH_MS = 250;
unsigned long lastDisplay = 0;

// ---- Timings (milliseconds) ----
const unsigned long RED_MS               = 10000;
const unsigned long RED_YELLOW_MS        = 2000;
const unsigned long GREEN_AFTER_PRESS_MS = 5000;
const unsigned long YELLOW_MS            = 2000;

// Buzzer patterns: on time and full period
const unsigned long RED_BEEP_ON_MS     = 250;  // 250 on / 250 off
const unsigned long RED_BEEP_PERIOD    = 500;
const unsigned long GREEN_BEEP_ON_MS   = 500;  // 500 on / 1500 off
const unsigned long GREEN_BEEP_PERIOD  = 2000;
const int BUZZER_HZ = 1000;

// ---- State machine ----
enum LightState { RED, RED_YELLOW, GREEN, YELLOW };

LightState state = RED;          // current state
unsigned long stateStart = 0;    // millis() when we entered it
bool pedRequested = false;       // pedestrian button pressed during this green?
unsigned long pressTime = 0;     // millis() when it was pressed
bool buzzerOn = false;           // so we only call tone()/noTone() on a change

const char* stateName(LightState s) {
  switch (s) {
    case RED:        return "RED";
    case RED_YELLOW: return "RED + YELLOW";
    case GREEN:      return "GREEN";
    case YELLOW:     return "YELLOW";
  }
  return "?";
}

void setLights(bool red, bool yellow, bool green) {
  digitalWrite(LED_RED,    red    ? HIGH : LOW);
  digitalWrite(LED_YELLOW, yellow ? HIGH : LOW);
  digitalWrite(LED_GREEN,  green  ? HIGH : LOW);
}

// Switch to a new state: record the time and set the LEDs for it.
void enterState(LightState next) {
  unsigned long now = millis();
  static bool firstState = true;
  if (!firstState) {
    Serial.printf("[%7lu ms]    %s lasted %.1f s\n",
                  now, stateName(state), (now - stateStart) / 1000.0);
  }
  firstState = false;

  state = next;
  stateStart = now;
  pedRequested = false;

  switch (next) {
    case RED:        setLights(true,  false, false); break;
    case RED_YELLOW: setLights(true,  true,  false); break;
    case GREEN:      setLights(false, false, true);  break;
    case YELLOW:     setLights(false, true,  false); break;
  }
  Serial.printf("[%7lu ms] -> %s\n", stateStart, stateName(next));
}

void setBuzzer(bool on) {
  if (on == buzzerOn) return;    // no change, nothing to do
  buzzerOn = on;
  if (on) tone(BUZZER, BUZZER_HZ);
  else    noTone(BUZZER);
}

// Beep pattern depends on the state and on how long we have been in it.
void updateBuzzer() {
  unsigned long t = millis() - stateStart;
  if (state == RED) {
    setBuzzer(t % RED_BEEP_PERIOD < RED_BEEP_ON_MS);
  } else if (state == GREEN) {
    setBuzzer(t % GREEN_BEEP_PERIOD < GREEN_BEEP_ON_MS);
  } else {
    setBuzzer(false);            // no pattern in RED + YELLOW or YELLOW
  }
}

// Check whether it is time to move to the next state.
void updateTrafficLight() {
  unsigned long now = millis();
  unsigned long t = now - stateStart;

  switch (state) {
    case RED:
      if (t >= RED_MS) enterState(RED_YELLOW);
      break;

    case RED_YELLOW:
      if (t >= RED_YELLOW_MS) enterState(GREEN);
      break;

    case GREEN:
      // Green holds until the button is pressed, then lasts 5 s more.
      if (!pedRequested && digitalRead(PED_BUTTON) == LOW) {
        pedRequested = true;
        pressTime = now;
        Serial.printf("[%7lu ms]    Pedestrian button pressed: green ends in 5 s\n", now);
      }
      if (pedRequested && now - pressTime >= GREEN_AFTER_PRESS_MS) {
        enterState(YELLOW);
      }
      break;

    case YELLOW:
      if (t >= YELLOW_MS) enterState(RED);
      break;
  }
}

// Show the state and a countdown on the OLED, a few times per second.
void updateDisplay() {
  if (!oledOk) return;
  unsigned long now = millis();
  if (now - lastDisplay < DISPLAY_REFRESH_MS) return;
  lastDisplay = now;

  unsigned long t = now - stateStart;
  unsigned long remainingMs = 0;
  bool waiting = false;
  switch (state) {
    case RED:        remainingMs = RED_MS - min(t, RED_MS); break;
    case RED_YELLOW: remainingMs = RED_YELLOW_MS - min(t, RED_YELLOW_MS); break;
    case YELLOW:     remainingMs = YELLOW_MS - min(t, YELLOW_MS); break;
    case GREEN:
      if (pedRequested) {
        unsigned long sincePress = now - pressTime;
        remainingMs = GREEN_AFTER_PRESS_MS - min(sincePress, GREEN_AFTER_PRESS_MS);
      } else {
        waiting = true;
      }
      break;
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Traffic Controller");

  oled.setTextSize(2);
  oled.setCursor(0, 20);
  oled.println(state == RED_YELLOW ? "RED+YEL" : stateName(state));

  oled.setTextSize(1);
  oled.setCursor(0, 48);
  if (waiting) {
    oled.println("Press button to cross");
  } else {
    oled.printf("Next in %.1f s", remainingMs / 1000.0);
  }
  oled.display();
}

void setup() {
  // Safest state first: show RED as soon as possible after power-up.
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  setLights(true, false, false);

  pinMode(PED_BUTTON, INPUT_PULLUP);

  // tone() in Arduino core 2.x logs a harmless "LEDC is not initialized"
  // error the first time it runs; hide log messages from the ledc driver.
  esp_log_level_set("ledc", ESP_LOG_NONE);

  Serial.begin(115200);
  Serial.println("\n=== Lab 2: Traffic Controller ===");

  SPI.begin();
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC);
  if (!oledOk) Serial.println("OLED not found; continuing without it");

  enterState(RED);
}

void loop() {
  updateTrafficLight();
  updateBuzzer();
  updateDisplay();
}