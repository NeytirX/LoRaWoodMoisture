// src/main.cpp - Main Firmware (LoRa P2P Wood Moisture Version)

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "config.h"
#include "wood_species_data.h"         // For species A, B coefficients
#include "wood_temp_correction_data.h" // For temperature correction table

#ifdef USE_AXP_POWER_MANAGEMENT
#include <XPowersLib.h>
XPowersLibInterface *PMU = NULL;
bool pmic_initialized = false;
#endif

// ESP32 Internal Temperature Sensor (available in newer ESP32 Arduino cores)
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read(); // Function to read ESP32 internal temperature
#ifdef __cplusplus
}
#endif

// --- RadioLib Module Instance ---
SX1262 radio = new Module(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, SPI);

// --- RTC DATA FOR SLEEP ---
RTC_DATA_ATTR uint32_t sleep_interval_seconds = P2P_SEND_INTERVAL_SECONDS;

// --- FORWARD DECLARATIONS ---
void setup_axp();
void deep_sleep_with_timer(uint32_t seconds);
void print_wakeup_reason();
float read_wood_resistance_ohms();
float calculate_indicated_mc(float R_wood_ohms, const WoodSpecies& species);
float read_esp_temperature_celsius();
float get_temperature_correction(float indicated_mc, float wood_temp_celsius);
float bilinear_interpolation(float x, float y, const float x_points[], int x_count, const float y_points[], int y_count, const float table[][MC_POINTS_COUNT]);

// --- SETUP FUNCTION ---
void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 2000);
    DEBUG_PRINTLN(F("\nStarting ESP32 Wood Moisture Sensor (LoRa P2P Mode)..."));

    print_wakeup_reason();

#ifdef USE_AXP_POWER_MANAGEMENT
    setup_axp();
#endif

    pinMode(MOISTURE_PROBE_POWER_PIN, OUTPUT);
    digitalWrite(MOISTURE_PROBE_POWER_PIN, LOW);

    DEBUG_PRINT(F("Initializing SX1262 radio... "));
    int radio_state = radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER, LORA_PREAMBLE_LENGTH, 3.3, false);
    if (radio_state == RADIOLIB_ERR_NONE) {
        DEBUG_PRINTLN(F("success!"));
    } else {
        DEBUG_PRINT(F("failed, code ")); DEBUG_PRINTLN(radio_state);
        DEBUG_PRINTLN(F("Halting due to radio init failure.")); while (true);
    }
    DEBUG_PRINTLN(F("Radio: Freq=") + String(LORA_FREQUENCY) + F("MHz, BW=") + String(LORA_BANDWIDTH) + F("kHz, SF=") + String(LORA_SPREADING_FACTOR) + F(", CR=4/") + String(LORA_CODING_RATE) + F(", TXPwr=") + String(LORA_TX_POWER) + F("dBm"));
    DEBUG_PRINT(F("Selected Wood Species: ")); DEBUG_PRINTLN(pgm_read_ptr(&species_data[SELECTED_WOOD_SPECIES_INDEX].name));
}

// --- LOOP FUNCTION ---
void loop() {
    DEBUG_PRINTLN(F("\n--- Cycle Start ---"));

    // 1. Read Wood Resistance
    float R_ohms = read_wood_resistance_ohms();
    if (R_ohms < 0 || R_ohms > 500000000.0f) { // Resistance too low (short?) or too high (open?)
        DEBUG_PRINT(F("Unreliable resistance reading: ")); DEBUG_PRINTLN(R_ohms);
        // Decide how to handle: send error, or last known good, or skip send
    }

    // 2. Calculate Indicated Moisture Content (MC)
    WoodSpecies current_species;
    memcpy_P(&current_species, &species_data[SELECTED_WOOD_SPECIES_INDEX], sizeof(WoodSpecies));
    float mc_indicated = calculate_indicated_mc(R_ohms, current_species);
    DEBUG_PRINT(F("Indicated MC (before temp correction): ")); DEBUG_PRINT(mc_indicated); DEBUG_PRINTLN(F(" %"));

    // 3. Read Temperature
    float wood_temp_c = DEFAULT_WOOD_TEMP_CELSIUS;
    #if ENABLE_TEMPERATURE_COMPENSATION == true
        wood_temp_c = read_esp_temperature_celsius();
        DEBUG_PRINT(F("Wood/ESP32 Temp: ")); DEBUG_PRINT(wood_temp_c); DEBUG_PRINTLN(F(" C"));
    #else
        DEBUG_PRINTLN(F("Temperature compensation disabled. Using default temp."));
    #endif

    // 4. Get Temperature Correction Factor
    float mc_corrected = mc_indicated;
    #if ENABLE_TEMPERATURE_COMPENSATION == true
        float temp_correction = get_temperature_correction(mc_indicated, wood_temp_c);
        DEBUG_PRINT(F("Temperature Correction Factor: ")); DEBUG_PRINT(temp_correction); DEBUG_PRINTLN(F(" % MC"));
        mc_corrected += temp_correction;
    #endif
    mc_corrected = constrain(mc_corrected, 0.0f, 100.0f); // Clamp to a sensible range
    DEBUG_PRINT(F("FINAL Corrected MC: ")); DEBUG_PRINT(mc_corrected); DEBUG_PRINTLN(F(" %"));
    
    // 5. Read Battery Voltage
    float battery_v = -1.0;
    #ifdef USE_AXP_POWER_MANAGEMENT
        if (pmic_initialized && PMU) battery_v = PMU->getBattVoltage() / 1000.0f;
        DEBUG_PRINT(F("Battery Voltage: ")); DEBUG_PRINT(battery_v); DEBUG_PRINTLN(F(" V"));
    #endif

    // 6. Prepare Payload (Corrected MC % and Battery Voltage * 10)
    byte payload[2];
    payload[0] = (mc_corrected < 0 || mc_corrected > 100) ? 0xFF : (byte)round(mc_corrected); // 0-100, 0xFF for error state
    payload[1] = (battery_v < 0) ? 0xFF : (byte)constrain((int)round(battery_v * 10), 0, 254);
    DEBUG_PRINT(F("Payload: MC_byte=") + String(payload[0]) + F(", Batt_byte=") + String(payload[1]));

    // 7. Transmit LoRa P2P Packet
    DEBUG_PRINT(F("\nTransmitting LoRa P2P packet... "));
    int transmit_state = radio.transmit(payload, sizeof(payload));
    if (transmit_state == RADIOLIB_ERR_NONE) {
        DEBUG_PRINTLN(F("success!"));
    } else {
        DEBUG_PRINT(F("failed, code ")); DEBUG_PRINTLN(transmit_state);
    }

    // 8. Deep Sleep
    DEBUG_PRINTLN(F("Entering deep sleep for ") + String(sleep_interval_seconds) + F(" seconds."));
    Serial.flush();
    deep_sleep_with_timer(sleep_interval_seconds);
}

