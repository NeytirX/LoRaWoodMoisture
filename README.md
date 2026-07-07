# LoRaWAN Wood Moisture Monitoring System

## Project Overview

This project implements a low-power, LoRaWAN-connected wood moisture monitoring system designed for long-term deployment in wood technology research and industrial applications. The system uses resistive probe technology based on the USDA Forest Products Laboratory (FPL) GTR-6 standard for accurate moisture content determination.

This firmware reads the resistive probe through a 100k voltage divider into a 16-bit ADS1115 external ADC, persists LoRaWAN sessions across deep sleep, exposes remote configuration via downlinks, and manages power based on battery state and USB presence. For interim use before an EU433 gateway is available, a companion P2P bench receiver (see Repository Companions) can pick up the sensor's frames directly.

### Key Features

- **Resistive Moisture Measurement:** Two-electrode resistive probe method following FPL GTR-6 guidelines, read by a 16-bit ADS1115 external ADC by default (internal ESP32 ADC available as a compile-time fallback via `USE_EXTERNAL_ADC`)
- **Species-Specific Calibration:** Supports multiple wood species with dedicated coefficients (A, B parameters)
- **Temperature Compensation:** Implements the FPL-GTR-6 Figure 5 correction grid (Celsius, digitized from the original chart) via bilinear interpolation
- **DS18B20 Temperature Sensor:** Optional 1-Wire temperature probe for direct wood temperature measurement; without a valid reading the MC correction uses a configured default temperature and the uplink is flagged
- **LoRaWAN Connectivity:** EU868/EU433 (runtime-selectable via NVS, single firmware binary for both board types), OTAA, Cayenne LPP payload format for IoT integration
- **Session Persistence:** LoRaWAN nonces saved to NVS and session saved to RTC memory across deep sleep cycles (avoids costly OTAA rejoin every wake)
- **Remote Configuration:** Downlink commands for adjusting measurement interval, wood species, LoRaWAN region, and forcing rejoin (a TX power command byte exists but is reserved — see Downlink Commands)
- **Battery-Aware Power Management:** AXP192/AXP2101 PMIC integration (auto-detected, T-Beam v1.1/v1.2) with critical voltage protection and adaptive sleep intervals (2x/4x multiplier when battery is low/critical); both the critical-battery abort and the sleep multipliers are skipped while USB power is present, including when no battery is detected at all
- **Hardware Watchdog:** ESP32 Task Watchdog Timer prevents firmware hangs (120s timeout)
- **Ultra-Low Power Design:** Deep sleep operation with configurable intervals (default: 1 hour)

### Thesis Context

This system was developed as part of a Master's thesis in Wood Technologies, focusing on:
1. Practical implementation of resistance-based moisture measurement
2. Embedded systems design for wood science applications
3. Temperature compensation methodology for field deployments
4. Validation of low-cost monitoring against reference instruments

---

## Hardware Requirements

### Core Components

| Component | Specification | Purpose |
|-----------|---------------|---------|
| TTGO T-Beam v1.1/v1.2 | ESP32 + SX1262 LoRa | Main controller and radio |
| AXP192 / AXP2101 PMIC | Integrated on T-Beam (auto-detected) | Power management and battery charging |
| Resistive Moisture Probe | Two-electrode type | Wood moisture sensing |
| 100k Pull-up Resistor | 1% tolerance recommended | Voltage divider reference |
| ADS1115 | 16-bit I2C ADC | Moisture divider readout (default measurement path; see docs/deployment_guide.md §2.1) |
| Li-ion/LiPo Battery | 3.7V, 18650 or similar | Power source |

### Pin Configuration

