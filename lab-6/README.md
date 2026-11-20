# Lab 6: Harden It

Starts from your Lab 5 device (BMP280 + pot -> HiveMQ Cloud over MQTT, credentials in NVS,
Last Will on `iot/<TEAM_ID>/status`) and hardens it:

- **Part A:** TLS with the broker certificate **verified** (ISRG Root X1 + `setCACert()`), and proof that a wrong CA fails.
- **Part B:** credentials stay in NVS, typed once over Serial, never in Git (`tools/check_secrets.sh` before every push).
- **Part C:** each reading is signed by the device key inside the ATECC608A (slot 0). Read-only use: **never lock or configure the chip**.
- **Part D:** read-only `espefuse.py summary`. **Never burn eFuses** on a kit board.

1. Open this folder in VS Code with PlatformIO. Set `TEAM_ID` in `src/main.cpp`. Build once to download the libraries.
2. Complete TODOs 1-6 (TODO 1 is in `include/iot_ca_certs.h`, TODOs 2-6 in `src/main.cpp`).
3. Upload, open the Serial Monitor (115200). A board provisioned in Lab 5 keeps its credentials;
   otherwise type them when asked.
4. Console: `HELP`, `STATUS`, `PUB`, `PUBKEY`, `CA GOOD`, `CA WRONG`, `PROVISION`, `FORGET`.

Topics: `iot/<TEAM_ID>/telemetry` (signed bytes), `iot/<TEAM_ID>/telemetry/sig`, `iot/<TEAM_ID>/pubkey` (retained),
`iot/<TEAM_ID>/status` (retained, Last Will).

Wiring: same kit as Lab 5. ATECC608A Qwiic breakout on I2C (SDA 21, SCL 22) at 0x60, next to the BMP280 (0x76).


## Team 24 EMQX Serverless configuration

This completed variant targets EMQX Serverless rather than the HiveMQ example in the original starter.

- MQTT host: enter the EMQX deployment Address only, without `mqtts://`, `:8883`, or `/`.
- MQTT-over-TLS port: `8883`.
- ESP32 client: `WiFiClientSecure` through `PubSubClient`.
- Correct trust anchor (`CA GOOD`): DigiCert Global Root G2 from EMQX's Serverless CA file.
- Negative-test trust anchor (`CA WRONG`): ISRG Root X1, which is a valid public root but unrelated to the EMQX broker chain.
- MQTT username and password: enter through Serial provisioning; never place them in source code.

For Team 24's current deployment, provision the host `ufb82712.ala.us-east-1.emqxsl.com`. Rotate any MQTT password that has been pasted into chat or committed to a repository, then provision the replacement locally.

Expected certificate test:

1. `CA GOOD` selects DigiCert Global Root G2 and should connect to EMQX.
2. `CA WRONG` selects ISRG Root X1 and should fail with an X.509 verification error.
3. `CA GOOD` restores the correct certificate and reconnects.

EMQX Serverless supports MQTT over TLS on port 8883. Port 8084 is secure WebSocket MQTT for browser clients and is not used by this ESP32 firmware.
