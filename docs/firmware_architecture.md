# Firmware Architecture Documentation

## Overview

This document describes the firmware architecture of the LoRaWAN Wood Moisture Monitoring System, implemented for a Master's thesis in Wood Technologies. The firmware uses a **sequential blocking flow** optimized for ultra-low power operation — the entire measure-send-sleep cycle runs once per boot in `setup()`.

**File:** `src/main.cpp`
**Framework:** Arduino ESP32 with RadioLib (pinned `^7.1.0`, resolves to 7.7.1 as of 2026-06-04)
**Design Pattern:** Sequential boot-to-sleep cycle (no state machine, no `loop()`)

---

## 1. System Architecture

### 1.1 High-Level Block Diagram

```
+-----------------------------------------------------------------+
|                      Firmware Architecture                       |
+-----------------------------------------------------------------+
|                                                                  |
|  +--------------+    +--------------+    +--------------+        |
|  |   Power      |    |    LoRaWAN   |    |   Moisture   |        |
|  |  Management  |    |   Protocol   |    |  Measurement |        |
|  |  (AXP PMIC)  |    |  (RadioLib)  |    |   (ADC)      |        |
|  +------+-------+    +------+-------+    +------+-------+        |
|         |                   |                   |                 |
|         +-------------------+-------------------+                 |
|                             |                                    |
|  +--------------+  +--------v--------+  +--------------+         |
|  |   Session    |  |   Sequential    |  |   Sensor     |         |
|  |   Manager    |  |   Flow Control  |  |   Layer      |         |
|  | (NVS + RTC)  |  +--------+--------+  |   (DS18B20)  |         |
|  +--------------+           |           +--------------+         |
|                             |                                    |
|         +-------------------+-------------------+                |
|         |                   |                   |                |
|  +------v-------+   +-------v-------+  +-------v-------+        |
|  |   Species    |   |  Temperature  |  |    Data       |        |
|  |   Data       |   |  Compensation |  |  Encoding     |        |
|  |   (PROGMEM)  |   |  (Bilinear)   |  |  (Cayenne LPP)|       |
|  +--------------+   +---------------+  +---------------+        |
|                                                                  |
+-----------------------------------------------------------------+
```

### 1.2 Execution Phases (Sequential)

RadioLib uses a synchronous/blocking API. The entire cycle runs in `setup()`, then the ESP32 enters deep sleep. On wake, the ESP32 restarts and `setup()` runs again. `loop()` is never reached. Each phase below is an `execute_phase_*` helper called in order from `setup()`; the abort-capable phases (PMIC/battery, radio/join) enter deep sleep directly instead of returning, so a failed phase never falls through to the next.

| Phase | Description | On Failure |
|-------|-------------|------------|
| 1. PMIC Setup | PMIC detect (AXP192/AXP2101), enable LoRa power, disable GPS | Continue without PMIC |
| 1b. Critical Battery Check | Read voltage, abort before radio if critical (unless USB present, in which case the device continues on USB power) | Extended sleep |
| 2. Sensor Init | ADC attenuation, DS18B20 detection | Continue (default-temp fallback, flagged in payload) |
| 3. Radio Init + Join/Restore | SX1262 init, restore session, OTAA activate | Sleep and retry |
| 5. Sensor Measurement | Resistance, temperature, MC calculation | Continue with available data |
| 6. Build Payload | Cayenne LPP encoding | Skip TX if empty |
| 7. LoRaWAN Uplink | sendReceive() with downlink check | Save session, sleep |
| 8. Deep Sleep | Timer wakeup configured | N/A |

The critical-battery read runs right after PMIC init (before the expensive radio init / OTAA join) so a critically low battery skips the radio entirely. The payload-facing battery voltage is re-read during Phase 5.

### 1.3 Execution Flow Diagram

