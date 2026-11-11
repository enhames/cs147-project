# Lab 5: Cloud Communication (MQTT over TLS)

Your ESP32 publishes BMP280 temperature/pressure and the knob position to your own
**HiveMQ Cloud** broker over MQTT on port 8883 (TLS). **Node-RED** on a team laptop subscribes,
draws a live dashboard, and raises an alert when the broker reports your device **offline**
(its Last Will).

```
BMP280 + pot -> ESP32 --Wi-Fi/TLS/MQTT:8883--> HiveMQ Cloud --> Node-RED (dashboard + alert)
```

## Build and run
1. Open this folder in VS Code with PlatformIO. Build once to download the libraries.
2. Set `TEAM_ID` at the top of `src/main.cpp` (e.g. `"T07"`).
3. Complete TODOs 1-6 in `src/main.cpp` (search for TODO). It compiles before you start.
   Until TODO 1 and 2 are done, the board asks for credentials on every boot.
4. Upload, open the Serial Monitor (115200). On first boot, type your Wi-Fi and HiveMQ
   credentials when asked. They are stored in NVS flash, not in the code.
5. Node-RED: import `nodered/flows.json`, set the broker host and credentials, finish the
   two TODOs in the flow (gauge/chart, offline alert), Deploy, open
   http://localhost:1880/dashboard/lab5

Serial commands: at startup `PROVISION` / `FORGET` (3 s window, press EN to reboot);
while running `HELP`, `STATUS`, `PUB`, `PROVISION`, `FORGET`.

Wiring: nothing new. BMP280 on I2C (SDA 21, SCL 22, 0x76), potentiometer wiper on GPIO 34,
SPI OLED (CS 5, DC 16, RES 17).

**Never commit passwords.** `.gitignore` is included. Type `FORGET` before you return the kit.
