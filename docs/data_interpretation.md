# Data Interpretation Guide

## Overview

This guide explains how to interpret moisture content data from the LoRaWAN Wood Moisture Monitoring System. It covers understanding raw vs. corrected values, identifying artifacts, statistical analysis methods, and drawing meaningful conclusions for thesis research.

**Context:** Master Thesis in Wood Technologies
**Data Source:** LoRaWAN-connected resistive moisture monitoring system

---

## 1. Understanding the Data

### 1.1 Data Fields

Each transmission contains the following fields (Cayenne LPP format):

| Channel | Field | Type | Range | Description |
|---------|-------|------|-------|-------------|
| 1 | `wood_mc` | Float | 0-100% | **Primary output**: Temperature-corrected moisture content |
| 2 | `wood_temp` | Float | -40 to 80 C | Wood temperature (DS18B20 if available, else ESP32 internal) |
| 3 | `indicated_mc` | Float | 0-100% | Raw moisture content before temperature correction |
| 4 | `resistance` | Float | 0.1-10000 kOhm | Raw resistance measurement |
| 5 | `battery` | Float | 2.8-4.2V | Battery voltage |
| 6 | `esp_temp` | Float | -40 to 80 C | ESP32 internal chip temperature |

**Note:** Channels 3, 4, and 6 are optional and may be disabled to conserve payload space.

### 1.2 Temperature Source

Firmware v1.1.0+ handles two cases:

- **DS18B20 (preferred):** Direct wood/ambient temperature measurement via 1-Wire on GPIO 14. Accuracy +/-0.5 C. Used for temperature correction and sent on LPP channel 2.
- **Fallback (no valid DS18B20 reading):** The MC correction uses `DEFAULT_WOOD_TEMP_CELSIUS` (21 C, where the FPL correction is ~0). Channel 2 is omitted and the fallback flag (LPP channel 7, digital) is set to 1. The ESP32 die temperature is never used for correction - it reads well above ambient due to chip self-heating.

When interpreting readings, filter on channel 7: rows with `temp_fallback = 1` were corrected with the default temperature, not a measured one. If the true wood temperature was far from 21 C, re-correct the indicated MC offline or exclude those rows.

### 1.3 Key Relationships

**Moisture Content Calculation Chain:**

```
ADC Reading -> Resistance -> Indicated MC -> Temperature Correction -> Corrected MC
```

**Mathematical Relationships:**

1. **Resistance from ADC:**
   ```
   R_wood = R_pullup * (ADC / (4095 - ADC))
   ```

2. **Indicated MC (FPL GTR-6):**
   ```
   MC_indicated = 10^(A + B * log10(R_kOhm))
   ```

3. **Temperature Correction:**
   ```
   MC_corrected = MC_indicated + C_t(T, MC_indicated)
   ```
   Where C_t is obtained by bilinear interpolation of the FPL GTR-6 correction table.

### 1.4 Expected Value Ranges

| Wood Condition | MC Range | Resistance Range |
|----------------|----------|------------------|
| Kiln-dried | 6-12% | 5-50 MOhm |
| Air-dried (indoor) | 8-15% | 1-20 MOhm |
| Air-dried (outdoor, covered) | 12-20% | 200 kOhm - 5 MOhm |
| Green/fresh | 25-30%+ | < 100 kOhm |

---

## 2. Temperature Correction

### 2.1 Why Correction is Necessary

Wood electrical resistance varies with temperature:
- **Resistance decreases** as temperature increases
- **Effect is MC-dependent**: Greater at higher moisture content
- **Magnitude**: Up to +/-7% MC correction at temperature extremes

**Without correction:**
- Summer readings appear artificially low
- Winter readings appear artificially high
- Seasonal trends are distorted

### 2.2 Correction Factor Table (FPL-GTR-6, Figure 5)

Correction values (C_t) to **add** to indicated MC. Digitized 2026-06-05 from
James (1988) Figure 5 (resistance-type meters, calibration 70 F / 21.1 C); the
full Celsius grid lives in `include/wood_temp_correction_data.h`. Excerpt:

```
Temperature ->  -20C   -10C    0C    10C    20C    30C    40C    50C
MC indicated
----------------------------------------------------------------------
6%              4.7    3.2    1.9    0.9    0.1   -0.7   -1.4   -2.1
10%             6.8    4.4    2.7    1.3    0.1   -0.9   -1.8   -2.7
15%             8.6    6.2    3.9    1.8    0.2   -1.2   -2.5   -3.6
20%            12.7    7.8    4.8    2.2    0.2   -1.6   -3.1   -4.4
25%            17.7   10.1    6.0    2.8    0.2   -1.9   -3.8   -5.4
```

