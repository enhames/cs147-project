#include <Arduino.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <string>
#include "iot_common.h"
#include "iot_display.h"

// Lab 4: BLE step counter.
//   Part A -> a phone turns the red LED on/off by writing to a BLE characteristic.
//   Part B -> MPU-6050 calibrated at rest, sampled every 20 ms, threshold step detector,
//             step count notified over BLE only when it changes, and shown on the OLED.
// Rule for the whole lab: NO delay() in loop(). Use millis() / iotEvery().

// ---------------- CHANGE ME: your team ID from Canvas (T01 ... T30) ----------------
// 25+ boards advertise in the same room. The name is how you find YOUR board in the app.
#define TEAM_ID "T24"   // <-- put your team ID here before you upload
const char* const DEVICE_NAME = "IoT-" TEAM_ID;   // -> "IoT-T07"

// ---------------- BLE UUIDs (random 128-bit, generated for this lab) ----------------
// Every team uses the same UUIDs; the device name tells the boards apart.
#define SERVICE_UUID    "71b12873-ad3c-4b5a-ad8a-f7139a8ef342"
#define LED_CHAR_UUID   "499306f5-0f14-4d5d-9d31-bdd52c597914"   // phone writes 1 / 0 (or ON / OFF)
#define STEPS_CHAR_UUID "3a007a51-0a02-4831-a402-4d0d8ddd1fbe"   // board notifies the count as text

// ---------------- MPU-6050 registers (same helpers as Lab 3) ----------------
constexpr uint8_t MPU_PWR_MGMT_1   = 0x6B;  // write 0x00 to wake the chip
constexpr uint8_t MPU_CONFIG       = 0x1A;  // DLPF_CFG: 0x03 = ~44 Hz low-pass filter
constexpr uint8_t MPU_ACCEL_CONFIG = 0x1C;  // AFS_SEL: 0x08 = +/-4 g (walking can pass 2 g)
constexpr uint8_t MPU_WHO_AM_I     = 0x75;  // 0x68 on a genuine part (clones: 0x70/0x72/0x98)
constexpr uint8_t MPU_ACCEL_XOUT_H = 0x3B;  // 6 bytes: XH XL YH YL ZH ZL
constexpr float   ACCEL_LSB_PER_G  = 8192.0f;  // at +/-4 g

// ---------------- Step detector settings (tune these with the Serial Plotter) ----------------
constexpr uint32_t SAMPLE_MS     = 20;     // 50 Hz
constexpr int      CAL_SAMPLES   = 100;    // 100 x 20 ms = 2 s at rest
constexpr float    FILTER_ALPHA  = 0.30f;  // exponential smoothing, 0..1 (higher = less smoothing)
constexpr float    STEP_HIGH_G   = 0.20f;  // signal must rise above this to count a step...
constexpr float    STEP_LOW_G    = 0.05f;  // ...and fall below this before the next one (hysteresis)
constexpr uint32_t REFRACTORY_MS = 300;    // ignore new peaks for 300 ms (nobody walks > 3 steps/s)
constexpr bool     PLOT_SIGNAL   = false;  // true: print "sig:<g>" every sample for the Serial Plotter

struct AccelG { float x, y, z; };

// ---------------- State ----------------
BLECharacteristic* ledChar   = nullptr;
BLECharacteristic* stepsChar = nullptr;
volatile bool bleConnected = false;
volatile bool ledOn = false;

bool     mpuOk = false;
bool     calibrating = true;
int      calCount = 0;
AccelG   calSum = {0, 0, 0};
AccelG   offset = {0, 0, 0};      // mean reading at rest (includes gravity)
float    restMag = 1.0f;           // |a| at rest, about 1 g
float    signalG = 0.0f;           // filtered dynamic acceleration, in g
bool     armed = true;             // hysteresis state: true = ready for the next step
uint32_t lastStepMs = 0;
uint32_t stepCount = 0;
uint32_t lastSentCount = UINT32_MAX;   // forces one update after calibration

// ============================================================================================
// MPU-6050 at the register level (given: you wrote these in Lab 3)
// ============================================================================================
bool mpuWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(IoTPins::MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool mpuReadRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(IoTPins::MPU6050_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(IoTPins::MPU6050_ADDR, len) < len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool readAccelG(AccelG& a) {
  uint8_t raw[6];
  if (!mpuReadRegs(MPU_ACCEL_XOUT_H, raw, 6)) return false;
  a.x = (int16_t)((raw[0] << 8) | raw[1]) / ACCEL_LSB_PER_G;
  a.y = (int16_t)((raw[2] << 8) | raw[3]) / ACCEL_LSB_PER_G;
  a.z = (int16_t)((raw[4] << 8) | raw[5]) / ACCEL_LSB_PER_G;
  return true;
}

bool mpuBegin() {
  uint8_t who = 0;
  bool ok = mpuWriteReg(MPU_PWR_MGMT_1, 0x00) &&
            mpuWriteReg(MPU_CONFIG, 0x03) &&
            mpuWriteReg(MPU_ACCEL_CONFIG, 0x08) &&
            mpuReadRegs(MPU_WHO_AM_I, &who, 1);
  Serial.print("WHO_AM_I = 0x");
  Serial.println(who, HEX);
  if (!ok) Serial.println("MPU-6050 did not answer at 0x68 (check AD0->GND, SDA 21 / SCL 22)");
  return ok;
}

// ============================================================================================
// Part A: BLE server
// ============================================================================================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected = true;
    Serial.println("BLE: phone connected");
  }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    Serial.println("BLE: phone disconnected, advertising again");
    BLEDevice::startAdvertising();   // without this, nobody can reconnect until reset
  }
};

