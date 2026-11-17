#pragma once

// Activity 7: fill these in with the values shown on screen in lecture.
// The classroom network is the course Wi-Fi router, not UCInet / eduroam.
// Keep real passwords out of Git: add include/secrets.h to .gitignore before you push.

#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"

// IPv4 address of the instructor laptop that runs the MQTT broker, e.g. "192.168.8.10".
#define MQTT_HOST        "192.168.8.10"
#define MQTT_PORT        1883

// Every topic in this activity starts with this base. Do not change it.
#define MQTT_TOPIC_BASE  "iot/class"