```
Moisture Probe (ADS1115 external ADC, default path -- USE_EXTERNAL_ADC true):
  - Divider node -> ADS1115 A0 (single-ended); ADS1115 shares the PMIC's
    I2C bus (SDA GPIO 21, SCL GPIO 22)
  - ADS1115 ADDR -> GND (I2C address 0x48)
  - Power Control: GPIO 25 (drives the divider, HIGH only during measurement)
  - Pull-up Resistor: 100k from GPIO 25 to the divider node
  - Wiring reference: docs/deployment_guide.md §2.1
  - GPIO 35 is only used when USE_EXTERNAL_ADC is false (legacy internal-ADC
    path); GPIO 32 is the radio BUSY line -- never use it here!

DS18B20 Temperature Sensor (optional):
  - Data: GPIO 14 (with 4.7k pull-up to 3.3V)

LoRa (SX1262, dedicated SPI bus):
  - SCK: GPIO 5
  - MISO: GPIO 19
  - MOSI: GPIO 27
  - CS/NSS: GPIO 18
  - RESET: GPIO 23
  - DIO1: GPIO 33
  - BUSY: GPIO 32

I2C (PMIC + ADS1115 external ADC):
  - SDA: GPIO 21
  - SCL: GPIO 22
```

### Optional/Recommended

- DS18B20 temperature sensor for direct wood temperature measurement (highly recommended for accurate temperature correction)
- Enclosure with IP65+ rating for outdoor deployment
- Solar panel for extended deployments

---

## Setup Instructions

### Prerequisites

1. **PlatformIO IDE** (VS Code extension) or PlatformIO CLI
2. **Python 3.8+** (for PlatformIO)
3. **USB cable** (micro-USB for T-Beam)
4. **LoRaWAN Network Server** account (e.g., The Things Network)

### Installation Steps

1. **Open the project** in PlatformIO

2. **Install dependencies** (automatic on first build):
   ```bash
   pio run
   ```

   Optionally run the host-side unit tests for the MC math (no hardware required):
   ```bash
   pio test -e native
   ```

3. **Configure LoRaWAN credentials:**
   - Copy the template: `cp include/lorawan_keys_template.h include/lorawan_keys.h`
   - Open `include/lorawan_keys.h`
   - Enter your Join EUI and Device EUI as **MSB** `uint64_t` hex literals (e.g., `0x0000000000000000`)
   - Enter your Network Key and Application Key as **MSB** `uint8_t[16]` byte arrays
   - For TTN v3: Copy the EUI/key values in MSB (big-endian) format

4. **Select wood species:**
   - Open `include/config.h`
   - Set `SELECTED_WOOD_SPECIES_INDEX` to match your target species
   - Available species are defined in `include/wood_species_data.h`

5. **Upload firmware:**
   ```bash
   pio run --target upload
   ```

6. **Monitor serial output:**
   ```bash
   pio device monitor -b 115200
   ```

---

## Configuration Guide

### Moisture Measurement Settings (`config.h`)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `USE_EXTERNAL_ADC` | true | Selects the ADS1115 external ADC (default) vs. the legacy internal ESP32 ADC |
| `MOISTURE_PROBE_ADC_PIN` | 35 | ADC input pin (internal path only, `USE_EXTERNAL_ADC` false) |
| `MOISTURE_PROBE_POWER_PIN` | 25 | Power control pin |
| `R_PULLUP_OHMS` | 100000.0f | Pull-up resistor value |
| `ADC_SAMPLES_TO_AVERAGE` | 10 | Samples per reading |
| `ADC_ATTENUATION` | ADC_11db | ADC range (0-3.3V) (internal path only, `USE_EXTERNAL_ADC` false) |
| `ADS1115_I2C_ADDRESS` | 0x48 | ADS1115 I2C address (ADDR -> GND); external path only |
| `EXT_ADC_COMPRESSION_LIMIT_MV` | 3150 | External-path node-voltage limit past which the divider is losing sensitivity (compression, not a converter knee); drives the LPP channel-8 flag; provisional pending the M2 precision-resistor ladder |
| `SELECTED_WOOD_SPECIES_INDEX` | 0 | Species table index |
| `ENABLE_TEMPERATURE_COMPENSATION` | true | Enable temp correction |
| `ONEWIRE_PIN` | 14 | DS18B20 data pin |

### Operational Settings

