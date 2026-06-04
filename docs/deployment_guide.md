# Installation and Deployment Guide

## Overview

This document provides comprehensive instructions for deploying the LoRaWAN Wood Moisture Monitoring System in field conditions. It covers site selection, physical installation, network configuration, and ongoing maintenance.

**Application:** Master Thesis in Wood Technologies - Field Deployment
**System:** LoRaWAN-connected resistive moisture monitoring (TTGO T-Beam v1.1/v1.2)
**Firmware Version:** 2.0.0

---

## 1. Pre-Deployment Planning

### 1.1 Site Selection Criteria

**Wood Material Considerations:**

| Factor | Recommendation | Rationale |
|--------|----------------|-----------|
| Wood species | Known, from coefficient table | Accurate MC calculation |
| Dimensions | Minimum 50 mm thickness | Representative moisture profile |
| Condition | Sound, no decay | Valid resistance measurements |
| Location | Protected from direct weather | Prevent probe corrosion |

**Environmental Considerations:**

| Factor | Optimal Range | Notes |
|--------|---------------|-------|
| Ambient temperature | 0-40 C | ESP32 operating range |
| Relative humidity | 20-95% RH | Non-condensing preferred |
| LoRaWAN coverage | RSSI > -120 dBm | Verify with network server |
| Accessibility | Periodic access required | Maintenance and validation |

### 1.2 Equipment Checklist

**Required:**
- [ ] TTGO T-Beam v1.1/v1.2 device with current firmware loaded (version per `config.h`, printed at boot)
- [ ] Moisture probe (2-electrode resistive type)
- [ ] 100 kOhm pull-up resistor (1% tolerance, 1/4W)
- [ ] Enclosure (IP65 or higher rating)
- [ ] Li-ion/LiPo battery (3.7V, 18650 type, charged)
- [ ] Mounting hardware (screws, cable ties)
- [ ] Silicone sealant or epoxy

**Recommended:**
- [ ] DS18B20 1-Wire temperature sensor (waterproof variant, for direct wood temperature)
- [ ] 4.7 kOhm pull-up resistor for DS18B20 data line
- [ ] Solar panel (for extended deployments)
- [ ] Reference moisture meter (for validation)
- [ ] Data sheet for field notes

**Tools:**
- [ ] Drill with wood bits
- [ ] Wire strippers/crimpers
- [ ] Multimeter
- [ ] Soldering iron
- [ ] Screwdrivers
- [ ] Heat gun or lighter (heat shrink)

---

## 2. Hardware Assembly

### 2.1 Moisture Probe Wiring

**Circuit Diagram:**

```
             GPIO 25 (probe power, HIGH only during measurement)
                      |
                 [R_pullup]
                   100 kOhm
                      |
                      +--------> To GPIO 35 (ADC input)
                      |
                   Probe 1
                      |
                  [WOOD]
                      |
                   Probe 2
                      |
                     GND
```

**Important Notes:**
- The firmware drives GPIO 25 (`MOISTURE_PROBE_POWER_PIN`) HIGH only during the measurement window, so the probe sees no continuous DC bias (prevents electrode corrosion and wood polarization) and draws no current between cycles.
- Do NOT wire the pull-up to 3.3V directly; the divider must hang off GPIO 25 or the readings will be valid but the probe stays permanently energized.
- GPIO 35 is input-only (ADC1_CH7) - it cannot be repurposed as an output. GPIO 32 must stay free: it is the LoRa radio's BUSY line.

**Wiring Steps:**

1. **Cut probe wires** to desired length (recommend 1-2 m for flexibility)

2. **Solder connections:**
   ```
   Probe Wire 1 --+-- 100k resistor -- GPIO 25 (probe power)
                   +-- Wire to GPIO 35 (ADC)
   Probe Wire 2 -- GND
   ```

3. **Insulate connections:**
   - Use heat shrink tubing
   - Apply silicone sealant over exposed metal
   - Prevent moisture ingress at solder joints

4. **Test continuity:**
   ```
   Multimeter check:
   - GPIO 25 to GPIO 35: ~100 kOhm (through resistor)
   - Probe 1 to Probe 2 (in air): > 10 MOhm
   ```

### 2.2 DS18B20 Temperature Sensor Wiring (Recommended)

The DS18B20 provides direct wood/ambient temperature measurement, significantly improving temperature compensation accuracy over the ESP32 internal sensor.

