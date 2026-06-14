// include/sensor.h — Sensor Abstraction Layer (Wood Moisture + Temperature)
//
// SINGLE-TU ONLY: defines static driver objects at header scope. Include from
// src/main.cpp only — a second includer causes duplicate-symbol / split-state bugs.
//
// Manages:
//   - DS18B20 temperature sensor (1-Wire), with fallback to ESP32 die temp
//   - Resistive wood moisture probe (voltage divider + ADC)
//   - ADC attenuation configuration
//   - Resistance range validation

#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

// ESP32 internal temperature sensor (undocumented but available)
// Pins us to the current espressif32 platform - see the platform note in
// platformio.ini before upgrading (replacement: IDF temperature sensor driver).
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read(); // Returns Fahrenheit (integer)
#ifdef __cplusplus
}
#endif

// --- Module-level state ---
static OneWire oneWire(ONEWIRE_PIN);
static DallasTemperature ds18b20(&oneWire);
static bool ds18b20_available = false;

// =============================================================================
// Initialization
// =============================================================================

/**
 * Initialize the ADC and DS18B20 sensor.
 * Call once during setup().
 */
inline void sensor_init() {
    // --- ADC Attenuation ---
    analogSetAttenuation(ADC_ATTENUATION);
    analogSetPinAttenuation(MOISTURE_PROBE_ADC_PIN, ADC_ATTENUATION);
    Serial.print(F("[Sensor] ADC attenuation set. Pin: "));
    Serial.println(MOISTURE_PROBE_ADC_PIN);

    // --- DS18B20 Detection ---
    ds18b20.begin();
    int deviceCount = ds18b20.getDeviceCount();
    if (deviceCount > 0) {
        ds18b20_available = true;
        ds18b20.setResolution(DS18B20_RESOLUTION_BITS);
        ds18b20.setWaitForConversion(true); // Blocking read for simplicity
        Serial.print(F("[Sensor] DS18B20 found! Devices: "));
        Serial.print(deviceCount);
        Serial.print(F(", Resolution: "));
        Serial.print(DS18B20_RESOLUTION_BITS);
        Serial.println(F(" bits"));
    } else {
        ds18b20_available = false;
        Serial.println(F("[Sensor] DS18B20 NOT found. MC correction will use default temp (flagged in payload)."));
    }
}

/**
 * Returns true if a DS18B20 was detected during init.
 */
inline bool sensor_has_ds18b20() {
    return ds18b20_available;
}

// =============================================================================
// Temperature Reading
// =============================================================================

/**
 * Read the ESP32 internal die temperature (inaccurate, ~40-60 C typical).
 * Only useful as a last-resort fallback.
 */
inline float read_esp_temperature_celsius() {
    float temp_f = (float)temprature_sens_read();
    return (temp_f - 32.0f) * 5.0f / 9.0f;
}

/**
 * Read wood temperature from DS18B20 if available.
 *
 * Without a valid DS18B20 reading, falls back to DEFAULT_WOOD_TEMP_CELSIUS
 * (~21 C, the FPL-GTR-6 calibration reference where the correction is ~zero)
 * and sets is_fallback so
 * the payload can flag the reading. The ESP32 die temperature is NOT used
 * for the correction: it reads tens of degrees above ambient and would
 * silently skew the corrected MC.
 *
 * For accurate temperature correction, mount the DS18B20 in the same probe
 * assembly as the moisture electrodes so it measures wood temperature directly.
 */
inline float read_wood_temperature(bool &is_fallback) {
    if (ds18b20_available) {
        ds18b20.requestTemperatures();
        float temp_c = ds18b20.getTempCByIndex(0);

        // Sanity check — DS18B20 returns DEVICE_DISCONNECTED_C (-127) on error
        if (temp_c != DEVICE_DISCONNECTED_C && temp_c >= -55.0f && temp_c <= 125.0f) {
            is_fallback = false;
            Serial.print(F("[Sensor] DS18B20 temp: "));
            Serial.print(temp_c);
            Serial.println(F(" C"));
            return temp_c;
        }
        Serial.println(F("[Sensor] DS18B20 read error! Using default wood temp."));
    }

    // Fallback: assume the default wood temperature instead of the die temp
    is_fallback = true;
    Serial.print(F("[Sensor] No valid wood temp. Using default: "));
    Serial.print(DEFAULT_WOOD_TEMP_CELSIUS);
    Serial.println(F(" C (fallback flagged in payload)"));
    return DEFAULT_WOOD_TEMP_CELSIUS;
}

// =============================================================================
// Resistance & Moisture Measurement
// =============================================================================

