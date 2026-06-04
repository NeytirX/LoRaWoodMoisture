// include/sensor.h — Sensor Abstraction Layer (Wood Moisture + Temperature)
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
        Serial.println(F("[Sensor] DS18B20 NOT found. Using ESP32 die temp as fallback."));
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
 * (~70F, where the FPL GTR-06 correction is zero) and sets is_fallback so
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
 * Circuit: VCC -> R_pullup -> ADC_PIN -> R_wood -> GND
 * R_wood = R_pullup * (ADC / (ADC_MAX - ADC))
 *
 * NOTE: The probe power pin must already be HIGH before calling this function.
 */
inline float read_wood_resistance_ohms() {
    // Wait for voltage to stabilize after power-up
    delay(ADC_READ_STABILIZATION_MS);

    // Multi-sample averaging to reduce noise
    uint32_t adc_sum = 0;
    for (int i = 0; i < ADC_SAMPLES_TO_AVERAGE; i++) {
        adc_sum += analogRead(MOISTURE_PROBE_ADC_PIN);
        delay(5);
    }
    float adc_raw_avg = (float)adc_sum / ADC_SAMPLES_TO_AVERAGE;

    Serial.print(F("[Sensor] Avg Raw ADC: "));
    Serial.println(adc_raw_avg);

    // Edge cases
    if (adc_raw_avg >= (ADC_MAX_READING - 1.0f)) {
        Serial.println(F("[Sensor] ADC saturated high — open circuit / very dry wood"));
        return 1.0e12f; // Effectively infinite resistance
    }
    if (adc_raw_avg < 1.0f) {
        Serial.println(F("[Sensor] ADC near zero — short circuit / extremely wet"));
        return 1.0e-3f; // Near-zero resistance
    }

    // Voltage divider: R_wood = R_pullup * (V_adc / (VCC - V_adc))
    // Since V_adc is proportional to ADC: R_wood = R_pullup * (ADC / (ADC_MAX - ADC))
    float R_wood = R_PULLUP_OHMS * (adc_raw_avg / (ADC_MAX_READING - adc_raw_avg));
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