**Wiring (parasite power mode NOT used -- use normal 3-wire mode):**

```
DS18B20 Pin     ESP32 Pin
-----------     ---------
VDD (red)   --> 3.3V
GND (black) --> GND
DQ  (yellow)--> GPIO 14 (ONEWIRE_PIN)
                  |
             [4.7k pull-up]
                  |
                 3.3V
```

**Installation Tips:**
- Use the waterproof stainless steel probe variant for outdoor use
- Place the DS18B20 sensor in direct contact with the wood surface, or insert into a small hole near the moisture electrodes
- Secure with tape or silicone, then route the cable to the enclosure
- The firmware auto-detects the DS18B20 at boot; if not found, the MC correction uses `DEFAULT_WOOD_TEMP_CELSIUS` and the uplink is flagged (LPP channel 7) so affected readings can be filtered

### 2.3 Enclosure Preparation

**Mounting Layout:**

```
+-------------------------------------+
|  +-----------------------------+    |
|  |         TTGO T-Beam         |    |
|  |     +---------------+       |    |
|  |     |   ESP32 +     |       |    |
|  |     |   SX1262 LoRa |       |    |
|  |     +---------------+       |    |
|  |         Antenna             |    |
|  +-----------------------------+    |
|                                      |
|  +-----------------------------+    |
|  |    Terminal Block /         |    |
|  |    Connection Strip         |    |
|  |  +---+---+---+---+---+     |    |
|  |  |3V3|G32|G14|GND|BAT|     |    |
|  |  +---+---+---+---+---+     |    |
|  +-----------------------------+    |
|                                      |
|  +-----------------------------+    |
|  |      Battery (18650)        |    |
|  +-----------------------------+    |
|                                      |
|         Cable Gland(s)              |
|         -> Moisture Probe           |
|         -> DS18B20 Sensor           |
+-------------------------------------+
```

**Weatherproofing:**

1. **Cable entry:**
   - Use cable gland(s) for probe wires and DS18B20 cable
   - Alternative: Drill small hole, seal with silicone

2. **Antenna exit:**
   - Use bulkhead SMA connector
   - Seal around connector with silicone

3. **Conformal coating (optional):**
   - Apply to PCB (avoid connectors and antenna)
   - Protects against humidity

---

## 3. Probe Installation

### 3.1 Installation Methods

**Method A: Surface Mount (for lumber/stacked wood)**

```
    Wood Surface
    ============
         |
    +----+----+
    |  Probe  | <-- Inserted into wood
    |  +-+ +-+|
    |  | | | || <-- Electrodes
    +--+-+-+-++
         |
    Cable to enclosure
```

**Procedure:**
1. Mark electrode positions (spacing per probe design)
2. Pre-drill pilot holes (slightly smaller than electrode diameter)
3. Drive electrodes flush with wood surface
4. Seal around electrodes with silicone

**Method B: Edge Mount (for thick timber)**

Electrodes are inserted from the edge, perpendicular to grain.

**Method C: Buried Probe (for internal monitoring)**

Electrodes are buried deeper with cable routed through a slot.

### 3.2 Electrode Spacing

**Recommended Spacing:**

| Wood Thickness | Spacing | Electrode Depth |
|----------------|---------|-----------------|
| 25-50 mm | 20-30 mm | 10-15 mm |
| 50-100 mm | 30-50 mm | 15-25 mm |
| >100 mm | 50-75 mm | 25-40 mm |

**Note:** Spacing affects measurement volume. Consistent spacing is critical for comparable readings.

### 3.3 Installation Best Practices

1. **Grain Orientation:**
   - Install electrodes parallel to grain when possible
   - Resistance across grain is ~1.5-2x higher than along grain

2. **Avoid Defects:**
   - Stay away from knots, cracks, checks
   - Avoid areas with visible decay or insect damage

3. **DS18B20 Placement:**
   - Place sensor tip in contact with wood surface near electrodes
   - Insert into a shallow (5 mm) drilled hole for better thermal coupling
   - Seal with silicone or thermal paste

4. **Multiple Probes:**
   - Install 2-3 probes at different locations
   - Average readings for representative value
   - Or monitor separately for moisture gradient

5. **Labeling:**
   - Tag each probe with ID
   - Document location on site diagram
   - Record installation date

---

