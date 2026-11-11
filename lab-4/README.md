# Lab 4: BLE Step Counter

You will build:
- **Part A:** a BLE server on your ESP32. Your phone (nRF Connect or LightBlue) turns the red LED on and off.
- **Part B:** a step counter. Calibrate the MPU-6050 at rest, sample it every 20 ms, count steps with a
  threshold detector, show the count on the OLED and notify it to your phone only when it changes.

1. Open this folder in VS Code with PlatformIO. Build once.
2. In `src/main.cpp`, set `TEAM_ID` to your team ID (for example `"T07"`). Your board will advertise as `IoT-T07`.
3. Complete TODOs 1-6 (search for TODO). The MPU-6050 helpers from Lab 3 are already filled in.
4. Upload, open the Serial Monitor (115200 baud), then connect from the phone app.

BLE is built into the ESP32 Arduino core: nothing to add to `lib_deps`.
Wiring: Stages 1-3 of the kit build. Nothing new. MPU-6050 at 0x68 (SDA 21, SCL 22), OLED on SPI
(SCK 18, MOSI 23, CS 5, DC 16, RES 17), red LED on D2, Button 1 on D32 (resets the count and recalibrates).
Tip: set `PLOT_SIGNAL = true` and open the Serial Plotter to tune `STEP_HIGH_G` / `STEP_LOW_G`.
