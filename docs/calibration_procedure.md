# Calibration Procedure for Wood Moisture Probes

## Overview

This document describes the calibration procedure for resistive wood moisture probes used in the LoRaWAN Wood Moisture Monitoring System. The procedure follows guidelines from USDA Forest Products Laboratory (FPL) GTR-6 and adapts them for the embedded system implementation.

**Standard Reference:** James, W.L. (1988). *Electric Moisture Meters for Wood* (FPL-GTR-6)
**Secondary Reference:** USDA FPL (2021). *Wood Handbook: Wood as an Engineering Material* (FPL-GTR-190), moisture relations chapters
**Application:** Master Thesis in Wood Technologies - Field Deployment Validation

---

## 1. Theoretical Background

### 1.1 Resistance-Moisture Relationship

Below the fiber saturation point (FSP, approximately 25-30% MC), wood electrical resistance decreases exponentially with increasing moisture content:

```
R_kOhms = 10^((log10(M) - A) / B)
```

Or, solving for moisture content (M):

```
M = 10^(A + B * log10(R_kOhms))
```

Where:
- **M** = Moisture content (% MC)
- **R_kOhms** = Wood resistance in kilo-ohms
- **A, B** = Species-specific coefficients

### 1.2 Species-Specific Coefficients

Coefficients A and B vary by wood species due to differences in:
- Extractives content
- Density
- Cellular structure
- Grain orientation

**Example Coefficients (FPL GTR-6 Table 1):**

| Species | A | B | Valid Range (% MC) |
|---------|---|---|-------------------|
| Douglas-Fir (Coast) | 1.725 | -0.02820 | 6-25 |
| Southern Yellow Pine | 1.806 | -0.02994 | 6-25 |
| Sitka Spruce | 1.621 | -0.02641 | 6-25 |
| Red Oak | 1.912 | -0.03218 | 6-25 |