## 4. LoRaWAN Network Configuration

### 4.1 Network Server Setup

**The Things Network (TTN) v3:**

1. **Create Application:**
   - Go to console.thethingsnetwork.org
   - Create new application
   - Note Application ID

2. **Register Device:**
   - Select "Add end device"
   - Choose "Manually add"
   - Select frequency plan: **EU_433**
   - Choose LoRaWAN version: **1.0.3**
   - Regional Parameters: **RP001-1.0.3-RevB**

3. **Configure Device:**
   - Activation mode: **Over The Air Activation (OTAA)**
   - Copy Device EUI, Application EUI, Application Key
   - Paste into `include/lorawan_keys.h`
   - **IMPORTANT:** EUIs must be in **MSB** (most-significant-byte-first) format as `uint64_t` hex literals
   - **IMPORTANT:** Keys (nwkKey, appKey) must be in **MSB** byte order as `uint8_t[16]` arrays

4. **Set Payload Formatter:**
   - Go to Integrations -> Payload Formatters
   - Select "Cayenne LPP" from dropdown
   - Click "Add formatter"

**Alternative Networks:**
- Helium (now Helium IoT)
- ChirpStack (self-hosted)
- Actility ThingPark
- Orange Live Objects

### 4.2 Coverage Verification

**Before Deployment:**

1. **Use smartphone gateway app** (if available):
   - TTN Mapper
   - Helium App
   - Check signal strength at site

2. **Test with device:**
   - Power on at installation location
   - Monitor join attempts via serial (115200 baud)
   - Check RSSI/SNR in network server

**Signal Quality Guidelines:**

| Metric | Excellent | Good | Marginal | Poor |
|--------|-----------|------|----------|------|
| RSSI (dBm) | > -80 | -80 to -100 | -100 to -120 | < -120 |
| SNR (dB) | > 10 | 5 to 10 | 0 to 5 | < 0 |

**Antenna Positioning:**
- Orient antenna vertically
- Elevate above obstructions
- Avoid metal enclosures
- Consider external antenna with cable

---

## 5. Firmware Configuration

### 5.1 Species Selection

Edit `include/config.h`:

```cpp
// Available species (from wood_species_data.h):
// 0: Douglas-Fir (Coast)
// 1: Pine, Southern Yellow (Loblolly)
// 2: Spruce, Sitka
// 3: Oak, Red
// 4: Maple, Sugar
// 5: Generic Softwood (Avg)
// 6: Generic Hardwood (Avg)

#define SELECTED_WOOD_SPECIES_INDEX 0  // Change as needed
```

Species can also be changed remotely via LoRaWAN downlink (command `0x02` + 1 byte species index).

### 5.2 Measurement Interval

```cpp
// Default: 1 hour
#define NORMAL_SEND_INTERVAL_SECONDS (60 * 60)

// For faster monitoring (battery will deplete faster):
// #define NORMAL_SEND_INTERVAL_SECONDS (15 * 60)  // 15 minutes

// For extended deployment:
// #define NORMAL_SEND_INTERVAL_SECONDS (4 * 60 * 60)  // 4 hours
```

The interval can also be changed remotely via LoRaWAN downlink (command `0x01` + 2 bytes uint16 big-endian, in minutes).

**Battery Life Estimates:**

| Interval | Estimated Life (2000 mAh) |
|----------|---------------------------|
| 15 minutes | ~2 weeks |
| 30 minutes | ~1 month |
| 1 hour | ~2 months |
| 4 hours | ~6 months |
| 24 hours | ~1 year |

**Battery-Aware Sleep:** When battery drops below `LOW_BATTERY_THRESHOLD_V` (3.4V), the firmware automatically doubles the sleep interval. Below `CRITICAL_BATTERY_THRESHOLD_V` (3.2V), the interval is quadrupled to conserve remaining energy.

### 5.3 Session Persistence

The firmware v2.0.0 includes LoRaWAN session persistence via NVS (nonces) and RTC memory (session). This means:
- After a successful OTAA join, the LoRaWAN nonces are saved to NVS and session state is saved to RTC memory
- On subsequent wakes from deep sleep, the session is restored without re-joining
- This saves airtime, battery, and reduces time-to-transmit
- If the session becomes invalid (e.g., server-side reset), the device automatically falls back to a fresh OTAA join
- A forced re-join can be triggered via downlink command `0x03`

