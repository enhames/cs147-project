#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
// Serial helpers for provisioning (Lab 5). Given code: you do not need to edit it.
//
// These functions BLOCK while they wait for you to type. That is fine here because
// they only run during setup() or when you ask for provisioning. Never call them
// from the normal loop() path.
// -----------------------------------------------------------------------------

// Read one line from Serial (ends at '\n', '\r' is ignored).
// timeoutMs = 0 waits forever. Returns "" on timeout.
inline String iotReadLine(uint32_t timeoutMs = 0, size_t maxLen = 96) {
  String line;
  const uint32_t start = millis();
  while (timeoutMs == 0 || millis() - start < timeoutMs) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\r') continue;
      if (c == '\n') { line.trim(); return line; }
      if (line.length() < maxLen) line += c;
    }
    delay(5);   // OK here: we are waiting for a human, not running the main loop
  }
  return String();
}

// Print a prompt and read the answer. Leading/trailing spaces are removed.
inline String iotAsk(const char* prompt) {
  Serial.print(prompt);
  Serial.print(" ");
  String v = iotReadLine(0);
  Serial.println();
  return v;
}

// "********  (12 characters)" -- never print a secret back to the screen.
inline String iotMasked(const String& secret) {
  if (secret.length() == 0) return String("(empty)");
  return String("********  (") + String((unsigned)secret.length()) + " characters)";
}

// Accept what people paste from the HiveMQ console:
//   "mqtts://abc123.s1.eu.hivemq.cloud:8883"  ->  "abc123.s1.eu.hivemq.cloud"
inline String iotCleanHost(String h) {
  h.trim();
  if (h.startsWith("mqtts://")) h = h.substring(8);
  if (h.startsWith("ssl://"))   h = h.substring(6);
  int colon = h.indexOf(':');
  if (colon >= 0) h = h.substring(0, colon);
  int slash = h.indexOf('/');
  if (slash >= 0) h = h.substring(0, slash);
  return h;
}
