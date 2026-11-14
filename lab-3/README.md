# Lab 3: Serial Communication (UART, I2C, SPI)

You will build:
- **Part A:** a UART command console in the Serial Monitor (HELP, LED, SERVO, READ ACCEL, READ TEMP)
  that rejects bad input instead of crashing. Keep it: it is your debugging interface for later labs.
- **Part B:** read the MPU-6050 accelerometer register by register with `Wire` (no MPU library).
- **Part C:** a live OLED dashboard over SPI that updates while the console stays responsive.

1. Open this folder in VS Code with PlatformIO, build once to download the libraries.
2. Complete TODOs 1-8 in `src/main.cpp` (search for TODO).
3. Upload, open the Serial Monitor (115200 baud), type `HELP` and press Enter.

Wiring: Stage 1 + 2 + 3 of the kit build. Nothing new.
MPU-6050 at 0x68 and BMP280 at 0x76 on I2C (SDA 21, SCL 22); OLED on SPI (SCK 18, MOSI 23,
CS 5, DC 16, RES 17); LEDs on 2/4/15; servo on 13 (5 V rail + capacitor).