```
               ESP32 Boot (reset/timer wake)
                        |
                        v
                   setup() begins
                        |
                        v
               Phase 1: PMIC Setup
              (PMIC init, power rails)
                        |
                        v
            Phase 1b: Critical Battery Check
                      |
            [critical voltage?]----> Extended sleep (unless USB present)
                      |
                      v
               Phase 2: Sensor Init
              (ADC config, DS18B20 detect)
                        |
                        v
               Phase 3: Radio Init
              (SX1262 begin, restore session)
                        |
                  activateOTAA()
                   /          \
          SESSION_RESTORED   NEW_SESSION
             |                    |
             |              Save nonces to NVS
             |                    |
             +--------+-----------+
                      |
            [activation failed?]----> Sleep + retry
                      |
                      v
              Phase 5: Measurement
           (resistance, temp, MC calc)
                      |
                      v
              Phase 6: Build Payload
                      |
            [empty payload?]--------> Skip to sleep
                      |
                      v
              Phase 7: sendReceive()
              (uplink + downlink RX)
                      |
              Save session to RTC
              Save nonces to NVS
                      |
                      v
              Phase 8: Deep Sleep
            (timer wakeup, restart)
```

---

## 2. Module Documentation

### 2.1 Power Management Module (AXP192 / AXP2101)

**File:** `src/main.cpp`, function `setup_axp()`

**Purpose:** Detect and initialize the power management IC (AXP192 on T-Beam v1.1, AXP2101 on v1.2) for battery monitoring and power distribution.

**Configuration:**
```cpp
#define USE_AXP_POWER_MANAGEMENT true
// AXP192_SLAVE_ADDRESS / AXP2101_SLAVE_ADDRESS defined by XPowersLib (both 0x34)
```

**Initialization Sequence (XPowersLib v0.1.9 API):**
1. Try `XPowersAXP192::init(Wire, 21, 22, ...)`; on failure try `XPowersAXP2101::init(...)` (same I2C address, distinguished by chip ID)
2. Enable the LoRa radio rail — `enableLDO2()` on AXP192, `enableALDO2()` on AXP2101
3. Cut GPS power — `disableLDO3()` / `disableALDO3()` (saves ~50mA)
4. `enableDC1()` — ESP32 core power
5. `setChargeTargetVoltage(..._CHG_VOL_4V2)` / `setChargerConstantCurr(..._CHG_CUR_100MA)` with the chip-specific constants

**Battery Monitoring:**
- Critical threshold: 3200 mV (operation stops, enters sleep)
- Low threshold: 3400 mV (sleep interval extended 2x)
- Critical sleep multiplier: 4x normal interval
- Reading: `pmic_batt_voltage()` wraps `getBattVoltage()` (millivolts) of whichever PMIC was detected

---

### 2.2 Sensor Layer Module

**File:** `include/sensor.h` (header-only, single-TU)

**Purpose:** Abstraction layer for all sensor operations - ADC initialization, DS18B20 temperature, resistive moisture measurement, and resistance validation.

This header defines `static` driver objects at header scope, so it must be included from `src/main.cpp` only. A second includer produces duplicate-symbol or split-state bugs (see `technical_debt.md` > Code structure).

**Initialization (`sensor_init()`):**
1. External path (`USE_EXTERNAL_ADC` true, shipped default): probe the ADS1115 at `ADS1115_I2C_ADDRESS` (0x48) on the shared PMIC I2C bus (SDA GPIO 21, SCL GPIO 22). This is a loud-failure design, not a silent fallback: if the ADS1115 does not respond, GPIO 35 is never touched, and every subsequent resistance reading returns open-circuit with the `adc_nonlinear` flag (LPP ch 8) set. Internal path (`USE_EXTERNAL_ADC` false): set ADC attenuation to ADC_11db (0-3.3V full range) on `MOISTURE_PROBE_ADC_PIN` (GPIO 35).
2. Detect DS18B20 on 1-Wire bus (GPIO 14)
3. Configure DS18B20 resolution (12-bit = 0.0625C)

**Temperature Reading Priority:**
1. DS18B20 (if detected) - direct wood temperature measurement
2. `DEFAULT_WOOD_TEMP_CELSIUS` (21 C) - fallback when no valid DS18B20 reading; the uplink is flagged on LPP channel 7 and the wood-temp channel is omitted

The ESP32 die temperature (`temprature_sens_read()`) is never used for the MC correction - it reads tens of degrees above ambient. It remains available on debug LPP channel 6 and in the serial output.