// Runs (in the BLE task) every time the phone writes the LED characteristic.
class LedCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    std::string v = c->getValue();
    if (v.empty()) return;
    // TODO 2 (Part A): decide on/off from what the phone wrote. Accept text from nRF Connect
    //   ("1", "0", "ON", "OFF") AND a raw byte from LightBlue (v[0] == 1 or v[0] == 0).
    //   Anything else: print "BLE: LED write ignored (send 1/0 or ON/OFF)" and return.
    //   Then set ledOn, digitalWrite(IoTPins::LED_1, ...), c->setValue("ON"/"OFF") and print
    //   "BLE: LED ON" / "BLE: LED OFF".
    bool requestedOn;
    if (v == "1" || v == "ON" || (v.size() == 1 && (uint8_t)v[0] == 1)) {  // Accept either text from nRF Connect or raw byte 0x01 from LightBlue.
      requestedOn = true;
    } else if (v == "0" || v == "OFF" || (v.size() == 1 && (uint8_t)v[0] == 0)) {
      requestedOn = false;
    } else {
      Serial.println("BLE: LED write ignored (send 1/0 or ON/OFF)");
      return;
    }

    ledOn = requestedOn;
    digitalWrite(IoTPins::LED_1, ledOn ? HIGH : LOW);  // Translate the BLE command into a physical GPIO output.
    c->setValue(ledOn ? "ON" : "OFF");  // Keep the GATT characteristic synchronized with the physical LED.
    Serial.println(ledOn ? "BLE: LED ON" : "BLE: LED OFF");
  }
};

void setupBle() {
  BLEDevice::init(DEVICE_NAME);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService* service = server->createService(SERVICE_UUID);

  // TODO 1 (Part A): create the two characteristics on `service`:
  //   (a) ledChar   = service->createCharacteristic(LED_CHAR_UUID, PROPERTY_READ | PROPERTY_WRITE);
  //       attach new LedCallbacks() with setCallbacks() and setValue(std::string("OFF")).
  //   (b) stepsChar = service->createCharacteristic(STEPS_CHAR_UUID, PROPERTY_READ | PROPERTY_NOTIFY);
  //       addDescriptor(new BLE2902()) so the phone can turn notifications on; setValue("0").
  //   (The properties are BLECharacteristic::PROPERTY_READ, ::PROPERTY_WRITE, ::PROPERTY_NOTIFY.)

  ledChar = service->createCharacteristic(  // Create a GATT characteristic the phone can use to control the LED.
      LED_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);  // The phone may inspect the state and send a new state.
  ledChar->setCallbacks(new LedCallbacks());  // Run onWrite() whenever the BLE client writes this characteristic.
  ledChar->setValue(std::string("OFF"));

  stepsChar = service->createCharacteristic(  // Create a separate GATT characteristic for the step count.
      STEPS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);  // Read gives the current count; Notify pushes future changes.
  stepsChar->addDescriptor(new BLE2902());  // BLE2902 adds the CCCD used by a phone to subscribe to notifications.
  stepsChar->setValue(std::string("0"));

  service->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);   // name + 128-bit UUID do not fit in one 31-byte packet
  adv->setMinPreferred(0x06);   // helps iPhones connect
  adv->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.print("BLE: advertising as ");
  Serial.println(DEVICE_NAME);
}

// ============================================================================================
// Part B: calibrate, sample, detect steps
// ============================================================================================
void startCalibration() {
  calibrating = true;
  calCount = 0;
  calSum = {0, 0, 0};
  Serial.println("Calibrating: keep the board still for 2 s...");
}