**Key Points:**
- Reference temperature: 70 F (21.1 C) - no correction
- Below ~21 C: Positive correction (cold wood has higher resistance, the meter reads low - add)
- Above ~21 C: Negative correction (warm wood has lower resistance, the meter reads high - subtract)
- Cells implying true MC above 28% (very cold + very wet) extrapolate beyond the chart's curve family and carry extra uncertainty

### 2.3 Correction Example

**Raw Data:**
```
MC_indicated = 14.5%
Wood_temp = 10 C (50 F)
```

**Lookup C_t:**
- At 10 C and 14.5% MC: C_t ~ +1.7% (interpolated)

**Corrected MC:**
```
MC_corrected = 14.5% + 1.7% = 16.2%
```

**Interpretation:** Cold wood has higher resistance, so the meter under-reads; the wood is actually wetter than indicated. After correction, the true MC is 16.2%.

### 2.4 Visualizing Correction Impact

**Before vs. After Correction:**

```
MC (%)
  |
20|    +--+                    +--+
  |   |  |    Uncorrected     |  |
15|  -+  +--------+    +------+  +---  Corrected
  |             |    |
10|             +----+
  |
  +---------------------------------------> Time
     Winter    Spring   Summer   Fall
```

**Note:** Uncorrected data shows artificial seasonal variation.

---

## 3. Data Quality Assessment

### 3.1 Quality Flags

Implement automated quality checks:

```python
def assess_data_quality(record):
    """Return quality assessment and flags"""
    flags = []
    quality = "GOOD"

    # Battery check
    if record['battery'] < 3.2:
        flags.append("LOW_BATTERY")
        quality = "SUSPECT"

    # Range checks
    if record['wood_mc'] < 6 or record['wood_mc'] > 30:
        flags.append("OUT_OF_CALIBRATION_RANGE")
        quality = "SUSPECT"

    if record['wood_mc'] < 0 or record['wood_mc'] > 100:
        flags.append("INVALID_MC")
        quality = "BAD"

    # Resistance sanity check
    if record.get('resistance', 0) < 0.1 or record.get('resistance', 0) > 100000:
        flags.append("INVALID_RESISTANCE")
        quality = "BAD"

    # Temperature check
    if record['wood_temp'] < -10 or record['wood_temp'] > 50:
        flags.append("TEMP_ANOMALY")
        quality = "SUSPECT"

    # Signal quality (if available)
    if record.get('rssi', -100) < -120:
        flags.append("POOR_SIGNAL")

    return quality, flags
```

### 3.2 Identifying Artifacts

**Common Data Artifacts:**

| Pattern | Likely Cause | Action |
|---------|--------------|--------|
| Sudden step change | Probe disconnection/reconnection | Flag data, check installation |
| Single outlier spike | EMI/RFI interference | Remove outlier, keep surrounding data |
| Gradual drift | Probe corrosion | Recalibrate, replace probe |
| Flat line | Device malfunction | Check device status |
| Missing data | Network coverage issue or deep sleep extension | Gap analysis, interpolate if brief |
| Missing temp + fallback flag = 1 | DS18B20 failure, MC corrected with default temp | Filter on channel 7, fix sensor |
| Battery voltage plateau then drop | Battery-aware sleep activated | Normal behavior at end of life |

**Example Artifact Detection:**

```python
def detect_artifacts(data, threshold=5.0):
    """Detect sudden jumps in MC data"""
    artifacts = []
    for i in range(1, len(data)):
        delta = abs(data[i]['mc'] - data[i-1]['mc'])
        if delta > threshold:
            artifacts.append({
                'index': i,
                'timestamp': data[i]['timestamp'],
                'delta': delta,
                'previous': data[i-1]['mc'],
                'current': data[i]['mc']
            })
    return artifacts
```

### 3.3 Missing Data Handling

**Gap Classification:**

| Gap Duration | Classification | Recommended Action |
|--------------|----------------|-------------------|
| < 2 hours | Minor | Linear interpolation acceptable |
| 2-24 hours | Moderate | Mark as interpolated |
| 24 hours - 1 week | Significant | Analyze cause, use with caution |
| > 1 week | Major | Treat as separate deployment |

**Note:** Gaps may occur when battery drops below thresholds and the firmware extends the sleep interval (2x at 3.4V, 4x at 3.2V). Check battery voltage around gap periods.

