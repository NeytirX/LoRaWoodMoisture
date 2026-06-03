# AGENTS.md — LoRaWoodMoisture Orchestrator

This file teaches opencode how to autonomously build, flash, monitor, validate, and iterate on the LoRaWAN wood moisture firmware.

## Project Context

**LoRaWoodMoisture** is a low-power, LoRaWAN-connected wood moisture monitoring system for the TTGO T-Beam v1.1 (ESP32 + SX1262 + AXP192). It uses resistive probe technology based on the USDA Forest Products Laboratory (FPL) GTR-06 standard.

**Architecture:** Sequential boot-to-sleep cycle. Everything runs in `setup()`, then the ESP32 enters deep sleep. On timer wake, the ESP32 restarts and `setup()` runs again. `loop()` is never reached.

**Key files:**
- `src/main.cpp` — Single-file firmware (all logic)
- `include/config.h` — System constants (firmware v2.0.0)
- `include/sensor.h` — ADC + DS18B20 sensor abstraction
- `include/session_manager.h` — LoRaWAN session persistence (NVS + RTC)
- `include/wood_species_data.h` — FPL GTR-06 species coefficients
- `include/wood_temp_correction_data.h` — FPL GTR-06 Table 2 correction factors
- `include/lorawan_keys.h` — OTAA credentials (gitignored, user-managed)
- `platformio.ini` — Build configuration

**Hardware:** TTGO T-Beam v1.1 with SX1262 LoRa radio, AXP192 PMIC, resistive moisture probe (GPIO 32 ADC, GPIO 25 power gate), DS18B20 temperature sensor (GPIO 14).

## Build Commands

All commands must be run from the `LoRaWoodMoisture/` directory.

### Build firmware

```bash
pio run
```

### Flash firmware to board

```bash
pio run --target upload
```

### Monitor serial output

```bash
pio device monitor -b 115200
```

### Clean build artifacts

```bash
pio run --target clean
```

### List connected devices

```bash
pio device list
```

## Board Detection

The TTGO T-Beam connects via USB serial. Expected device: **COM3** (USB VID:PID=1A86:55D4, CH9102F USB-to-serial).

**Before any flash or monitor operation**, verify the board is connected:

```bash
pio device list
```

If the board is not listed:
1. Check USB cable connection
2. Try a different USB port
3. Install CH9102F drivers if needed (Windows)
4. Verify the board powers on (LED indicators)

If the COM port has changed from COM3, use the detected port in monitor commands:
```bash
pio device monitor -b 115200 --port <detected-port>
```

## Validation Diagnostics

After flashing, monitor serial output at 115200 baud. A successful boot produces this sequence:

### Expected serial output (success path)

```
========================================
 Wood Moisture Sensor (LoRaWAN/RadioLib)
========================================
Firmware v2.0.0
Wake reason: Timer (or "Other" for cold boot)
[WDT] Enabled. Timeout: 120 s
[Phase] PMIC Setup
[Phase] Sensor Init
[Phase] Radio Init
[Radio] SX1262 initialized OK
[LoRaWAN] Activating OTAA...
[LoRaWAN] Session restored from saved state! (or "New session - fresh join successful!")
[Phase] Sensor Measurement
Wood Resistance: <value> kOhms
Indicated MC: <value> %
Wood Temp: <value> C
FINAL Corrected MC: <value> %
[Phase] Build Payload
[Phase] LoRaWAN Uplink
[TX] Uplink successful!
[Session] Session saved to RTC RAM.
[Sleep] Entering deep sleep for 3600 seconds
```

### Key validation checkpoints

| Checkpoint | What to look for | Failure indicator |
|------------|------------------|-------------------|
| Boot | Firmware version prints | No output = serial issue or bad flash |
| PMIC | "PMIC initialized OK" | "PMIC init failed!" = AXP192 issue |
| Radio | "SX1262 initialized OK" | "Init FAILED, code: <n>" = wiring or SPI issue |
| LoRaWAN | "Session restored" or "New session" | "Activation FAILED" = join issue (check keys) |
| Sensor | Wood resistance and MC values print | Missing = ADC or probe issue |
| TX | "Uplink successful!" | "Uplink FAILED" = LoRa connectivity issue |
| Sleep | "Entering deep sleep for N seconds" | Missing = firmware hung (watchdog should reset) |

## Iteration Workflow

The standard development loop:

1. **Edit source** — Modify files in `src/` or `include/`
2. **Build** — `pio run` (fix any compile errors)
3. **Flash** — `pio run --target upload`
4. **Monitor** — `pio device monitor -b 115200`
5. **Validate** — Check serial output against validation checkpoints above
6. **Repeat** — Go to step 1

### Quick iteration command

For rapid development, chain build and flash:
```bash
pio run --target upload && pio device monitor -b 115200
```

### After deep sleep testing

The firmware enters deep sleep after each measurement cycle (default: 1 hour). To test quickly:
- Press the RST button on the T-Beam to wake immediately
- Or wait for the timer (set `NORMAL_SEND_INTERVAL_SECONDS` in `config.h` to a shorter value for testing)

## Troubleshooting

### Build errors

| Error | Cause | Fix |
|-------|-------|-----|
| `lorawan_keys.h: No such file` | Missing OTAA credentials | Copy `include/lorawan_keys_template.h` to `include/lorawan_keys.h` and fill in your keys |
| `RadioLib.h: No such file` | Libraries not installed | Run `pio run` once to auto-install dependencies |
| `undefined reference` | Missing include or library | Check `platformio.ini` lib_deps and `#include` statements |

### Upload failures

| Error | Cause | Fix |
|-------|-------|-----|
| `Failed to connect` | Board not in upload mode | Hold BOOT button on T-Beam while running upload, or press RST then immediately upload |
| `Wrong serial port` | COM port mismatch | Run `pio device list` to find correct port |
| `A fatal error occurs: Wrong boot mode` | ESP32 in wrong state | Press RST button, then retry upload |

### Serial monitor issues

| Issue | Cause | Fix |
|-------|-------|-----|
| No output | Wrong baud rate | Ensure using 115200 baud |
| Garbled output | Wrong baud rate or USB issue | Check baud rate, try different USB cable/port |
| `Permission denied` | Port in use | Close other serial monitors (Arduino IDE, PuTTY, etc.) |

### LoRaWAN issues

| Issue | Cause | Fix |
|-------|-------|-----|
| Join fails repeatedly | Wrong keys | Verify EUI/key values in `lorawan_keys.h` match your network server |
| Join fails after working | Session corruption | Send force-rejoin downlink (0x03) or reflash |
| TX fails | Out of range or antenna | Check LoRa gateway proximity, verify antenna connection |

### Watchdog resets

| Symptom | Cause | Fix |
|---------|-------|-----|
| Reboots every ~2 minutes | Firmware hung in setup() | Check serial output for where it stops; likely blocking in radio init or sensor read |
| Reboots after long sleep | Normal behavior | Watchdog timeout is 120s; if measurement+TX takes longer, increase `WATCHDOG_TIMEOUT_SECONDS` |