// Called every SAMPLE_MS from loop(). One accelerometer read per call, never blocks.
void sampleImu() {
  AccelG a;
  if (!readAccelG(a)) return;

  if (calibrating) {
    calSum.x += a.x; calSum.y += a.y; calSum.z += a.z;
    if (++calCount >= CAL_SAMPLES) {
      // TODO 3 (Part B): calibration is done. Set `offset` to the mean of the CAL_SAMPLES readings
      //   (calSum / CAL_SAMPLES on each axis) and `restMag` to the length of that mean vector
      //   (sqrt(x*x + y*y + z*z), about 1 g). Lying flat you should see offset z close to 1.000.

      offset.x = calSum.x / CAL_SAMPLES;  // Calibration averages stationary samples to estimate the resting X offset.
      offset.y = calSum.y / CAL_SAMPLES;  // Estimate the resting Y offset.
      offset.z = calSum.z / CAL_SAMPLES;  // Estimate the resting Z value, including gravity on that axis.
      restMag = sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);  // Save total acceleration at rest so gravity can be removed later.

      calibrating = false;
      signalG = 0.0f;
      armed = true;
      Serial.println("Calibrated: offset x=" + String(offset.x, 3) + " y=" + String(offset.y, 3) +
                     " z=" + String(offset.z, 3) + " g, |a| at rest=" + String(restMag, 3) + " g");
    }
    return;
  }

  // Dynamic acceleration = how far |a| moved away from its value at rest (gravity removed).
  // Magnitude works whatever way the board is held. To use a single axis instead:
  //   float raw = a.z - offset.z;
  // TODO 4 (Part B): compute `raw` = |a| - restMag (or one axis minus its offset, your choice),
  //   then smooth it: signalG = FILTER_ALPHA * raw + (1 - FILTER_ALPHA) * signalG.
  float magnitude = sqrt(a.x * a.x + a.y * a.y + a.z * a.z);  // Use all three axes so detection is less sensitive to board orientation.
  float raw = magnitude - restMag;  // Subtract the calibrated resting magnitude to keep dynamic motion.
  signalG = FILTER_ALPHA * raw + (1.0f - FILTER_ALPHA) * signalG;  // Low-pass filter the motion signal to reduce accelerometer noise.
  if (PLOT_SIGNAL) Serial.println("sig:" + String(signalG, 3));

  // Threshold detector with hysteresis and a refractory period:
  //   one step = rise above STEP_HIGH_G while armed and > REFRACTORY_MS since the last step;
  //   re-arm only after the signal falls back below STEP_LOW_G.
  // TODO 5 (Part B): if armed, signalG > STEP_HIGH_G and more than REFRACTORY_MS since lastStepMs:
  //   count the step, remember millis() in lastStepMs, disarm, print "STEP <n>".
  //   If not armed and signalG < STEP_LOW_G: arm again.
  uint32_t now = millis();
  if (armed && signalG > STEP_HIGH_G && now - lastStepMs > REFRACTORY_MS) {  // Count only a new threshold crossing outside the refractory period.
    stepCount++;
    lastStepMs = now;
    armed = false;  // Disarm until the signal falls below the low threshold (hysteresis).
    Serial.println("STEP " + String(stepCount));
  } else if (!armed && signalG < STEP_LOW_G) {  // Rearm only after motion settles below the low threshold.
    armed = true;
  }

}

// Notify the phone only when the count changed (BLE airtime is shared with 25 other teams).
void publishSteps() {
  // TODO 6 (Part B): return early while calibrating or if stepCount == lastSentCount.
  //   Otherwise remember lastSentCount, put the count in stepsChar as text
  //   (setValue(std::to_string(stepCount))), notify() if a phone is connected,
  //   and print "BLE: notified steps=<n>".
  if (calibrating || stepCount == lastSentCount) return;  // Avoid BLE traffic during calibration and when the value is unchanged.

  lastSentCount = stepCount;
  stepsChar->setValue(std::to_string(stepCount));  // Store the updated count as text in the GATT characteristic.
  if (bleConnected) stepsChar->notify();  // Push the new count to a subscribed phone only when connected.
  Serial.println("BLE: notified steps=" + String(stepCount));
}

void drawOled() {
  String l2 = bleConnected ? "BLE: connected" : "BLE: advertising";
  String l3, l4;
  if (!mpuOk)           l3 = "MPU-6050 missing";
  else if (calibrating) l3 = "Calibrating... still";
  else                  l3 = "Steps: " + String(stepCount);
  l4 = "LED " + String(ledOn ? "ON " : "OFF") + " sig " + String(signalG, 2);
  iotDisplayLines(String(DEVICE_NAME), l2, l3, l4);
}

// ============================================================================================

void setup() {
  iotStartSerial("Lab 4: BLE Step Counter");
  iotInitI2C();
  iotInitSPI();
  iotInitDisplay();

  pinMode(IoTPins::LED_1, OUTPUT);
  digitalWrite(IoTPins::LED_1, LOW);
  pinMode(IoTPins::BUTTON_1, INPUT_PULLUP);

  mpuOk = mpuBegin();
  setupBle();
  startCalibration();
  Serial.println("Button 1 (D32): reset the count and recalibrate.");
}

void loop() {
  static uint32_t lastSample = 0, lastDraw = 0, lastButton = 0;
  static bool buttonWasUp = true;

  if (mpuOk && iotEvery(SAMPLE_MS, lastSample)) {
    sampleImu();
    publishSteps();
  }

  // Button 1: reset + recalibrate (checked every 30 ms = simple debounce).
  if (iotEvery(30, lastButton)) {
    bool up = digitalRead(IoTPins::BUTTON_1) == HIGH;
    if (buttonWasUp && !up) {
      stepCount = 0;
      lastSentCount = UINT32_MAX;
      startCalibration();
    }
    buttonWasUp = up;
  }

  if (iotEvery(200, lastDraw)) drawOled();
}