| Parameter | Default | Description |
|-----------|---------|-------------|
| `NORMAL_SEND_INTERVAL_SECONDS` | 3600 | Measurement interval (1 hour) |
| `LORAWAN_JOIN_MAX_RETRIES` | 5 | Max join attempts |
| `CRITICAL_BATTERY_THRESHOLD_V` | 3.2 | Critical cutoff threshold (V) |
| `LOW_BATTERY_THRESHOLD_V` | 3.4 | Extended sleep threshold (V) |
| `BATTERY_ABSENT_THRESHOLD_V` | 0.5 | Below this + USB present = no battery connected |
| `WATCHDOG_TIMEOUT_SECONDS` | 120 | Watchdog timer (seconds) |

Each wake cycle sends exactly one uplink (single-TX-per-cycle design): the firmware performs a single `sendReceive()` with no retry loop. A failed uplink is intentionally dropped and the device sleeps until the next measurement; for an hourly cadence a missed reading is acceptable, and skipping retries saves the battery and air-time they would cost.

### Downlink Commands (Remote Configuration)

Send downlink messages on any port to reconfigure the device:

| Command | Byte Format | Description |
|---------|-------------|-------------|
| Set Interval | `0x01 HH LL` | Set measurement interval (HH:LL = minutes, big-endian uint16) |
| Set Species | `0x02 XX` | Set wood species index (XX = 0 to NUM_WOOD_SPECIES-1) |
| Force Rejoin | `0x03` | Force a fresh OTAA join on next wake |
| Set TX Power | `0x04 XX` | Reserved — parsed but not applied; ADR manages TX power (issue: implement or keep reserved) |
| Set Region | `0x05 XX` | Set LoRaWAN region (XX = 0: EU868, 1: EU433). Persisted in NVS, forces rejoin |

### Adding Wood Species

Edit `include/wood_species_data.h`:

```cpp
// 1. Add a PROGMEM name string
static const char SPECIES_NAME_7[] PROGMEM = "Your Species Name";

// 2. Add entry to the species_data[] array
const WoodSpecies species_data[] PROGMEM = {
    // ... existing entries ...
    {SPECIES_NAME_7, A_coefficient, B_coefficient},
};
```

Coefficients should be sourced from FPL GTR-6 Table 1 or peer-reviewed literature.

---

## Data Format (Cayenne LPP)

The device transmits data using Cayenne Low Power Payload format:

| Channel | Type | Description | Data Type |
|---------|------|-------------|-----------|
| 1 | Analog Input | Wood Moisture Content (%) | Float |
| 2 | Temperature | Wood Temperature (C); omitted on temp fallback | Float |
| 3 | Analog Input | Indicated MC (pre-correction) | Float |
| 4 | Generic Sensor | Resistance (kOhm) | uint32 (integer kOhm) |
| 5 | Analog Input | Battery Voltage (V) | Float |
| 6 | Temperature | ESP32 Internal Temp (C) | Float |
| 7 | Digital Input | Temp fallback flag | 0/1 |
| 8 | Digital Input | ADC nonlinear flag | 0/1 |

**Note:** Channels 3 and 6 are commented out by default to conserve payload space. Channel 4 (resistance) is enabled so flagged readings can be re-judged offline. Enable 3/6 in `main.cpp` if needed for debugging.

**Channel 7** is always sent: `1` means no valid DS18B20 reading was available, so the corrected MC was computed with the configured default wood temperature (`DEFAULT_WOOD_TEMP_CELSIUS`, 21 C) and channel 2 is omitted. Filter or re-correct these readings during analysis.

**Channel 8** is always sent: `1` means the divider node pushed the ESP32 ADC past its linearity knee (`ADC_LINEARITY_LIMIT_MV`) or saturated it, so the returned resistance is compressed and the MC is low-confidence. Typically very dry wood. Treat those rows as qualitative and re-judge with the raw resistance (channel 4).

---

## Firmware Architecture

### Sequential Boot-to-Sleep Design (8 Phases)

The firmware uses RadioLib (pinned `^7.1.0`, resolves to 7.7.1 as of 2026-06-04) with a synchronous/blocking API. The entire measure-send-sleep cycle runs once in `setup()`, then the ESP32 enters deep sleep. On wake, the ESP32 restarts and `setup()` runs again. `loop()` is never reached.