**Interpolation Methods:**

```python
# Linear interpolation (simple gaps)
df['mc_interpolated'] = df['mc'].interpolate(method='linear')

# Time-weighted (irregular intervals)
df['mc_interpolated'] = df['mc'].interpolate(method='time')

# For temperature data (diurnal pattern)
from scipy.interpolate import UnivariateSpline
spline = UnivariateSpline(x=timestamps, y=mc_values, s=0.5)
mc_smooth = spline(new_timestamps)
```

---

## 4. Statistical Analysis

### 4.1 Descriptive Statistics

**Key Metrics to Report:**

| Statistic | Formula | Use |
|-----------|---------|-----|
| Mean | sum(x)/n | Central tendency |
| Standard Deviation | sqrt(sum((x-mean)^2)/(n-1)) | Variability |
| Min/Max | - | Range |
| Median | Middle value | Robust central tendency |
| IQR | Q3 - Q1 | Robust spread |
| Coefficient of Variation | SD/Mean * 100% | Relative variability |

**Python Example:**

```python
import pandas as pd

# Load data
df = pd.read_csv('moisture_data.csv', parse_dates=['timestamp'])

# Overall statistics
summary = df['wood_mc'].describe()
print(summary)

# Group by day
daily = df.resample('D', on='timestamp').agg({
    'wood_mc': ['mean', 'std', 'min', 'max'],
    'wood_temp': ['mean', 'min', 'max'],
    'battery': 'mean'
})
```

### 4.2 Equilibrium Moisture Content (EMC) Analysis

**EMC Reference Values (21 C / 70 F):**

| RH (%) | EMC (%) |
|--------|---------|
| 20 | 4.4 |
| 40 | 7.8 |
| 60 | 11.0 |
| 75 | 14.0 |
| 80 | 15.5 |
| 90 | 19.0 |
| 95 | 22.0 |

**Comparison with EMC:**

```python
# Calculate deviation from expected EMC
emc_expected = 12.0  # Based on local climate
df['emc_deviation'] = df['wood_mc'] - emc_expected

# Time to reach EMC after perturbation
def time_to_emc(data, emc_target, tolerance=1.0):
    """Calculate time to reach within tolerance of EMC"""
    within_tolerance = abs(data['wood_mc'] - emc_target) < tolerance
    first_index = within_tolerance.idxmax()
    return data.loc[first_index, 'timestamp'] - data.index[0]
```

### 4.3 Trend Analysis

**Detecting Moisture Trends:**

```python
from scipy import stats

# Linear regression for trend
slope, intercept, r_value, p_value, std_err = stats.linregress(
    df.index.astype('int64') // 10**9,  # Convert to seconds
    df['wood_mc']
)

# Trend interpretation
if p_value < 0.05:
    if slope > 0:
        trend = f"Significant increasing trend ({slope*86400:.3f}%/day)"
    else:
        trend = f"Significant decreasing trend ({slope*86400:.3f}%/day)"
else:
    trend = "No significant trend"
```

**Seasonal Decomposition:**

```python
from statsmodels.tsa.seasonal import seasonal_decompose

# Requires regular time intervals
df_regular = df.set_index('timestamp').resample('H').mean()
result = seasonal_decompose(df_regular['wood_mc'], model='additive', period=24*7)

# Components:
# - result.trend: Long-term trend
# - result.seasonal: Weekly pattern
# - result.resid: Residual (unexplained)
```

### 4.4 Correlation Analysis

**Temperature Correlation:**

```python
# Correlation between MC and temperature
corr_mc_temp = df['wood_mc'].corr(df['wood_temp'])

# Lagged correlation (MC response to temp change)
df['temp_lag1'] = df['wood_temp'].shift(1)
df['temp_lag7'] = df['wood_temp'].shift(7)

lag_corr = {
    'Same day': df['wood_mc'].corr(df['wood_temp']),
    '1 day lag': df['wood_mc'].corr(df['temp_lag1']),
    '7 day lag': df['wood_mc'].corr(df['temp_lag7'])
}
```

**Multiple Probe Comparison:**

```python
# Correlation matrix for multiple probes
probes = ['probe_1_mc', 'probe_2_mc', 'probe_3_mc']
corr_matrix = df[probes].corr()

# Print correlation matrix
print(corr_matrix)
```

---

## 5. Validation Against Reference

### 5.1 Reference Methods

**Gravimetric (Primary Standard):**

```
MC_grav = (W_wet - W_dry) / W_dry * 100%
Uncertainty: +/-0.5% MC
```