// --- SENSOR AND CALCULATION FUNCTIONS ---
float read_wood_resistance_ohms() {
    digitalWrite(MOISTURE_PROBE_POWER_PIN, HIGH);
    delay(ADC_READ_STABILIZATION_MS);
    
    uint32_t adc_sum = 0;
    for (int i = 0; i < ADC_SAMPLES_TO_AVERAGE; i++) {
        adc_sum += analogRead(MOISTURE_PROBE_ADC_PIN);
        delay(10); // Small delay between samples
    }
    digitalWrite(MOISTURE_PROBE_POWER_PIN, LOW);
    float adc_raw_avg = (float)adc_sum / ADC_SAMPLES_TO_AVERAGE;

    DEBUG_PRINT(F("Avg Raw ADC: ")); DEBUG_PRINTLN(adc_raw_avg);

    if (adc_raw_avg >= (ADC_MAX_READING - 1.0f) ) { // Check if close to max (potential open circuit / very high resistance)
        return 1.0e12; // Return a very large resistance value (1 TeraOhm)
    }
    if (adc_raw_avg < 1.0f) { // Check if close to min (potential short circuit)
        return 1.0e-3; // Return a very small resistance value (1 milliOhm)
    }

    // R_wood = R_PULLUP * (adc_raw / (ADC_MAX_VALUE - adc_raw))
    float R_wood = R_PULLUP_OHMS * (adc_raw_avg / (ADC_MAX_READING - adc_raw_avg));
    DEBUG_PRINT(F("Calculated R_wood: ")); DEBUG_PRINT(R_wood); DEBUG_PRINTLN(F(" Ohms"));
    return R_wood;
}

float calculate_indicated_mc(float R_wood_ohms, const WoodSpecies& species) {
    if (R_wood_ohms <= 0) return 0; // Cannot take log of non-positive resistance
    float R_kOhms = R_wood_ohms / 1000.0f;
    if (R_kOhms <= 0) return 0; // Should not happen if R_wood_ohms is positive

    // M = 10^(A + B * log10(R_kOhms))
    float log10_R_kOhms = log10(R_kOhms);
    float mc = pow(10, species.A + (species.B * log10_R_kOhms));
    return mc;
}

float read_esp_temperature_celsius() {
    // Convert internal temperature from Fahrenheit to Celsius
    // temprature_sens_read() returns temp in F for ESP32. Needs ESP32 Arduino core >= 2.0.0
    // For older cores, this function might not be available or might behave differently.
    // If using an older core, you might need to include "soc/sens_reg.h" and read registers directly.
    float temp_f = temprature_sens_read();
    return (temp_f - 32.0) * 5.0 / 9.0;
}

float get_temperature_correction(float indicated_mc, float wood_temp_celsius) {
    if (!ENABLE_TEMPERATURE_COMPENSATION) return 0.0f;

    float wood_temp_f = (wood_temp_celsius * 9.0f / 5.0f) + 32.0f;

    // Clamp inputs to the defined table ranges to avoid out-of-bounds access during interpolation
    // and to handle extrapolation by using edge values.
    wood_temp_f = constrain(wood_temp_f, pgm_read_float(&temp_points_f[0]), pgm_read_float(&temp_points_f[TEMP_POINTS_COUNT - 1]));
    indicated_mc = constrain(indicated_mc, pgm_read_float(&mc_points_indicated[0]), pgm_read_float(&mc_points_indicated[MC_POINTS_COUNT - 1]));

    return bilinear_interpolation(wood_temp_f, indicated_mc, temp_points_f, TEMP_POINTS_COUNT, mc_points_indicated, MC_POINTS_COUNT, correction_table);
}