**Resistance Measurement (`read_wood_resistance_ohms(bool &adc_nonlinear)`):**
- Probe power pin must already be HIGH (toggled by main code)
- Waits for ADC stabilization delay
- Open-circuit gate on the raw count (calibration-independent)
- Samples the divider node: external path (default) via the ADS1115's `computeVolts()` on single-ended A0; internal path (`USE_EXTERNAL_ADC` false) via `analogReadMilliVolts()` (eFuse-calibrated). Either way, a trimmed mean (drop min+max) over the samples rejects EMI spikes
- Sets `adc_nonlinear` past `EXT_ADC_COMPRESSION_LIMIT_MV` on the external path (the divider losing sensitivity near V_top, not a converter knee - provisional) or past `ADC_LINEARITY_LIMIT_MV`/saturation on the internal path
- Handles edge cases: ADC saturation (open circuit) and near-zero (short circuit); on the external path an ADS1115-not-found condition also returns open-circuit with the flag set (see 5.1)
- Formula: `R_wood = R_pullup * V_node / (V_top - V_node)`

**Validation:**
- `is_resistance_in_valid_range()`: coarse sanity bounds (1 kOhm to ~1.55 MOhm), serial warning only
- Trustworthy-measurement signal is the ADC linearity/compression flag (`adc_nonlinear`, LPP ch 8), set on the node voltage (`EXT_ADC_COMPRESSION_LIMIT_MV` on the external path, `ADC_LINEARITY_LIMIT_MV` on the internal path), not on the resistance value

---

### 2.3 Session Manager Module

**File:** `include/session_manager.h` (header-only, single-TU)

**Purpose:** Persist RadioLib LoRaWAN session state across deep sleep and power cycles.

This header defines `RTC_DATA_ATTR` globals at header scope, so it must be included from `src/main.cpp` only. A second includer produces duplicate-symbol or split-state bugs (see `technical_debt.md` > Code structure).

RadioLib uses two internal buffers:

| Buffer | Size | Persistence | Changes |
|--------|------|-------------|---------|
| Nonces | `RADIOLIB_LORAWAN_NONCES_BUF_SIZE` | NVS flash (survives power loss) | Only on join |
| Session | `RADIOLIB_LORAWAN_SESSION_BUF_SIZE` | RTC RAM (survives deep sleep) | Every uplink |

**RadioLib API:**
- `node.getBufferNonces()` → returns `uint8_t*` to internal buffer
- `node.getBufferSession()` → returns `uint8_t*` to internal buffer
- `node.setBufferNonces(const uint8_t*)` → restores nonces before `activateOTAA()`
- `node.setBufferSession(const uint8_t*)` → restores session before `activateOTAA()`

**Operations:**
- `session_save_nonces()` — Called after successful OTAA join and after each uplink
- `session_load_nonces()` — Called on every boot before `activateOTAA()`
- `session_save_rtc()` — Called after every uplink (frame counters change)
- `session_restore_rtc()` — Called on warm boot before `activateOTAA()`
- `session_invalidate()` — Called on force-rejoin downlink or cold boot

**Validity Tracking:**
- A plain `RTC_DATA_ATTR bool rtc_session_valid` flag marks whether the RTC session buffer holds restorable data (RTC RAM zeroes on power-on reset, so the flag starts false on cold boot)
- If the flag is false, `session_restore_rtc()` reports no session and `activateOTAA()` performs a fresh join

---

### 2.4 Moisture Measurement

**Files:**
- `include/wood_mc_math.h` — `calculate_indicated_mc()` (`wood_mc` namespace; called from `main.cpp` and exercised by the native tests)
- `include/sensor.h` — `read_wood_resistance_ohms()`
- `include/wood_species_data.h` — Species coefficients (PROGMEM)

**Measurement Principle:**

The system uses a two-electrode resistive method based on the relationship:

```
R_wood = R_pullup x (ADC_reading / (ADC_max - ADC_reading))
```

Where:
- `R_pullup` = 100 kOhm (precision resistor)
- `ADC_max` = 4095 (12-bit resolution)

**Moisture Content Calculation (FPL GTR-6):**

```
M = 10^(A + B x log10(R_kOhm))
```

Where A and B are species-specific coefficients from FPL GTR-6 Table 1.

---

### 2.5 Temperature Compensation Module

**Files:**
- `include/wood_mc_math.h` — `get_temperature_correction()`, `bilinear_interpolation()` (`wood_mc` namespace; called from `main.cpp`)
- `include/wood_temp_correction_data.h` — FPL-GTR-6 Figure 5 correction grid (Celsius, digitized 2026-06-05)