**Pin-Type Moisture Meter (Secondary):**

```
Example: Delmhorst F-2000
Uncertainty: +/-1% MC (6-20% range)
```

### 5.2 Accuracy Metrics

| Metric | Formula | Target |
|--------|---------|--------|
| Bias | Mean(MC_system - MC_ref) | +/-1% MC |
| MAE | Mean(|MC_system - MC_ref|) | < 1.5% MC |
| RMSE | sqrt(Mean((MC_system - MC_ref)^2)) | < 2% MC |
| R^2 | Coefficient of determination | > 0.85 |

**Python Calculation:**

```python
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
import numpy as np

def calculate_accuracy_metrics(system_mc, reference_mc):
    """Calculate validation metrics"""
    bias = np.mean(system_mc - reference_mc)
    mae = mean_absolute_error(reference_mc, system_mc)
    rmse = np.sqrt(mean_squared_error(reference_mc, system_mc))
    r2 = r2_score(reference_mc, system_mc)

    return {
        'Bias': bias,
        'MAE': mae,
        'RMSE': rmse,
        'R^2': r2
    }

# Example usage
metrics = calculate_accuracy_metrics(df['system_mc'], df['reference_mc'])
print(metrics)
```

### 5.3 Bland-Altman Analysis

**For Method Comparison:**

```python
import matplotlib.pyplot as plt
import numpy as np

def bland_altman_plot(method1, method2):
    """Create Bland-Altman plot for method comparison"""
    mean_vals = (method1 + method2) / 2
    diff = method1 - method2

    mean_diff = np.mean(diff)
    sd_diff = np.std(diff)

    plt.figure(figsize=(10, 6))
    plt.scatter(mean_vals, diff, alpha=0.5)
    plt.axhline(mean_diff, color='red', linestyle='--', label='Mean bias')
    plt.axhline(mean_diff + 1.96*sd_diff, color='gray', linestyle=':', label='+/-1.96 SD')
    plt.axhline(mean_diff - 1.96*sd_diff, color='gray', linestyle=':')
    plt.xlabel('Mean of Methods (%)')
    plt.ylabel('Difference (%)')
    plt.title('Bland-Altman Plot: System vs Reference')
    plt.legend()
    plt.grid(True, alpha=0.3)
```

---

## 6. Interpretation for Wood Technology Applications

### 6.1 Drying Kinetics

**Analyzing Drying Rates:**

```python
# Calculate drying rate
df['drying_rate'] = df['wood_mc'].diff() / df['time_delta_hours']

# Phase identification
def identify_drying_phase(mc_data):
    """Identify drying phases from MC curve"""
    phases = []
    current_phase = "Initial"

    for i, mc in enumerate(mc_data):
        if mc > 25:
            phase = "Green"
        elif mc > 20:
            phase = "FSP Region"
        elif mc > 15:
            phase = "Transition"
        elif mc > 8:
            phase = "Air-Dry"
        else:
            phase = "Kiln-Dry"

        if phase != current_phase:
            phases.append((i, phase))
            current_phase = phase

    return phases
```

**Drying Curve Interpretation:**

```
MC (%)
  |
30| Green ----------+
  |                 |
25|                 | FSP (Fiber Saturation Point)
  |                 +----------+
20|                 |          |
  |                 |          | Transition
15|                 |          +----------+
  |                 |          |          |
10|                 |          |          | Air-Dry
  |                 |          |          +------
 5|                 |          |          |
  |                 |          |          |
 0+-----------------+----------+----------+------> Time
     Phase 1       Phase 2    Phase 3    Phase 4
     (Free water)  (Bound     (Approach   (EMC)
                   water)      equilibrium)
```

### 6.2 Moisture Gradients

**Multiple Depth Monitoring:**

If probes are installed at different depths:

```
Surface (0-20mm)    ---+
Mid (20-50mm)     -----+---> Gradient analysis
Core (>50mm)      -----+
```

**Gradient Calculation:**

```python
df['gradient_surface_mid'] = df['mc_surface'] - df['mc_mid']
df['gradient_mid_core'] = df['mc_mid'] - df['mc_core']

# Stress prediction (high gradient = checking risk)
df['checking_risk'] = df['gradient_surface_mid'].apply(
    lambda x: 'HIGH' if x > 5 else ('MEDIUM' if x > 3 else 'LOW')
)
```

### 6.3 EMC Approach Modeling

**Exponential Decay Model:**

```
MC(t) = EMC + (MC_0 - EMC) * e^(-kt)
```

