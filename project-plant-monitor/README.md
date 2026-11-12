# Team 24 Connected Plant Moisture Monitor

**Status: HARDWARE-READY, PENDING CALIBRATION AND PHYSICAL VALIDATION**

This project monitors one potted plant with a Capacitive Soil Moisture Sensor v1.2, displays the local status, and publishes live/reliability telemetry through MQTT to a Node-RED dashboard. Automatic watering is intentionally excluded from the required scope.

## Hardware

- ESP32 DevKit V1 (`esp32dev`)
- Capacitive Soil Moisture Sensor v1.2, analog output
- Course-kit BMP280, SPI OLED, LEDs, buzzer and Button 1
- Stable USB wall power for unattended operation

## Soil sensor wiring

Disconnect USB power while wiring. Verify the PCB silkscreen before applying power.

| Sensor lead | ESP32 |
|---|---|
| Yellow / AOUT | GPIO36 / VP |
| Red / VCC | 3V3 |
| Black / GND | GND |

Only the long sensing blade enters soil. Keep the connector and electronics above the soil.

## Build

1. Open this folder in VS Code with PlatformIO.
2. Build once to install dependencies.
3. Upload to the ESP32.
4. Open Serial Monitor at 115200 baud.
5. Enter Wi-Fi and MQTT credentials when prompted. Credentials are stored in ESP32 NVS, not source control.

## Calibration required before grading

The firmware publishes `moisture_raw` immediately, so it can pass the real-sensor vertical slice before percentage calibration. Percentage and DRY/NORMAL/WET decisions remain disabled until valid endpoints are saved.

1. Install the chosen sensor at its final depth and location.
2. Record at least 30 readings in representative dry soil and compute the median.
3. Type `SETDRY <median>` in Serial Monitor.
4. Water normally, let excess water drain, and wait for moisture to distribute.
5. Record at least 30 readings and compute the median.
6. Type `SETWET <median>`.
7. Type `STATUS` and verify the endpoints.

The mapping supports either sensor direction. Do not copy another sensor's endpoint values.

## Commands

- `HELP`
- `STATUS`
- `RAW`
- `PUB`
- `SETDRY <0-4095>`
- `SETWET <0-4095>`
- `ACK`
- `PROVISION`
- `FORGET`

## MQTT

- `iot/T24/plant/telemetry`
- `iot/T24/plant/status`
- `iot/T24/plant/alert`

Use EMQX TLS port 8883. Lab 5's `setInsecure()` pattern encrypts transport but does not authenticate the broker. Replace it with CA verification before claiming production-grade server authentication.

## Node-RED

1. Install Node-RED and `@flowfuse/node-red-dashboard`.
2. Import `nodered/flows.json`.
3. Edit the MQTT broker node:
   - Server: your EMQX hostname only
   - Port: 8883
   - TLS: enabled
   - Username/password: enter in Node-RED, never in `flows.json`
4. Deploy and open `http://localhost:1880/dashboard/plant-t24`.
5. Set the CSV path in the file node if Windows does not allow `plant_t24_reliability.csv` in the Node-RED working directory.

## Vertical slice

Demonstrate a real `moisture_raw` value moving through:

`Sensor -> GPIO36 -> ESP32 -> Wi-Fi -> MQTT -> EMQX -> Node-RED dashboard`

## 72-hour test

- Publish period: 60 seconds
- Expected messages: 72 x 60 = 4,320
- Keep ESP32, router/hotspot, Node-RED computer and broker available.
- Disable laptop sleep and hibernation.
- Preserve every outage and gap.
- Record start/end time and a real current measurement.
- Do not change code, wiring, thresholds or calibration during the run.

## Validation still required

- Local PlatformIO build and upload
- Connector/pin verification
- Test all five sensor modules and select the most stable unit
- Dry and wet calibration in the final pot/soil
- LEDs, buzzer and Button 1
- Wi-Fi/MQTT reconnect
- Last Will offline transition
- Node-RED dashboard and CSV logging
- Measured current
- 72-hour unattended run