**Algorithm:**

1. Read wood temperature (DS18B20 primary, `DEFAULT_WOOD_TEMP_CELSIUS` fallback)
2. Constrain temperature to the table range (-20 C to 50 C; the grid is in Celsius, no unit conversion)
3. Constrain indicated MC to the table range (6% - 25%)
4. Perform bilinear interpolation on the correction grid
5. Apply: `MC_corrected = MC_indicated + C_t`

---

### 2.6 LoRaWAN Communication Module

**Library:** RadioLib (pinned `^7.1.0`, resolves to 7.7.1 as of 2026-06-04)
**Region:** Runtime-selected from NVS on every boot in `execute_phase_radio_and_join()`; compile-time default is the `LORAWAN_REGION_DEFAULT` tunable in `config.h` (0=EU868, shipped default; 1=EU433) — single firmware binary for both board types
**Radio:** SX1262 (T-Beam v1.1/v1.2)
**Payload Format:** Cayenne LPP

**Region Detection (NVS-based):**
```cpp
const LoRaWANBand_t* region = &EU868;  // LORAWAN_REGION_DEFAULT == 0
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint8_t regionVal = prefs.getUChar(NVS_KEY_REGION, LORAWAN_REGION_DEFAULT);
    prefs.end();
    if (regionVal == 1) region = &EU433;
}
node = new LoRaWANNode(&radio, region);
```
Region is stored in NVS (`lorawan` namespace, `region` key: 0=EU868, 1=EU433). Changeable via downlink command `0x05`. On first boot with no NVS value, the read falls back to the compile-time `LORAWAN_REGION_DEFAULT` (shipped as EU868).

**Radio Initialization:**
```cpp
SX1262 radio = new Module(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
// LoRaWANNode constructed dynamically after region detection
node = new LoRaWANNode(&radio, region);
radio.begin();
```

**Join Procedure (OTAA):**
1. Load nonces from NVS and session from RTC RAM
2. Call `node.setBufferNonces()` and `node.setBufferSession()` if data available
3. Call `node.beginOTAA(joinEUI, devEUI, nwkKey, appKey)`
4. Call `node.activateOTAA()` — this is blocking
5. Returns `RADIOLIB_LORAWAN_SESSION_RESTORED` (-1117) or `RADIOLIB_LORAWAN_NEW_SESSION` (-1118), or error

**Uplink with Downlink:**
```cpp
int txResult = node.sendReceive(data, len, fPort, downBuf, &downLen);
// txResult >= 0: success (0 = no downlink, >0 = downlink fPort)
// txResult < 0: error code
```

### 2.7 Downlink Command Processing

**File:** `src/main.cpp`, function `process_downlink()`

**Commands:**

| Byte | Command | Payload | Action |
|------|---------|---------|--------|
| 0x01 | Set Interval | 2 bytes (uint16 big-endian, minutes) | Adjusts `current_interval_seconds` |
| 0x02 | Set Species | 1 byte (species index) | Changes `selected_species_index` |
| 0x03 | Force Rejoin | None | Invalidates session, will rejoin on next boot |
| 0x04 | Set TX Power | 1 byte (power index) | Reserved: parsed and logged, but not applied — ADR manages TX power |
| 0x05 | Set Region | 1 byte (0=EU868, 1=EU433) | Persists to NVS, invalidates session, rejoin on next boot |

---

## 3. Memory Layout

### 3.1 Build Size (RadioLib pinned `^7.1.0`, resolves to 7.7.1 as of 2026-06-04)

| Resource | Used | Available | % |
|----------|------|-----------|---|
| RAM | 26,232 bytes | 1,310,720 bytes | 2.0% |
| Flash | 424,713 bytes | 1,310,720 bytes | 32.4% |

### 3.2 PROGMEM Usage

Large constant data structures are stored in flash (PROGMEM) to conserve RAM:

| Data Structure | Size | Location |
|----------------|------|----------|
| `species_data[]` | ~140 bytes | Flash |
| Species name strings | ~200 bytes | Flash (separate PROGMEM arrays) |
| `temp_points_c[]` | 60 bytes | Flash |
| `mc_points_indicated[]` | 80 bytes | Flash |
| `correction_table[][]` | 1200 bytes | Flash |
| LoRaWAN keys | ~50 bytes | Flash |