### 5.4 Downlink Commands

The firmware accepts the following downlink commands on any port:

| Command | Byte | Payload | Description |
|---------|------|---------|-------------|
| Set Interval | 0x01 | 2 bytes (uint16 BE, minutes) | Change measurement/send interval |
| Set Species | 0x02 | 1 byte (species index) | Change wood species |
| Force Re-join | 0x03 | (none) | Invalidate session, force fresh OTAA join |
| Set TX Power | 0x04 | 1 byte (power index) | Adjust LoRa transmit power |

### 5.5 Optional Data Channels

Edit `src/main.cpp` to uncomment the optional Cayenne LPP channels in the payload-building phase:

```cpp
// Uncomment to send debug data:
if (last_mc_indicated >= 0)
    lpp.addAnalogInput(LPP_CHANNEL_INDICATED_MC, last_mc_indicated);
if (last_R_kOhms >= 0)
    lpp.addAnalogInput(LPP_CHANNEL_RESISTANCE, last_R_kOhms);
if (last_esp_temp_c > -50)
    lpp.addTemperature(LPP_CHANNEL_ESP_TEMP, last_esp_temp_c);
```

**Note:** Additional channels increase payload size. LoRaWAN limits payload to 51 bytes (EU433, SF12).

---

## 6. Commissioning

### 6.1 Pre-Installation Testing

**Bench Test Checklist:**

- [ ] Connect battery, verify power LED
- [ ] Open serial monitor (115200 baud)
- [ ] Verify boot message with firmware version (v2.0.0)
- [ ] Confirm PMIC initialization ("PMIC Ok.")
- [ ] Verify LoRaWAN join success ("LoRaWAN join successful" or "Session restored" in serial output)
- [ ] Check sensor reading output:
  ```
  Wood Resistance: XXXX kOhms
  Indicated MC: XX.X %
  DS18B20 Temp: XX.X C  (or "DS18B20 not found, using ESP32 internal")
  ESP32 Temp: XX.X C
  FINAL Corrected MC: XX.X %
  ```
- [ ] Confirm successful uplink ("sendReceive returned:" with non-negative value)
- [ ] Verify data appears on network server (Cayenne LPP decoded)
- [ ] Power cycle and verify session persistence (second boot should skip join)
- [ ] Test watchdog by observing normal operation (should not reset in 120s)

### 6.2 Field Installation

**Step-by-Step:**

1. **Site Survey:**
   - Verify LoRaWAN coverage
   - Identify probe locations
   - Plan cable routing for both moisture probe and DS18B20

2. **Install Probes:**
   - Follow Section 3 procedures
   - Label each probe
   - Document GPS coordinates (if applicable)

3. **Install DS18B20:**
   - Place sensor in contact with wood near moisture electrodes
   - Secure and waterproof the connection
   - Route cable to enclosure

4. **Mount Enclosure:**
   - Install in protected location
   - Orient antenna vertically
   - Secure against animals/weather

5. **Connect Everything:**
   - Terminate wires at terminal block
   - Verify connections with multimeter
   - Seal cable entries

6. **Power On:**
   - Connect battery
   - Observe LED behavior
   - Monitor serial output (if accessible)

7. **Verify Transmission:**
   - Check network server for incoming data
   - Confirm expected measurement values
   - Document installation parameters

### 6.3 Documentation

**Installation Record:**

```
Site ID: _______________
Date: _______________
Installer: _______________

Location:
  GPS: _______________
  Description: _______________

Probe Installation:
  Probe ID: _______________
  Wood Species: _______________
  Dimensions: _______________
  Orientation: _______________
  DS18B20 Installed: Yes / No
  Notes: _______________

Device Configuration:
  Device EUI: _______________
  Firmware Version: 2.0.0
  Species Index: _______________
  Send Interval: _______________
  Session Persistence: Enabled

Initial Readings:
  MC: _______________ %
  DS18B20 Temp: _______________ C
  ESP32 Temp: _______________ C
  Battery: _______________ V
  RSSI: _______________ dBm
  SNR: _______________ dB
```

---

## 7. Maintenance

### 7.1 Routine Checks

**Weekly (First Month):**
- Verify data arrival on server
- Check for gaps in transmission
- Compare with reference meter (if accessible)

**Monthly:**
- Review battery voltage trend
- Inspect physical condition
- Validate MC readings