/**
 * Read the wood resistance in Ohms using the voltage divider circuit.
 *
 * Circuit: V_top (probe power pin, ~VCC_PROBE_VOLTAGE) -> R_pullup -> ADC_PIN
 *          -> R_wood -> GND
 *   V_node = V_top * R_wood / (R_wood + R_pullup)
 *   R_wood = R_pullup * V_node / (V_top - V_node)
 *
 * Two deliberate choices here (see docs/data_interpretation.md):
 *   - The divider math runs on analogReadMilliVolts(), which applies the ESP32
 *     eFuse ADC calibration (linearity + offset). The old raw-count form assumed
 *     ADC full-scale == V_top, which is not true on the ESP32 at 11 dB.
 *   - The open-circuit (very-dry) gate stays on the RAW count, because the raw
 *     ADC saturates cleanly at full scale whereas the calibrated mV compresses
 *     near the top rail and cannot reliably reach V_top.
 *
 * V_top defaults to VCC_PROBE_VOLTAGE (3.3 V nominal). For best accuracy, measure
 * the probe power pin's HIGH voltage under load once and set VCC_PROBE_VOLTAGE.
 *
 * Sets adc_nonlinear = true when the divider node sits past the ESP32 ADC's
 * linearity knee (or saturates), i.e. the returned R is in the "silent
 * compression zone" and the MC is low-confidence. This is flagged on the ADC
 * voltage, NOT the computed resistance: in that zone the R value is itself
 * corrupted, so a value-based bound would miss it (see the front-end plan in
 * docs/ai/2026-06-05-001-feat-measurement-front-end-plan.md).
 *
 * NOTE: The probe power pin must already be HIGH before calling this function.
 */
inline float read_wood_resistance_ohms(bool &adc_nonlinear) {
    adc_nonlinear = false;

    // Wait for the divider node to settle after the probe power pin goes HIGH.
    delay(ADC_READ_STABILIZATION_MS);

    // Open-circuit / very-dry gate on the raw count (calibration-independent).
    int raw_gate = analogRead(MOISTURE_PROBE_ADC_PIN);
    if (raw_gate >= (int)(ADC_MAX_READING - 2.0f)) {
        adc_nonlinear = true;
        Serial.println(F("[Sensor] ADC saturated high — open circuit / very dry wood (out of range)"));
        return 1.0e12f; // Effectively infinite resistance
    }

    // Calibrated-millivolt sampling.
    uint32_t samples[ADC_SAMPLES_TO_AVERAGE];
    for (int i = 0; i < ADC_SAMPLES_TO_AVERAGE; i++) {
        samples[i] = analogReadMilliVolts(MOISTURE_PROBE_ADC_PIN);
        delay(5);
    }

    // Trimmed mean: drop the lowest and highest sample, average the rest. Keeps
    // the noise reduction of oversampling while rejecting a single EMI/RFI spike
    // (a documented artifact for this resistive probe).
    uint32_t v_min = samples[0], v_max = samples[0], v_sum = 0;
    for (int i = 0; i < ADC_SAMPLES_TO_AVERAGE; i++) {
        v_sum += samples[i];
        if (samples[i] < v_min) v_min = samples[i];
        if (samples[i] > v_max) v_max = samples[i];
    }
    float v_node_mv = (ADC_SAMPLES_TO_AVERAGE > 2)
        ? (float)(v_sum - v_min - v_max) / (ADC_SAMPLES_TO_AVERAGE - 2)
        : (float)v_sum / ADC_SAMPLES_TO_AVERAGE;

    Serial.print(F("[Sensor] Node voltage (trimmed mean): "));
    Serial.print(v_node_mv);
    Serial.println(F(" mV"));

    const float v_top_mv = VCC_PROBE_VOLTAGE * 1000.0f;

    // Short circuit / extremely wet: node pulled near 0 V.
    if (v_node_mv < ADC_SHORT_CIRCUIT_MV) {
        Serial.println(F("[Sensor] Node near 0 V — short circuit / extremely wet"));
        return 1.0e-3f; // Near-zero resistance
    }
    // Safety guard for the divider denominator (calibrated mV should stay below
    // V_top, but a noisy over-range sample must not produce a negative R).
    if (v_node_mv >= (v_top_mv - 1.0f)) {
        adc_nonlinear = true;
        Serial.println(F("[Sensor] Node at V_top — treating as open circuit"));
        return 1.0e12f;
    }
    // Past the ADC linearity knee the conversion is compressed and the returned
    // R is systematically wrong (silent zone). Report it but flag low-confidence.
    if (v_node_mv > ADC_LINEARITY_LIMIT_MV) {
        adc_nonlinear = true;
        Serial.println(F("[Sensor] Node past ADC linearity knee — reading low-confidence (flagged)"));
    }

    float R_wood = R_PULLUP_OHMS * (v_node_mv / (v_top_mv - v_node_mv));
    return R_wood;
}

/**
 * Check if the measured resistance is within the valid calibration range
 * for typical resistive moisture meters.
 * Returns true if valid, false if suspect.
 */
inline bool is_resistance_in_valid_range(float R_ohms) {
    if (R_ohms < MIN_VALID_RESISTANCE_OHMS) {
        Serial.print(F("[Sensor] WARNING: Resistance "));
        Serial.print(R_ohms);
        Serial.println(F(" Ohm is below valid range — possible short or sensor issue"));
        return false;
    }
    if (R_ohms > MAX_VALID_RESISTANCE_OHMS) {
        Serial.print(F("[Sensor] WARNING: Resistance "));
        Serial.print(R_ohms / 1e6f);
        Serial.println(F(" MOhm is above valid range — very dry or open circuit"));
        return false;
    }
    return true;
}

#endif // SENSOR_H