### 3.3 RTC_DATA_ATTR Usage

Variables preserved across deep sleep:

```cpp
RTC_DATA_ATTR uint32_t current_interval_seconds;
RTC_DATA_ATTR uint8_t join_retry_count;
RTC_DATA_ATTR uint8_t selected_species_index;       // Changeable via downlink
RTC_DATA_ATTR bool rtc_session_valid;               // session_manager.h
RTC_DATA_ATTR uint8_t rtc_session_buf[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];  // session_manager.h
```

---

## 4. Power Management

### 4.1 Deep Sleep Implementation

**Function:** `deep_sleep_with_timer(uint32_t seconds)`

- Disables watchdog timer before entering sleep
- Configures ESP32 timer wakeup
- Flushes serial buffer
- Calls `esp_deep_sleep_start()`

**Current Consumption Estimates:**

| State | Current (approx.) |
|-------|-------------------|
| Active (TX) | ~120 mA |
| Active (measuring) | ~50 mA |
| Deep Sleep | ~10-15 uA |

### 4.2 Battery-Aware Sleep

The sleep interval is dynamically adjusted based on battery voltage:

| Battery Level | Threshold | Sleep Multiplier |
|---------------|-----------|------------------|
| Normal | > 3.4V | 1x (default interval) |
| Low | < 3.4V | 2x |
| Critical | < 3.2V | 4x |

Both multipliers are skipped while USB power is present (`pmic_usb_present()`, covering both AXP192 and AXP2101) — on USB the device runs at the normal interval regardless of the reported battery voltage.

### 4.3 Sensor Power Control

The moisture probe is powered only during measurement:

```
Phase 5: digitalWrite(MOISTURE_PROBE_POWER_PIN, HIGH)  // Power up
Phase 5: ... measurement ...
Phase 5: digitalWrite(MOISTURE_PROBE_POWER_PIN, LOW)   // Power down
```

### 4.4 GPS Disabled

LDO3 (GPS power) is explicitly disabled during PMIC setup via `PMU->disableLDO3()`, saving approximately 50mA of continuous current draw.

---

## 5. Error Handling

### 5.1 Validation Errors

| Condition | Action |
|-----------|--------|
| Battery < 3200 mV | Skip measurement, enter sleep immediately — unless USB present (continues on USB power) |
| ADC saturated high (>= 4094) | Return 1e12 ohm (open circuit) |
| ADC near zero (< 1) | Return 1e-3 ohm (short circuit) |
| Resistance out of range | Print warning, continue with reading |
| ADS1115 not found (external path) | Return open-circuit reading, flag uplink (LPP channel 8) - no silent fallback to the internal ADC |
| DS18B20 read error | Correct MC with `DEFAULT_WOOD_TEMP_CELSIUS`, flag uplink (LPP channel 7) |

### 5.2 LoRaWAN Errors

| Condition | Action |
|-----------|--------|
| Radio init failed | Sleep 30s, retry on next boot |
| Join failed | Sleep 30s, retry (max 5 times) |
| Max join retries | Extended sleep (150s), reset counter |
| TX failed | Save session (preserve frame counter), sleep |

### 5.3 Hardware Watchdog

- ESP32 Task Watchdog Timer configured at 120 seconds
- Reset at key points during measurement
- Disabled before entering deep sleep
- Causes hardware reset (panic) if execution hangs

---

## 6. Firmware Versioning

The version lives in `config.h` as `FIRMWARE_VERSION_MAJOR` / `_MINOR` / `_PATCH` defines (single source of truth; the README header mirrors it). It is printed at startup for debugging and deployment tracking - correlate logged data with the firmware that produced it via the boot banner.

---

## 7. Debug Output

Debug macros (config.h):

```cpp
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINT(x) Serial.print(x)
#define SERIAL_BAUD 115200
```

