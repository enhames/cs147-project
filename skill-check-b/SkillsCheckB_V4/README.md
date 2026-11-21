# IoT@UCI Skills Check B

Group check, closed-assistant, about 25 minutes.

1. Wire the light sensor (BH1750 / GY-302, pins already soldered) to your kit, using its datasheet
   and the board labels. Confirm with STATUS that the sensor is found.
2. Set TEAM_ID in src/main.cpp. Build and upload. The board uses the Wi-Fi and HiveMQ credentials
   already stored in NVS from Labs 5 and 6 (type PROVISION within 3 s of boot if it asks).
3. The device publishes lux on iot/<TEAM_ID>/lux and moves the servo on "servo:<angle>" sent to
   iot/<TEAM_ID>/cmd (use the HiveMQ web client).
4. This firmware has ONE defect. Tell the TA what it is and why it matters, fix it without
   breaking the device, and name one other place the same kind of defect could hide.