**Quarterly:**
- Full site inspection
- Probe and DS18B20 condition assessment
- Recalibration if needed

### 7.2 Battery Management

**Voltage Thresholds:**

| Voltage | Status | Action |
|---------|--------|--------|
| > 3.7V | Full | Normal operation |
| 3.5-3.7V | Good | Normal operation |
| 3.4-3.5V | Low | Firmware auto-doubles interval |
| 3.2-3.4V | Critical | Firmware auto-quadruples interval |
| < 3.2V | Shutdown | Device enters extended deep sleep |

**Replacement Procedure:**
1. Note current battery voltage from server
2. Visit site with charged replacement
3. Disconnect old battery
4. Connect new battery
5. Verify device wakes and transmits (session should restore from RTC memory)
6. Record replacement date

### 7.3 Probe Maintenance

**Signs of Probe Degradation:**
- Erratic readings
- Gradually increasing resistance
- Visible corrosion on electrodes
- Physical damage

**Cleaning Procedure:**
1. Disconnect probe
2. Clean electrodes with fine sandpaper
3. Wipe with isopropyl alcohol
4. Inspect for pitting/corrosion
5. Replace if severely corroded

### 7.4 Remote Management

The firmware supports remote configuration via LoRaWAN downlink commands:
- Change measurement interval without site visit
- Switch wood species if specimen changes
- Force re-join if network issues arise
- Adjust TX power for signal optimization

Schedule downlinks via your network server console (TTN: Applications -> Messaging -> Downlink).

---

## 8. Data Management

### 8.1 Data Retrieval

**From TTN:**
1. Go to Applications -> [Your App] -> Data
2. Export as JSON or CSV
3. Import into analysis software

**Automated Export:**
- Set up AWS/Azure integration
- Use TTN Webhooks
- Schedule periodic exports

### 8.2 Cayenne LPP Channel Mapping

| Channel | Field | Unit |
|---------|-------|------|
| 1 | Wood MC (corrected) | % |
| 2 | Wood Temperature | C |
| 3 | Indicated MC (raw) | % |
| 4 | Resistance | kOhm |
| 5 | Battery Voltage | V |
| 6 | ESP32 Internal Temp | C |
| 7 | Temp Fallback Flag (1 = corrected with default temp) | 0/1 |

### 8.3 Data Quality Checks

**Automated Validation:**

```python
def validate_reading(mc, temp, battery, rssi):
    """Return quality flag and issues list"""
    issues = []

    if battery < 3.2:
        issues.append("LOW_BATTERY")
    if mc < 6 or mc > 30:
        issues.append("OUT_OF_RANGE")
    if rssi < -120:
        issues.append("POOR_SIGNAL")
    if temp < -10 or temp > 50:
        issues.append("TEMP_ANOMALY")

    if issues:
        return "SUSPECT", issues
    return "VALID", []
```

**Manual Review:**
- Plot time series for each probe
- Look for step changes (may indicate probe failure or session re-join)
- Check for diurnal temperature patterns
- Compare DS18B20 temp with ESP32 internal temp to verify sensor health

---

## 9. Troubleshooting

### 9.1 Common Deployment Issues

| Problem | Possible Cause | Solution |
|---------|----------------|----------|
| No data received | LoRaWAN coverage | Relocate antenna, use external antenna |
| Device won't join | Incorrect keys | Verify EUIs (MSB!) and keys (MSB!) in lorawan_keys.h |
| Erratic MC readings | Poor probe contact | Reinstall probes, check wiring |
| Rapid battery drain | High transmission rate | Increase send interval (or use downlink cmd 0x01) |
| All readings same | Probe failure | Check continuity, replace probe |
| Temperature offset | ESP32 self-heating | Install DS18B20, or add offset correction |
| DS18B20 reads -127 C | Wiring issue | Check 4.7k pull-up, verify GPIO 14 connection |
| Session not restoring | NVS/RTC memory corruption | Force re-join (downlink cmd 0x03) or power cycle |

### 9.2 Serial Debug Output