**Sample Boot Output:**
```
========================================
 Wood Moisture Sensor (LoRaWAN/RadioLib)
========================================
Firmware v1.3.0
Wake reason: Timer
Woke from deep sleep.
Selected species [0]: Douglas-Fir (Coast)
[Phase] PMIC Setup
Init PMIC (AXP192/AXP2101)...
PMIC: AXP2101 (T-Beam v1.2) initialized OK.
[Battery] Voltage: 3.89 V
[Phase] Sensor Init
[Sensor] ADS1115 found (external ADC, A0). Addr: 0x48
[Sensor] DS18B20 found! Devices: 1, Resolution: 12 bits
[Region] EU868
[Phase] Radio Init
[Radio] SX1262 initialized OK
[Session] Nonces loaded from NVS (16 bytes).
[Session] Session restored from RTC RAM.
[LoRaWAN] Activating OTAA...
[LoRaWAN] Session restored from saved state!
[Phase] Sensor Measurement
[Sensor] Node voltage (trimmed mean): 1034.60 mV
Wood Resistance: 45.67 kOhms
Indicated MC: 12.34 %
[Sensor] DS18B20 temp: 22.5 C
Temp Correction: -0.1 % MC
FINAL Corrected MC: 12.24 %
[Phase] Build Payload
[Phase] LoRaWAN Uplink
[TX] Uplink successful!
[Session] Session saved to RTC RAM.
[Session] Nonces saved to NVS.
[Sleep] Entering deep sleep for 3600 seconds
Entering deep sleep...
```

---

## 8. Compilation and Build

### 8.1 PlatformIO Configuration

**File:** `platformio.ini`

```ini
[env:ttgo-t-beam]
platform = espressif32
board = ttgo-t-beam
framework = arduino
lib_deps =
    jgromes/RadioLib @ ^7.1.0
    electroniccats/CayenneLPP @ ^1.6.0
    lewisxhe/XPowersLib @ ^0.1.9
    paulstoffregen/OneWire @ ^2.3.8
    milesburton/DallasTemperature @ ^3.11.0
build_flags =
    -I include
```

RadioLib does not require build flags for region/radio selection — both EU433 and SX1262 are configured in code.

---

## 9. Modification Guidelines

### 9.1 Adding New Wood Species

1. Edit `wood_species_data.h`
2. Add a PROGMEM name string: `static const char SPECIES_NAME_N[] PROGMEM = "Name";`
3. Add entry to `species_data[]` with name, A, B coefficients
4. `NUM_WOOD_SPECIES` updates automatically via sizeof
5. Change `SELECTED_WOOD_SPECIES_INDEX` in `config.h` or use downlink command 0x02

### 9.2 Changing Measurement Interval

Edit `config.h`:
```cpp
#define NORMAL_SEND_INTERVAL_SECONDS (60 * 60)  // 1 hour
```

Or send downlink command `0x01` with interval in minutes.

### 9.3 Disabling Temperature Compensation

Edit `config.h`:
```cpp
#define ENABLE_TEMPERATURE_COMPENSATION false
```

### 9.4 Changing LoRaWAN Region

Two ways to change region, depending on whether you need it at first boot or in the field:

1. **Compile-time default:** In `config.h`, set `LORAWAN_REGION_DEFAULT` (0=EU868, 1=EU433) before flashing. This is what a fresh device uses until it receives a Set Region downlink.
2. **Runtime (remote):** Send downlink command `0x05` with the desired region byte. The value is bounds-checked, persisted to NVS, and forces a rejoin so the new region takes effect on the next boot.

RadioLib has built-in region definitions for both EU868 and EU433 — no build flags needed either way. For sub-band selection on other regions (e.g., US915), use `node.selectSubband(1)` before join.

### 9.5 LoRaWAN Key Format

RadioLib uses different key formats than MCCI LMIC:

| Key | Format | Example |
|-----|--------|---------|
| JoinEUI (AppEUI) | `uint64_t` MSB hex literal | `0x70B3D57ED0000001` |
| DevEUI | `uint64_t` MSB hex literal | `0x0000000000000001` |
| NwkKey (AppKey 1.0.x) | `uint8_t[16]` MSB byte array | `{0x01, 0x02, ..., 0x10}` |
| AppKey | `uint8_t[16]` MSB byte array | Same as NwkKey for 1.0.x |

---

## 10. Related Documentation

- [Calibration Procedure](calibration_procedure.md)
- [Deployment Guide](deployment_guide.md)
- [Data Interpretation](data_interpretation.md)

---

**Document Version:** 2.0
**Last Updated:** 2026-07-02
**Author:** Master Thesis Project, Wood Technologies