// Bilinear interpolation function
float bilinear_interpolation(float x, float y, 
                           const float x_points[], int x_count, 
                           const float y_points[], int y_count, 
                           const float table[][MC_POINTS_COUNT]) {
    // Find indices for x (temperature)
    int x_idx = 0;
    while (x_idx < x_count -1 && x > pgm_read_float(&x_points[x_idx+1])) {
        x_idx++;
    }
    // Ensure x_idx is not x_count-1 if x is exactly x_points[x_count-1] to avoid reading past x_idx+1
    if (x_idx == x_count -1 && x == pgm_read_float(&x_points[x_idx])){
         x_idx = x_count -2; // Use the interval before last if x is the last point
    }
    if (x_idx >= x_count -1 ) x_idx = x_count - 2; // Protection
    
    // Find indices for y (indicated MC)
    int y_idx = 0;
    while (y_idx < y_count - 1 && y > pgm_read_float(&y_points[y_idx+1])) {
        y_idx++;
    }
    if (y_idx == y_count - 1 && y == pgm_read_float(&y_points[y_idx])) {
        y_idx = y_count - 2;
    }
    if (y_idx >= y_count -1) y_idx = y_count - 2; // Protection

    float x1 = pgm_read_float(&x_points[x_idx]);
    float x2 = pgm_read_float(&x_points[x_idx + 1]);
    float y1 = pgm_read_float(&y_points[y_idx]);
    float y2 = pgm_read_float(&y_points[y_idx + 1]);

    float q11 = pgm_read_float(&table[x_idx][y_idx]);
    float q12 = pgm_read_float(&table[x_idx][y_idx + 1]);
    float q21 = pgm_read_float(&table[x_idx + 1][y_idx]);
    float q22 = pgm_read_float(&table[x_idx + 1][y_idx + 1]);

    if ((x2 - x1) == 0 || (y2 - y1) == 0) { // Avoid division by zero if points are identical
        // This can happen if input x or y is exactly on a grid line and is also an edge point.
        // Or if the lookup table points are not strictly increasing.
        // Return nearest point or average, here simply q11 as a fallback.
        return q11; 
    }

    float r1 = ((x2 - x) / (x2 - x1)) * q11 + ((x - x1) / (x2 - x1)) * q21;
    float r2 = ((x2 - x) / (x2 - x1)) * q12 + ((x - x1) / (x2 - x1)) * q22;
    float p = ((y2 - y) / (y2 - y1)) * r1 + ((y - y1) / (y2 - y1)) * r2;

    return p;
}


// --- PMIC, SLEEP, AND UTILITY FUNCTIONS ---
#ifdef USE_AXP_POWER_MANAGEMENT
void setup_axp() {
    DEBUG_PRINTLN(F("Initializing AXP192 PMIC..."));
    PMU = new XPowersLib();
    if (!PMU) {
        DEBUG_PRINTLN(F("Failed to allocate XPowersLib object!")); pmic_initialized = false; return;
    }
    int ret = PMU->init(Wire, 21, 22, AXP192_SLAVE_ADDRESS);
    if (ret == XPOWERS_SUCCESS) {
        DEBUG_PRINTLN(F("PMIC Initialized.")); pmic_initialized = true;
        PMU->setPowerOutPut(XPOWERS_LDO2, XPOWERS_ON); PMU->setPowerOutPut(XPOWERS_LDO3, XPOWERS_ON);
        PMU->setPowerOutPut(XPOWERS_DCDC1, XPOWERS_ON);
        PMU->clearIrqStatus();
        PMU->setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);
        PMU->setChargeConstantCurrent(XPOWERS_AXP192_CHG_CUR_100MA);
        DEBUG_PRINT(F("PMIC Battery: ")); DEBUG_PRINT(PMU->getBattVoltage() / 1000.0f); DEBUG_PRINTLN(F("V"));
    } else {
        DEBUG_PRINT(F("PMIC Init Failed, error: ")); DEBUG_PRINTLN(ret); delete PMU; PMU = NULL; pmic_initialized = false;
    }
}
#endif

void deep_sleep_with_timer(uint32_t seconds) {
    DEBUG_PRINTLN(F("Configuring deep sleep for ") + String(seconds) + F(" seconds."));
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    DEBUG_PRINTLN(F("Going to sleep now.")); Serial.flush();
    esp_deep_sleep_start();
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason){
    case ESP_SLEEP_WAKEUP_TIMER : DEBUG_PRINTLN(F("Wakeup: Timer")); break;
    default : DEBUG_PRINT(F("Wakeup: Other (")); DEBUG_PRINT(wakeup_reason); DEBUG_PRINTLN(F(")"));break;
  }
}
