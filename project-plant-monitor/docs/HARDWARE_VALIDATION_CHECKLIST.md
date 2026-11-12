# Hardware validation checklist

- [ ] Confirm black aligns with GND, red with VCC and yellow with AOUT.
- [ ] Power sensor from ESP32 3V3, never VIN/5V for this build.
- [ ] Connect AOUT to GPIO36/VP.
- [ ] Test all five sensors in air, dry soil and watered/drained soil.
- [ ] Select the most stable sensor with the clearest dry-to-wet span.
- [ ] Record at least 30 samples per calibration condition.
- [ ] Save dry median with `SETDRY` and wet median with `SETWET`.
- [ ] Confirm percentage rises as soil becomes wetter.
- [ ] Confirm local sensing continues with Wi-Fi disabled.
- [ ] Confirm MQTT resumes after Wi-Fi returns.
- [ ] Confirm retained status changes to offline after an unclean disconnect.
- [ ] Confirm dashboard receives real sensor data, not an Inject-node test value.
