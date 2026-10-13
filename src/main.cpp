// IoT kit check: tests whatever is wired so far. Open Serial Monitor at 115200.
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
 
// Fall 2026 pin map (same as cs147_pins.h)
const int LED_1 = 2, LED_2 = 4, LED_3 = 15;      // red, yellow, green
const int BUTTON_1 = 32, BUTTON_2 = 33;          // to GND, INPUT_PULLUP
const int POT_ADC = 34, BUZZER = 27, SERVO_PWM = 13;
const int OLED_CS = 5, OLED_DC = 16, OLED_RST = 17;  // SCK 18, MOSI 23
const int I2C_SDA = 21, I2C_SCL = 22;
 
Adafruit_SSD1306 oled(128, 64, &SPI, OLED_DC, OLED_RST, OLED_CS);
 
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== IoT kit check ===");
 
  // Stage 1: LEDs, buzzer, OLED
  int leds[] = {LED_1, LED_2, LED_3};
  for (int pin : leds) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH); delay(300); digitalWrite(pin, LOW);
  }
  Serial.println("LEDs: did red, yellow, green each light once?");
  tone(BUZZER, 1000, 200);
  Serial.println("Buzzer: did you hear a short beep?");
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  SPI.begin();
  if (oled.begin(SSD1306_SWITCHCAPVCC)) {
    oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(2); oled.setCursor(0, 0); oled.println("IoT KIT");
    oled.display();
    Serial.println("OLED: PASS if it shows IoT KIT");
  } else {
    Serial.println("OLED: FAIL (check the 7 OLED wires)");
  }
 
  // Stage 2: I2C scan (expect 0x60, 0x68, 0x76)
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.print("I2C devices:");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
  }
  Serial.printf("  (%d found; Stage 2 expects 0x60 0x68 0x76)\n", found);
 
  // Stage 3: servo sweep 0 -> 180 -> 90 degrees (16-bit, 50 Hz)
  ledcSetup(2, 50, 16);   // channel 2: tone() uses channel 0
  ledcAttachPin(SERVO_PWM, 2);
  const int dutyFor[] = {1638, 8192, 4915};
  for (int d : dutyFor) { ledcWrite(2, d); delay(700); }
  Serial.println("Servo: did it sweep and stop in the middle? (Stage 3)");
  Serial.println("Now press the buttons and turn the pot:");
}
void loop() {
  int pot = analogRead(POT_ADC);
  bool b1 = digitalRead(BUTTON_1) == LOW;
  bool b2 = digitalRead(BUTTON_2) == LOW;
  Serial.printf("pot=%4d  button1=%s  button2=%s\n", pot, b1 ? "DOWN" : "up", b2 ? "DOWN" : "up");
  oled.fillRect(0, 32, 128, 32, SSD1306_BLACK);
  oled.setTextSize(1); oled.setCursor(0, 36);
  oled.printf("pot %4d  B1 %s B2 %s", pot, b1 ? "on" : "--", b2 ? "on" : "--");
  oled.display();
  delay(250);
}