Where:
- MC(t) = Moisture content at time t
- EMC = Equilibrium moisture content
- MC_0 = Initial moisture content
- k = Drying rate constant

**Fitting the Model:**

```python
from scipy.optimize import curve_fit
import numpy as np

def emc_approach(t, emc, mc0, k):
    return emc + (mc0 - emc) * np.exp(-k * t)

# Fit to data
t_hours = (df['timestamp'] - df['timestamp'].iloc[0]).dt.total_seconds() / 3600
popt, pcov = curve_fit(emc_approach, t_hours, df['wood_mc'],
                       p0=[12, df['wood_mc'].iloc[0], 0.01])

emc_fitted, mc0_fitted, k_fitted = popt
print(f"Fitted EMC: {emc_fitted:.2f}%")
print(f"Drying rate constant k: {k_fitted:.4f} h^-1")
```

---

## 7. Reporting Results

### 7.1 Thesis Table Templates

**Table: Summary Statistics by Location**

| Location | n | Mean MC (%) | SD (%) | Min (%) | Max (%) | CV (%) |
|----------|---|-------------|--------|---------|---------|--------|
| Site A | 720 | 12.5 | 1.8 | 8.2 | 18.3 | 14.4 |
| Site B | 720 | 14.2 | 2.1 | 9.5 | 21.0 | 14.8 |
| Site C | 720 | 11.8 | 1.5 | 7.9 | 16.2 | 12.7 |

**Table: Accuracy Assessment**

| Metric | Value | Target | Pass/Fail |
|--------|-------|--------|-----------|
| Bias | +0.3% MC | +/-1.0% MC | Pass |
| MAE | 0.8% MC | <1.5% MC | Pass |
| RMSE | 1.1% MC | <2.0% MC | Pass |
| R^2 | 0.92 | >0.85 | Pass |

### 7.2 Figure Recommendations

**Essential Figures:**

1. **Time Series Plot**
   - MC and temperature over deployment period
   - Show corrected vs. uncorrected comparison
   - Mark battery-aware sleep interval changes

2. **Histogram/Distribution**
   - MC distribution for each location
   - Normal distribution fit

3. **Bland-Altman Plot**
   - Method comparison with reference

4. **Box Plot**
   - MC by location or treatment

5. **Correlation Heatmap**
   - Relationships between variables

### 7.3 Statistical Test Recommendations

| Research Question | Recommended Test |
|-------------------|------------------|
| Compare MC between 2 locations | t-test or Mann-Whitney U |
| Compare MC between >2 locations | ANOVA or Kruskal-Wallis |
| Trend over time | Linear regression, Mann-Kendall |
| Agreement with reference | Bland-Altman, Lin's concordance |
| Seasonal effects | Seasonal decomposition, ANOVA with month factor |
| Relationship with environmental factors | Multiple regression, GLM |

---

## 8. Common Interpretation Pitfalls

### 8.1 Errors to Avoid

| Pitfall | Why It's Wrong | Correct Approach |
|---------|----------------|------------------|
| Using uncorrected MC | Temperature effects distort results | Always use temperature-corrected values |
| Ignoring out of range data | >30% or <6% MC is unreliable | Flag or exclude from analysis |
| Treating ESP32 temp as wood temp | Self-heating causes 5-15 C offset | Use DS18B20, or apply correction offset |
| Overinterpreting single readings | Natural variability exists | Use statistical aggregates |
| Assuming species coefficients are exact | Variation within species exists | Report uncertainty bounds |
| Ignoring battery-aware sleep changes | Data gaps at end of battery life | Check battery voltage around gaps |

### 8.2 Uncertainty Reporting

**Recommended Format:**

```
MC = 14.5 +/- 1.8% (95% CI, k=2)

Components:
- Measurement uncertainty: +/-1.0%
- Species coefficient uncertainty: +/-1.2%
- Temperature correction uncertainty: +/-0.5% (DS18B20) or +/-1.0% (ESP32 internal)
- Combined (RSS): +/-1.6% (with DS18B20) or +/-1.9% (ESP32 only)
- Expanded (k=2): +/-3.2% (with DS18B20) or +/-3.8% (ESP32 only)
```

---

## 9. Data Export and Analysis Workflow

### 9.1 TTN Data Export

**Manual Export:**
1. Go to TTN Console -> Application -> Data
2. Select date range
3. Click "Export" -> JSON or CSV

**Automated Export (Python):**

