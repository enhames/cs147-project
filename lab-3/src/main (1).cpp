hel#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include "iot_common.h"
#include "iot_display.h"

// Lab 3: Serial communication. Three buses, one program.
//   UART  -> command console in the Serial Monitor (Part A)
//   I2C   -> MPU-6050 read register by register, BMP280 through its library (Part B)
//   SPI   -> OLED dashboard (Part C)
// Rule for the whole lab: NO delay() in loop(). Use millis() / iotEvery() so the console,
// the sensors and the display all keep running.

// ---------------- MPU-6050 registers (datasheet: MPU-6000/6050 Register Map) ----------------
constexpr uint8_t MPU_PWR_MGMT_1   = 0x6B;  // write 0x00 to wake the chip (it boots asleep)
constexpr uint8_t MPU_WHO_AM_I     = 0x75;  // should read 0x68
constexpr uint8_t MPU_ACCEL_XOUT_H = 0x3B;  // 6 bytes: XH XL YH YL ZH ZL (big-endian, signed)
constexpr float   ACCEL_LSB_PER_G  = 16384.0f;  // full-scale range +/-2 g (the power-on default)

// ---------------- Servo (same setup as Activity 4) ----------------
constexpr int SERVO_CH = 2, SERVO_HZ = 50, SERVO_RES_BITS = 16;
constexpr int SERVO_MIN_US = 600, SERVO_MAX_US = 2400;
uint32_t usToDuty(int us) { return (uint32_t)us * 65536 / 20000; }

Adafruit_BMP280 bmp(&Wire);
bool bmpOk = false;
const uint8_t LED_PINS[3] = {IoTPins::LED_1, IoTPins::LED_2, IoTPins::LED_3};
int servoAngle = 90;

struct AccelG { float x, y, z; };

// ============================================================================================
// Part B: I2C at the register level
// ============================================================================================

// Write one byte to a register. (Given: use it as a model for mpuReadRegs.)
bool mpuWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(IoTPins::MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;   // 0 = the chip ACKed every byte
}

// Read `len` consecutive registers starting at `reg` into buf. Returns true on success.
bool mpuReadRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  // TODO 1: (a) beginTransmission to the MPU, write the register address, and
  //             endTransmission(false) -- false keeps the bus (repeated START).
  //         (b) requestFrom(address, len); if it returns fewer than len bytes, return false.
  //         (c) read the bytes into buf[0..len-1] with Wire.read().

  Wire.beginTransmission(IoTPins::MPU6050_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  uint8_t received = Wire.requestFrom(IoTPins::MPU6050_ADDR, len);
  if (received != len) return false;

  for (uint8_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }

  return true;
}

// Read the accelerometer and convert to g.
bool readAccelG(AccelG& a) {
  uint8_t raw[6];
  if (!mpuReadRegs(MPU_ACCEL_XOUT_H, raw, 6)) return false;
  // TODO 2: combine each high/low byte pair into a SIGNED 16-bit value, e.g.
  //         int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
  //         then divide by ACCEL_LSB_PER_G to get g. Do the same for y (raw[2..3]) and z (raw[4..5]).
  int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
  int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
  int16_t az = (int16_t)((raw[4] << 8) | raw[5]);

  a.x = ax / ACCEL_LSB_PER_G;
  a.y = ay / ACCEL_LSB_PER_G;
  a.z = az / ACCEL_LSB_PER_G;

  return true;
}

// ============================================================================================
// Part A: UART command console
// ============================================================================================

void printHelp() {
  Serial.println("OK Commands (case-insensitive):");
  Serial.println("  HELP                 this list");
  Serial.println("  LED <1|2|3> <ON|OFF> red / yellow / green LED");
  Serial.println("  SERVO <0-180>        move the servo to an angle in degrees");
  Serial.println("  READ ACCEL           accelerometer x y z in g (register-level I2C)");
  Serial.println("  READ TEMP            BMP280 temperature (C) and pressure (hPa)");
}

void reply(const String& msg) { Serial.println(msg); }
void error(const String& msg) { Serial.print("ERR "); Serial.println(msg); }

// Parse a whole decimal number. Returns false for "", "12abc", "4.5", "-" ...
bool parseInt(const String& s, long& out) {
  if (s.length() == 0 || s.length() > 6) return false;
  unsigned i = (s[0] == '-') ? 1 : 0;
  if (i == s.length()) return false;
  for (; i < s.length(); i++) if (!isDigit(s[i])) return false;
  out = s.toInt();
  return true;
}

// Split a line into up to maxTok words separated by spaces. Returns the word count.
int tokenize(const String& line, String tok[], int maxTok) {
  int n = 0; int i = 0; const int L = line.length();
  while (i < L && n < maxTok + 1) {
    while (i < L && line[i] == ' ') i++;
    if (i >= L) break;
    int j = i;
    while (j < L && line[j] != ' ') j++;
    if (n < maxTok) tok[n] = line.substring(i, j);
    n++; i = j;
  }
  return n;   // may be maxTok+1: "too many words"
}