**Normal Boot Sequence (first join):**
```
=== Wood Moisture Sensor v2.0.0 ===
Wake reason: Timer
Selected Wood Species: Douglas-Fir (Coast)
--- Phase 1: PMIC Setup ---
PMIC Ok.
--- Phase 2: Sensor Init ---
DS18B20 found on GPIO 14
--- Phase 3: Radio Init ---
SX1262 init OK
Restoring LoRaWAN session...
activateOTAA: NEW_SESSION (fresh join)
LoRaWAN join successful!
Saving nonces to NVS...
--- Phase 4: Battery Check ---
Battery: 3890 mV (3.89 V)
--- Phase 5: Sensor Measurement ---
DS18B20 Temp: 22.3 C
Avg Raw ADC: 1234.56
Wood Resistance: 45.67 kOhms
Indicated MC: 12.34 %
Temp Correction Factor: 0.1 % MC
FINAL Corrected MC: 12.44 %
--- Phase 6: Build Payload ---
Cayenne LPP payload: 15 bytes
--- Phase 7: LoRaWAN Uplink ---
sendReceive returned: 0 (success, no downlink)
Saving session to RTC...
--- Phase 8: Deep Sleep ---
Sleeping for 3600 s.
```

**Normal Boot Sequence (session restored):**
```
=== Wood Moisture Sensor v2.0.0 ===
Wake reason: Timer
Selected Wood Species: Douglas-Fir (Coast)
--- Phase 1: PMIC Setup ---
PMIC Ok.
--- Phase 2: Sensor Init ---
DS18B20 found on GPIO 14
--- Phase 3: Radio Init ---
SX1262 init OK
Restoring LoRaWAN session...
activateOTAA: SESSION_RESTORED
--- Phase 4: Battery Check ---
...
```

### 9.3 Error Messages

| Message | Meaning | Action |
|---------|---------|--------|
| "PMIC init failed" | No AXP192/AXP2101 responding | Check I2C connections |
| "activateOTAA failed" | LoRaWAN join failed | Verify keys, check coverage |
| "Invalid resistance reading" | ADC reading out of range | Check probe wiring |
| "CRITICAL: Battery voltage too low" | Below 3.2V | Replace battery |
| "sendReceive failed" | LoRaWAN TX error | Check antenna, coverage |
| "DS18B20 not found" | Sensor not on bus | Check wiring (GPIO 14, 4.7k pull-up) |
| "Session invalid, re-joining" | RTC/NVS session corrupted | Automatic recovery |

---

## 10. Decommissioning

### 10.1 Site Restoration

1. **Remove probes:**
   - Carefully extract from wood
   - Fill holes if required (wood preservation)
   - Remove DS18B20 sensor

2. **Remove enclosure:**
   - Document final condition
   - Note any environmental effects

3. **Dispose of batteries:**
   - Follow local regulations
   - Recycle Li-ion batteries appropriately

### 10.2 Data Archive

1. **Export all data** from network server
2. **Document metadata:**
   - Site information
   - Calibration records
   - Maintenance log
   - Firmware version and configuration
3. **Store in thesis repository** or institutional archive

---

## Appendix A: Tools and Templates

### A.1 Installation Checklist Template

```
Pre-Deployment:
[ ] Firmware v2.0.0 uploaded and tested
[ ] LoRaWAN keys configured (MSB for EUIs and keys)
[ ] Species selected
[ ] DS18B20 wired and verified
[ ] Enclosure prepared
[ ] Probes wired and tested
[ ] Battery charged

Field Installation:
[ ] Site surveyed
[ ] Probes installed
[ ] DS18B20 installed near electrodes
[ ] Enclosure mounted
[ ] Connections verified
[ ] Device powered on
[ ] Data confirmed on server
[ ] Session persistence confirmed (power cycle test)

Documentation:
[ ] Installation record completed
[ ] Photos taken
[ ] GPS coordinates recorded
[ ] Initial readings documented
```

### A.2 Maintenance Log Template

```
Date: _______________
Site: _______________
Technician: _______________

Observations:
[ ] Normal operation
[ ] Probe inspection needed
[ ] DS18B20 inspection needed
[ ] Battery replacement needed
[ ] Other: _______________

Readings:
MC Probe 1: _____ %
MC Probe 2: _____ %
Reference: _____ %
DS18B20 Temp: _____ C
Battery: _____ V

Actions Taken:
_________________________________
_________________________________

Downlink Commands Sent:
_________________________________

Next Visit Due: _______________
```

---

**Document Version:** 2.0
**Last Updated:** 2026-03-17
**Author:** Master Thesis Project, Wood Technologies