```python
import requests
import pandas as pd

def fetch_ttn_data(app_id, api_key, start_time, end_time):
    """Fetch data from TTN API"""
    url = f"https://eu1.cloud.thethings.network/api/v3/as/applications/{app_id}/packages/storage/uplink_message"

    headers = {
        "Authorization": f"Bearer {api_key}"
    }

    params = {
        "start_time": start_time,
        "end_time": end_time,
        "limit": 10000
    }

    response = requests.get(url, headers=headers, params=params)
    data = response.json()

    # Parse and return as DataFrame
    return parse_ttn_response(data)
```

### 9.2 Analysis Pipeline

```python
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# 1. Load data
df = pd.read_csv('ttn_export.csv', parse_dates=['timestamp'])

# 2. Quality filtering
df = df[df['quality_flag'] == 'GOOD']

# 3. Resample to hourly (if needed)
df_hourly = df.set_index('timestamp').resample('H').mean()

# 4. Calculate derived variables
df_hourly['mc_daily_change'] = df_hourly['wood_mc'].diff(24)
df_hourly['temp_daily_change'] = df_hourly['wood_temp'].diff(24)

# 5. Generate summary statistics
summary = df_hourly.describe()

# 6. Create visualizations
create_time_series_plot(df_hourly)
create_distribution_plot(df['wood_mc'])
create_correlation_matrix(df_hourly)

# 7. Export results
summary.to_csv('summary_statistics.csv')
df_hourly.to_csv('processed_data.csv')
```

---

## 10. Example Analysis Report

### 10.1 Template Structure

```
MOISTURE MONITORING ANALYSIS REPORT

Site: [Location Name]
Period: [Start Date] to [End Date]
Duration: [X] days
Firmware: [version from boot banner]
Temperature Source: DS18B20 / ESP32 Internal

1. EXECUTIVE SUMMARY
   - Key findings
   - Average MC: XX.X +/- X.X%
   - Trend: [Increasing/Decreasing/Stable]

2. DATA QUALITY
   - Total readings: XXXX
   - Valid readings: XXXX (XX%)
   - Data gaps: X (total XX hours)
   - Battery-aware sleep events: X

3. MOISTURE CONTENT ANALYSIS
   3.1 Descriptive Statistics
   3.2 Time Series Analysis
   3.3 Trend Analysis

4. TEMPERATURE ANALYSIS
   4.1 Diurnal Patterns
   4.2 Correction Factor Application
   4.3 DS18B20 vs ESP32 Internal Comparison (if both available)

5. VALIDATION RESULTS
   5.1 Comparison with Reference
   5.2 Accuracy Metrics

6. CONCLUSIONS
   - Moisture status assessment
   - Recommendations

Appendices:
A. Raw Data Tables
B. Calibration Records
C. Detailed Statistical Output
D. Firmware Configuration (species index, interval, etc.)
```

---

## Appendix A: Quick Reference

### A.1 Correction Factor Lookup

| Temp (C) | Temp (F) | Correction at 10% MC | Correction at 20% MC |
|-----------|-----------|---------------------|---------------------|
| -18 | 0 | -2.9% | -5.5% |
| -7 | 20 | -2.0% | -4.0% |
| 4 | 40 | -1.1% | -2.5% |
| 16 | 60 | -0.4% | -0.9% |
| 21 | 70 | 0.0% | 0.0% |
| 27 | 80 | +0.4% | +0.9% |
| 38 | 100 | +0.9% | +1.9% |
| 49 | 120 | +1.4% | +2.9% |

### A.2 Species Coefficients Quick Reference

| Species | A | B |
|---------|---|---|
| Douglas-Fir (Coast) | 1.725 | -0.02820 |
| Southern Yellow Pine | 1.806 | -0.02994 |
| Sitka Spruce | 1.621 | -0.02641 |
| Red Oak | 1.912 | -0.03218 |
| Sugar Maple | 1.850 | -0.03100 |

### A.3 Python Functions

```python
import numpy as np

def mc_from_resistance(r_kohms, A, B):
    """Calculate MC from resistance using FPL GTR-6 formula"""
    return 10 ** (A + B * np.log10(r_kohms))

def resistance_from_mc(mc, A, B):
    """Calculate resistance from MC"""
    return 10 ** ((mc - A) / B)

def celsius_to_fahrenheit(c):
    return c * 9/5 + 32

def fahrenheit_to_celsius(f):
    return (f - 32) * 5/9
```

---

**Document Version:** 1.1
**Last Updated:** 2026-03-17
**Author:** Master Thesis Project, Wood Technologies