void handleCommand(String line) {
  line.trim();
  line.toUpperCase();
  if (line.length() == 0) return;

  String t[4];
  int n = tokenize(line, t, 3);
  if (n > 3) { error("too many words. Type HELP"); return; }

  if (t[0] == "HELP" && n == 1) { printHelp(); return; }

  if (t[0] == "LED") {
    // TODO 3: check n == 3; parse t[1] with parseInt() and accept only 1..3; accept only
    //         ON or OFF in t[2]. Then digitalWrite(LED_PINS[num-1], ...) and reply("OK LED ...").
    //         Anything else -> error("usage: LED <1|2|3> <ON|OFF>") and return.
    long number;
    if (n != 3 || !parseInt(t[1], number) || number < 1 || number > 3 ||
        (t[2] != "ON" && t[2] != "OFF")) {
      error("usage: LED <1|2|3> <ON|OFF>");
      return;
    }

    bool turnOn = (t[2] == "ON");
    digitalWrite(LED_PINS[number - 1], turnOn ? HIGH : LOW);
    reply("OK LED " + String(number) + " " + t[2]);
    return;
  }

  if (t[0] == "SERVO") {
    // TODO 4: check n == 2; parse t[1]; accept only 0..180. Map the angle to a pulse between
    //         SERVO_MIN_US and SERVO_MAX_US, ledcWrite(SERVO_CH, usToDuty(pulse)),
    //         remember it in servoAngle, and reply("OK SERVO <angle>").
    long angle;
    if (n != 2 || !parseInt(t[1], angle) || angle < 0 || angle > 180) {
      error("usage: SERVO <0-180>");
      return;
    }

    int pulse = map(angle, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
    ledcWrite(SERVO_CH, usToDuty(pulse));
    servoAngle = angle;
    reply("OK SERVO " + String(servoAngle));
    return;
  }

  if (t[0] == "READ" && n == 2 && t[1] == "ACCEL") {
    // TODO 5: call readAccelG(); on success print e.g. "ACCEL x=0.01 y=-0.02 z=1.00 g |a|=1.00"
    //         (|a| = sqrt(x*x + y*y + z*z)); on failure error("MPU-6050 not responding").
    AccelG a;
    if (!readAccelG(a)) {
      error("MPU-6050 not responding");
      return;
    }

    float magnitude = sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    reply("OK ACCEL x=" + String(a.x, 2) +
          " y=" + String(a.y, 2) +
          " z=" + String(a.z, 2) +
          " g |a|=" + String(magnitude, 2));
    return;
  }

  if (t[0] == "READ" && n == 2 && t[1] == "TEMP") {
    // TODO 6: if bmpOk, print "TEMP 23.4 C  PRESS 1012.6 hPa" using bmp.readTemperature() and
    //         bmp.readPressure() / 100.0; otherwise error("BMP280 not found").
    if (!bmpOk) {
      error("BMP280 not found");
      return;
    }

    float temperature = bmp.readTemperature();
    float pressure = bmp.readPressure() / 100.0f;
    reply("OK TEMP " + String(temperature, 1) +
          " C  PRESS " + String(pressure, 1) + " hPa");
    return;
  }

  error("unknown command '" + line + "'. Type HELP");
}

// Collect characters without blocking; run a command at each newline. (Given.)
void pollConsole() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { handleCommand(buf); buf = ""; continue; }
    if (buf.length() >= 60) { error("line too long (max 60 characters)"); buf = ""; continue; }
    buf += c;
  }
}

// ============================================================================================

void setup() {
  iotStartSerial("Lab 3: Serial Communication");
  iotInitI2C();
  iotInitSPI();
  iotInitDisplay();

  for (uint8_t p : LED_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  ledcSetup(SERVO_CH, SERVO_HZ, SERVO_RES_BITS);
  ledcAttachPin(IoTPins::SERVO_PWM, SERVO_CH);
  ledcWrite(SERVO_CH, usToDuty(1500));

  bmpOk = bmp.begin(IoTPins::BMP280_ADDR);
  Serial.println(bmpOk ? "BMP280 OK at 0x76" : "BMP280 not found (check SDO->GND, CSB->3V3)");

  // TODO 7 (Part B): wake the MPU-6050 by writing 0x00 to MPU_PWR_MGMT_1, then read one byte
  //         from MPU_WHO_AM_I with mpuReadRegs() and print it in hex: "WHO_AM_I = 0x68".
  if (!mpuWriteReg(MPU_PWR_MGMT_1, 0x00)) {
    error("could not wake MPU-6050");
  } else {
    uint8_t whoAmI;
    if (mpuReadRegs(MPU_WHO_AM_I, &whoAmI, 1)) {
      Serial.print("WHO_AM_I = 0x");
      if (whoAmI < 0x10) Serial.print("0");
      Serial.println(whoAmI, HEX);
    } else {
      error("could not read MPU-6050 WHO_AM_I");
    }
  }

  printHelp();
  Serial.print("> ");
}

void loop() {
  pollConsole();

  // Part C: live OLED dashboard over SPI, refreshed every 200 ms (5 Hz) without blocking.
  static uint32_t lastDraw = 0;
  if (iotEvery(200, lastDraw)) {
    // TODO 8 (Part C): read the accelerometer (readAccelG) and, if bmpOk, the temperature,
    //         then show four lines with iotDisplayLines(), for example:
    //           "Lab 3 dashboard" / "ax  0.01 ay -0.02" / "az  1.00 g" / "T 23.4C servo 90"
    AccelG a;
    bool accelOk = readAccelG(a);

    String accelXY = accelOk
        ? "ax " + String(a.x, 2) + " ay " + String(a.y, 2)
        : "MPU read failed";
    String accelZ = accelOk
        ? "az " + String(a.z, 2) + " g"
        : "az --";
    String temperature = bmpOk
        ? String(bmp.readTemperature(), 1)
        : "--";

    iotDisplayLines("Lab 3 dashboard",
                    accelXY,
                    accelZ,
                    "T " + temperature + "C servo " + String(servoAngle));
  }
}