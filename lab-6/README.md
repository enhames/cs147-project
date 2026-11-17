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
