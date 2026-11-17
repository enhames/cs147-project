# Activity 7: MQTT Classroom Command Center (starter)

Your ESP32 is an MQTT client. The instructor's laptop runs the MQTT broker.
Button 1 publishes your telemetry to a team topic. Your kit also subscribes to one class command topic:
when the instructor publishes ALERT or NORMAL once, every kit in the room reacts.

| Topic | Who publishes | Payload |
|---|---|---|
| `iot/class/team/<TEAM_ID>/telemetry` | your kit (Button 1) | `TEAM=07;SEQ=3;SENSOR=62` |
| `iot/class/command` | instructor | `ALERT` or `NORMAL` |
| `iot/class/team/<TEAM_ID>/chat` | your kit (Button 2, extension) | `HELLO` |

## Build and run
1. Open this folder in VS Code with PlatformIO.
2. Edit `include/secrets.h`: Wi-Fi name and password, and `MQTT_HOST` = the instructor laptop address shown on
   screen. Keep `MQTT_PORT 1883` and `MQTT_TOPIC_BASE "iot/class"`.
3. In `src/main.cpp`, change `TEAM_ID = "00"` to your two-digit team number.
4. Complete TODO 1-4 in `src/main.cpp`.
5. Build, Upload, open the Serial Monitor at 115200 baud.
6. Turn the knob and press Button 1. Your team appears on the instructor screen.
7. Wait for ALERT and NORMAL from the instructor and watch your OLED and LEDs.

## Expected Serial output
```
Connecting to MQTT broker as iot-team07-1A2B3C4D... connected.
Subscribed to: iot/class/command

----- MQTT Telemetry Publish -----
Topic: iot/class/team/07/telemetry
Payload: TEAM=07;SEQ=1;SENSOR=62
Publish result: SUCCESS

----- MQTT Message Received -----
Topic: iot/class/command
Payload: ALERT
```
ALERT: red LED on, OLED shows CLASS ALERT. NORMAL: green LED on, normal screen.

## Keep secrets.h out of Git
Add this line to `.gitignore` before you push: `include/secrets.h`

## Kit parts
ESP32, SPI OLED, potentiometer (GPIO 34), Button 1 (GPIO 32), Button 2 (GPIO 33), red LED (GPIO 2),
green LED (GPIO 15). Library: `knolleary/PubSubClient` (installed by PlatformIO).