> **Caution:** the coefficients above are illustrative placeholders (see `docs/ai/issues.md` #11); they do not reproduce real resistance-MC behavior. Thesis coefficients come from the M3 regression (§6.2), backed by James (1988) and additional literature.

All species coefficients are stored in `include/wood_species_data.h`. The active species is selected at compile time via `SELECTED_WOOD_SPECIES_INDEX` in `include/config.h`, or at runtime via a LoRaWAN downlink command (`DOWNLINK_CMD_SET_SPECIES`, 0x02).

---

## 2. Equipment Requirements

### 2.1 Calibration Standards

| Equipment | Specification | Purpose |
|-----------|---------------|---------|
| Precision Resistors | 1 kOhm - 100 MOhm, 1% tolerance | Resistance verification |
| Reference Moisture Meter | Delmhorst, Protimeter, or equivalent | Validation |
| Oven | 103 +/- 2 C | Gravimetric MC determination |
| Analytical Balance | 0.01 g precision | Weight measurements |
| Temperature Chamber | 10-40 C range | Temperature calibration |
| DS18B20 reference thermometer | +/-0.5 C accuracy | Probe temperature validation |
| Data Logger | For reference temperature | Environmental monitoring |

### 2.2 Test Specimens

**Recommended:**
- Minimum 10 specimens per species
- Dimensions: 20 x 20 x 100 mm (radial x tangential x longitudinal)
- Clear wood, no defects
- Same batch/source for consistency

---

## 3. Resistance Calibration Procedure

### 3.1 ADC Calibration (Electrical)

**Purpose:** Verify the analog-to-digital converter accuracy.

The firmware uses `ADC_11db` attenuation for full 0-3.3V range. Each measurement is averaged over `ADC_SAMPLES_TO_AVERAGE` readings (default: 10) with `ADC_READ_STABILIZATION_MS` delay (default: 100 ms).

**Procedure:**

1. **Connect precision resistors** in place of the wood probe:
   ```
   GPIO25 --[R_pullup 100k]--+--[R_test]-- GND
                              |
                           ADC GPIO35
   ```

2. **Test resistor values** (expected counts follow the firmware circuit: `R_wood = R_PULLUP_OHMS * ADC / (ADC_MAX_READING - ADC)`, probe side to GND):
   - 100 Ohm (expected ADC: ~4, short-circuit region)
   - 100 kOhm (expected ADC: ~2048, mid-scale)
   - 1 MOhm (expected ADC: ~3723, beyond the linearity knee)
   - 10 MOhm (expected ADC: ~4054, near saturation)

   > **Caveat:** on the internal ESP32 ADC, counts above ~3043 (node above ~2.45 V, i.e. R_test above ~290 kOhm with the stock 100 kOhm pull-up) sit in the nonlinear region - expect the 1 MOhm and 10 MOhm points to read compressed or clipped. Those two points verify where saturation begins, not accuracy. For full-range verification, run the extended precision-resistor ladder in `docs/ai/2026-06-05-001-feat-measurement-front-end-plan.md` (Acceptance Test) before conditioning any wood samples.

3. **Record readings:**
   ```
   R_test (Ohm)  | ADC Expected | ADC Measured | Error (%)
   --------------|--------------|--------------|----------
   100           | 4            | _____        | _____
   100000        | 2048         | _____        | _____
   1000000       | 3723         | _____        | _____
   10000000      | 4054         | _____        | _____
   ```

4. **Adjust if needed:**
   - If systematic error > 2%, verify `R_PULLUP_OHMS` value in `config.h`
   - Check ADC attenuation setting: `ADC_ATTENUATION` in `config.h`
   - The resistance validation bounds (`MIN_VALID_RESISTANCE_OHMS` / `MAX_VALID_RESISTANCE_OHMS`) will flag out-of-range readings automatically

### 3.2 Probe Factor Determination

**Purpose:** Account for probe geometry and contact resistance.

**Procedure:**

1. **Prepare reference block:**
   - Use wood specimen at known moisture content (equilibrated)
   - Measure reference MC with calibrated pin meter
   - Record temperature (use DS18B20 if available)

2. **Insert probes:**
   - Drive electrodes to full depth
   - Maintain consistent spacing (if applicable)
   - Apply consistent pressure

3. **Measure resistance:**
   ```cpp
   R_measured = read_wood_resistance_ohms();  // From sensor.h
   ```

4. **Calculate probe correction factor:**
   ```
   R_expected = 10^((log10(M_ref) - A) / B) * 1000  // in ohms
   Probe_Factor = R_measured / R_expected
   ```

5. **Apply correction in firmware:**
   ```cpp
   float R_corrected = R_measured / PROBE_FACTOR;
   ```

---

## 4. Moisture Content Verification

### 4.1 Gravimetric Method (Primary Standard)

**Procedure:**

1. **Weigh wet specimen:**
   ```
   W_wet = _____ g
   ```

2. **Measure with probe system:**
   ```
   MC_indicated = _____ %
   Temperature = _____ C  (from DS18B20 or ESP32 internal)
   ```

3. **Oven-dry specimen:**
   - Dry at 103 +/- 2 C for 24 hours
   - Weigh immediately after cooling in desiccator
   ```
   W_dry = _____ g
   ```

4. **Calculate gravimetric MC:**
   ```
   MC_gravimetric = (W_wet - W_dry) / W_dry * 100%
   ```

5. **Compare readings:**
   ```
   Error = MC_indicated - MC_gravimetric = _____ %
   ```

**Acceptable Accuracy:**
- +/-1% MC for 6-20% range
- +/-2% MC for 20-30% range

### 4.2 Reference Meter Comparison (Secondary)

**Procedure:**

1. **Equilibrate specimens** at different humidity levels:
   - 33% RH (LiCl saturated solution) -> ~6% MC
   - 75% RH (NaCl saturated solution) -> ~14% MC
   - 97% RH (K2SO4 saturated solution) -> ~20% MC

2. **Wait for equilibrium** (2-4 weeks at constant temperature)

3. **Measure with both meters:**
   ```
   Humidity | MC_reference | MC_probe | Error
   ---------|--------------|----------|------
   33%      | _____        | _____    | _____
   75%      | _____        | _____    | _____
   97%      | _____        | _____    | _____
   ```

---

## 5. Temperature Compensation Calibration

### 5.1 Temperature Coefficient Verification

**Purpose:** Validate FPL GTR-6 Table 2 correction factors.

The firmware uses bilinear interpolation of a 13x20 correction lookup table (stored in `include/wood_temp_correction_data.h`) to apply temperature correction to indicated MC. Reference temperature is 70 F (21 C) where correction is zero.

**Procedure:**

1. **Use specimen at constant MC** (oven-dry or equilibrated)

2. **Place in temperature chamber** and step through temperatures:
   ```
   Temperature Setpoints: 10, 15, 20, 25, 30, 35, 40 C
   ```

3. **At each temperature:**
   - Wait 30 minutes for equilibrium
   - Record probe resistance
   - Record DS18B20 reading (or the chamber temperature if no DS18B20 is fitted)
   - Calculate indicated MC (without correction)

4. **Compare with FPL correction:**
   ```
   T (C)  | MC_indicated | MC_expected | C_t (FPL) | C_t (measured)
   -------|--------------|-------------|-----------|---------------
   10     | _____        | _____       | _____     | _____
   20     | _____        | _____       | _____     | _____
   25     | _____        | _____       | 0.0       | 0.0 (reference)
   30     | _____        | _____       | _____     | _____
   40     | _____        | _____       | _____     | _____
   ```

### 5.2 DS18B20 vs ESP32 Internal Temperature Comparison

**Background:** The firmware (v1.1.0+) supports a DS18B20 1-Wire temperature sensor (on GPIO 14) for direct wood/ambient temperature measurement. When no valid DS18B20 reading is available, the MC correction uses `DEFAULT_WOOD_TEMP_CELSIUS` (21 C) and the uplink is flagged on LPP channel 7; the ESP32 internal chip temperature is only a debug signal (serial output and optional LPP channel 6), never a correction input.

**Validation Procedure:**

1. **With DS18B20 connected,** the firmware automatically uses it for temperature correction.

2. **Log both temperatures simultaneously:**
   ```cpp
   T_ds18b20 = readDS18B20();              // From sensor.h
   T_esp32   = read_esp_temperature_celsius(); // From sensor.h
   ```

3. **Determine thermal offset:**
   - Place DS18B20 in contact with wood specimen
   - Compare DS18B20 reading with ESP32 internal reading
   - The ESP32 internal sensor typically reads 5-15 C higher than ambient due to chip self-heating

4. **If no DS18B20 is fitted:** the firmware does not estimate wood temperature from the die sensor; it corrects MC with `DEFAULT_WOOD_TEMP_CELSIUS` and flags the uplink (LPP channel 7). For bench calibration at a known temperature, set `DEFAULT_WOOD_TEMP_CELSIUS` in `config.h` to the chamber temperature.
   Note: For best accuracy, installing a DS18B20 is strongly recommended.

---

## 6. Calibration Data Sheet

### 6.1 Resistance Verification

```
Date: _______________
Technician: _______________
R_pullup (measured): _______________ Ohm
Firmware Version: _______________

Test Points:
+----------------+-------------+-------------+-------------+
| R_test (Ohm)   | ADC Reading | R_calc (Ohm) | Error (%)   |
+----------------+-------------+-------------+-------------+
| 100            |             |             |             |
| 1,000          |             |             |             |
| 10,000         |             |             |             |
| 100,000        |             |             |             |
| 1,000,000      |             |             |             |
| 10,000,000     |             |             |             |
+----------------+-------------+-------------+-------------+
```

### 6.2 Species Coefficient Verification

```
Species: _______________
Sample Size: n = _____

+------------+---------------+---------------+---------------+
| MC_grav    | R_measured    | MC_calculated | Residual      |
| (%)        | (kOhm)       | (%)           | (%)           |
+------------+---------------+---------------+---------------+
|            |               |               |               |
|            |               |               |               |
|            |               |               |               |
+------------+---------------+---------------+---------------+

Regression Results:
A = _______________ (FPL: _______)
B = _______________ (FPL: _______)
R^2 = _______________
```

---

## 7. Field Calibration Protocol

### 7.1 Pre-Deployment Checklist

- [ ] ADC calibration verified with precision resistors
- [ ] Species coefficients entered in `wood_species_data.h`
- [ ] Temperature compensation enabled (`ENABLE_TEMPERATURE_COMPENSATION true`)
- [ ] DS18B20 sensor verified (if installed on GPIO 14)
- [ ] Battery voltage reading verified
- [ ] LoRaWAN transmission confirmed (OTAA join successful)
- [ ] Session persistence verified (device restores session after deep sleep)

### 7.2 In-Situ Validation

**Procedure:**

1. **Install reference probe** adjacent to monitoring probe

2. **Take periodic readings** (weekly recommended):
   ```
   Date | MC_system | MC_reference | T_ambient | T_ds18b20 | Notes
   -----|-----------|--------------|-----------|-----------|------
   ```

3. **Check for drift:**
   - Compare system readings with reference
   - Document any systematic bias
   - Recalibrate if error exceeds +/-2% MC
   - Use downlink command to change species remotely if needed

### 7.3 Post-Deployment Verification

1. **Retrieve data** from LoRaWAN network server (TTN, ChirpStack, etc.)

2. **Analyze trends:**
   - Look for discontinuities (may indicate probe issues or session re-joins)
   - Check battery voltage trends
   - Verify temperature compensation behavior
   - Compare DS18B20 temp with ESP32 internal temp over time

3. **Physical inspection:**
   - Check probe corrosion
   - Verify wood condition at installation site
   - Clean electrode contacts if reusing
   - Inspect DS18B20 wiring and waterproofing

---

## 8. Uncertainty Analysis

### 8.1 Sources of Uncertainty

| Source | Estimated Uncertainty |
|--------|----------------------|
| ADC quantization (12-bit) | +/-0.02% MC |
| Resistor tolerance (1%) | +/-0.5% MC |
| Species coefficient variation | +/-1-2% MC |
| Temperature measurement (DS18B20) | +/-0.3% MC |
| Temperature measurement (ESP32 internal) | +/-1.0% MC |
| Temperature correction table interpolation | +/-0.5% MC |
| **Combined (RSS) with DS18B20** | **+/-1.3-2.3% MC** |
| **Combined (RSS) with ESP32 internal only** | **+/-1.5-2.5% MC** |

### 8.2 Reporting Uncertainty

For thesis documentation, report:

```
MC = X.X +/- Y.Y % (k=2, 95% confidence)
```

Where the expanded uncertainty is calculated as:
```
U = k * sqrt(u_ADC^2 + u_R^2 + u_species^2 + u_temp^2)
```

---

## 9. Troubleshooting

### 9.1 Common Issues

| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| MC reads 0% or negative | Open circuit | Check probe connections |
| MC reads >30% constantly | Short circuit | Inspect for electrode contact |
| High variability | Poor contact | Ensure full electrode insertion |
| Systematic bias | Wrong species | Verify species selection (or change via downlink cmd 0x02) |
| Temperature-dependent error | Compensation issue | Validate DS18B20 or ESP32 temp sensor |
| DS18B20 reads -127 C | Sensor not detected | Check wiring, 4.7k pull-up on data line |
| "Invalid resistance" in serial | ADC out of bounds | Check MIN/MAX_VALID_RESISTANCE_OHMS |

### 9.2 Data Quality Flags

Implement in data analysis:

```python
def quality_flag(mc, resistance, battery):
    if battery < 3.2: return "BAD"       # Low battery
    if resistance < 100: return "SUSPECT" # Possible short
    if resistance > 200e6: return "BAD"   # Open circuit (MAX_VALID_RESISTANCE_OHMS)
    if mc < 6 or mc > 30: return "SUSPECT"# Out of calibration range
    return "GOOD"
```

---

## 10. References

1. James, W.L. (1988). *Electric Moisture Meters for Wood* (Gen. Tech. Rep. FPL-GTR-6). Madison, WI: USDA Forest Service, Forest Products Laboratory. (Source of the resistance-MC relation and temperature corrections; previously misattributed here to the Wood Handbook.)

2. USDA Forest Products Laboratory. (2021). *Wood Handbook: Wood as an Engineering Material* (FPL-GTR-190). Madison, WI.

3. Simpson, W.T. (1993). *Determine Moisture Content and Affect on Strength*. USDA Forest Service.

4. Norimoto, M. (1976). *Dielectric properties of wood*. Wood Research, 59, 1-108.

---

## Appendix A: Quick Reference Formulas

### Resistance Calculation
```
R_wood = R_pullup * (ADC / (4095 - ADC))
```

### Moisture Content (FPL GTR-6)
```
MC = 10^(A + B * log10(R_kOhms))
```

### Temperature Correction
```
MC_corrected = MC_indicated + C_t
```

### Gravimetric MC
```
MC = (W_wet - W_dry) / W_dry * 100%
```

---

**Document Version:** 1.2
**Last Updated:** 2026-06-05
**Author:** Master Thesis Project, Wood Technologies