```
ESP32 Boot (reset / timer wake)
         |
         v
  Phase 1: PMIC Setup
  (PMIC detect AXP192/AXP2101, enable LoRa power, disable GPS)
         |
         v
  Phase 1b: Critical Battery Check
  (read voltage, abort if critical -> extended sleep, unless USB present)
         |
         v
  Phase 2: Sensor Init
  (ADC config, DS18B20 detection)
         |
         v
  Phase 3: Radio Init + Join/Restore
  (SX1262 begin, restore session from NVS/RTC, OTAA activate)
         |
    [activateOTAA result]
    /                    \
  SESSION_RESTORED     NEW_SESSION
  (skip join)          (fresh join)
         \                /
          v              v
  Phase 5: Sensor Measurement
  (resistance, temperature, MC calculation with species correction;
   battery voltage re-read here for the payload)
         |
         v
  Phase 6: Build Payload
  (Cayenne LPP encoding)
         |
         v
  Phase 7: LoRaWAN Uplink
  (sendReceive(), process downlink commands, save session)
         |
         v
  Phase 8: Deep Sleep
  (timer wakeup configured, battery-aware interval)
```

(Phase numbering keeps the historical gap after Phase 3 — there is no separate Phase 4.)

### Key Modules

| Module | File | Function |
|--------|------|----------|
| Configuration | `include/config.h` | System parameters and thresholds |
| Sensor Layer | `include/sensor.h` | ADC, DS18B20, resistance measurement |
| Session Manager | `include/session_manager.h` | LoRaWAN session save/restore (nonces in NVS, session in RTC memory) |
| Species Data | `include/wood_species_data.h` | FPL GTR-6 species coefficients (PROGMEM) |
| Temp Correction | `include/wood_temp_correction_data.h` | FPL-GTR-6 Figure 5 correction grid (Celsius) |
| LoRaWAN Keys | `include/lorawan_keys.h` | Network credentials (MSB format: `uint64_t` EUIs, `uint8_t[16]` keys) |
| Main Logic | `src/main.cpp` | Sequential boot-to-sleep flow, downlink handling |

### Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| RadioLib | `^7.1.0` (resolves to 7.7.1 as of 2026-06-04) | SX1262 LoRa radio driver + LoRaWAN stack |
| CayenneLPP | ^1.6.0 | Low Power Payload encoding |
| XPowersLib | ^0.1.9 | AXP192/AXP2101 PMIC control |
| OneWire | ^2.3.8 | 1-Wire bus protocol |
| DallasTemperature | ^3.11.0 | DS18B20 temperature sensor |
| Adafruit ADS1X15 | ^2.5.0 | ADS1115 external ADC driver (moisture divider readout) |

### Build Statistics (as of firmware 1.3.0)

| Resource | Usage |
|----------|-------|
| RAM | 1.9% (24,976 / 1,310,720 bytes) |
| Flash | 33.6% (439,749 / 1,310,720 bytes) |

---

## Technical Documentation

The following detailed documentation is available in the `docs/` directory:

1. **[Firmware Architecture](docs/firmware_architecture.md)** - Detailed sequential flow, module documentation, and memory layout
2. **[Calibration Procedure](docs/calibration_procedure.md)** - Probe calibration and verification methods
3. **[Deployment Guide](docs/deployment_guide.md)** - Installation and field deployment instructions
4. **[Data Interpretation](docs/data_interpretation.md)** - Understanding moisture readings, temperature correction, and analysis methods

---

## Repository Companions

- **`receiver/`** - A standalone Heltec WiFi LoRa 32 V3 project that receives the sensor's frames over a raw private-LoRa link when no LoRaWAN gateway is available (bench/interim use). Decodes Cayenne LPP, prints JSON over serial, supports optional MQTT, and shows MC on its OLED. See `receiver/README.md`.
- **`tools/p2p_tx_smoke/`** - A throwaway canned-frame transmitter for bring-up/testing of the receiver.
- **`tbeam-p2p` build env** - TEMPORARY sensor-side counterpart to the P2P link, gated by `P2P_MODE`; removed once an EU433 gateway exists. The deployment data path remains LoRaWAN. Wire format shared via `receiver/include/p2p_frame.h`.

---
---

## License

This project is part of academic research. Please cite appropriately if used in your work.

---

## Contact

For thesis-related inquiries, contact the author through academic channels.
