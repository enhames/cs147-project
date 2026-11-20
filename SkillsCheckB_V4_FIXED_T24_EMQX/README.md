# IoT@UCI Skills Check B

Group check, closed-assistant, about 25 minutes.

1. Wire the light sensor (BH1750 / GY-302, pins already soldered) to your kit, using its datasheet
   and the board labels. Confirm with STATUS that the sensor is found.
2. Set TEAM_ID in src/main.cpp. Build and upload. The board uses the Wi-Fi and EMQX credentials
   already stored in NVS from Labs 5 and 6 (type PROVISION within 3 s of boot if it asks).
3. The device publishes lux on iot/<TEAM_ID>/lux and moves the servo on "servo:<angle>" sent to
   iot/<TEAM_ID>/cmd (use the EMQX/MQTT web client).
4. This firmware has ONE defect. Tell the TA what it is and why it matters, fix it without
   breaking the device, and name one other place the same kind of defect could hide.


## Team 24 EMQX settings

- Team ID: `T24`
- Broker: enter the assigned EMQX hostname during `PROVISION`
- MQTT over TLS: port `8883`
- Root CA: `DIGICERT_GLOBAL_ROOT_G2_PEM`
- Do not put Wi-Fi, MQTT, or command-authentication secrets in source code.
