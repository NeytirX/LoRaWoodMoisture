// src/main.cpp - Wood Moisture Sensor Firmware (LoRaWAN via RadioLib)
//
// Target: TTGO T-Beam v1.1/v1.2 (ESP32 + SX1262 + AXP192/AXP2101)
// LoRaWAN: RadioLib with EU433 band, OTAA.
//
// Single-file firmware: the whole measure-send-sleep cycle runs once in
// setup(), then the ESP32 deep-sleeps and restarts on timer wake. loop() is
// never reached. See docs/firmware_architecture.md for the phase breakdown.

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <esp_task_wdt.h>            // ESP32 Task Watchdog Timer
#include <Preferences.h>             // NVS for saving settings

#include "config.h"
#include "wood_species_data.h"
#include "wood_temp_correction_data.h"
#include "session_manager.h"
#include "sensor.h"
#include <CayenneLPP.h>

#ifdef USE_AXP_POWER_MANAGEMENT
#include <XPowersAXP192.tpp>
#include <XPowersAXP2101.tpp>
XPowersAXP192  *PMU192  = nullptr;  // T-Beam v1.1
XPowersAXP2101 *PMU2101 = nullptr;  // T-Beam v1.2
bool pmic_initialized = false;
#endif

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================
void setup_axp();
float pmic_batt_voltage();
void deep_sleep_with_timer(uint32_t seconds);
void print_wakeup_reason();
float calculate_indicated_mc(float R_wood_ohms, const WoodSpecies& species);
float get_temperature_correction(float indicated_mc, float wood_temp_celsius);
float bilinear_interpolation(float x, float y,
                             const float x_points[], int x_count,
                             const float y_points[], int y_count,
                             const float table[][MC_POINTS_COUNT]);
void process_downlink(uint8_t *data, uint8_t len);
uint32_t calculate_sleep_interval();

// =============================================================================
// RADIOLIB: SX1262 Radio + LoRaWAN Node
// =============================================================================
// SX1262 radio module: Module(NSS, DIO1, RST, BUSY)
SX1262 radio = new Module(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);

// LoRaWAN node bound to the radio, using EU433 region
// EU433 is a built-in region in RadioLib
LoRaWANNode node(&radio, &EU433);

// Note: RadioLib v7.6.0 uses internal buffers accessed via getBufferNonces()/getBufferSession().
// We store copies in NVS (nonces) and RTC RAM (session) for persistence across sleep/power cycles.

// =============================================================================
// RTC-PERSISTENT VARIABLES (survive deep sleep)
// =============================================================================
RTC_DATA_ATTR bool lorawan_joined = false;
RTC_DATA_ATTR uint32_t current_interval_seconds = NORMAL_SEND_INTERVAL_SECONDS;
RTC_DATA_ATTR uint8_t join_retry_count = 0;
RTC_DATA_ATTR uint8_t selected_species_index = SELECTED_WOOD_SPECIES_INDEX;

// =============================================================================
// PAYLOAD & SENSOR DATA
// =============================================================================
CayenneLPP lpp(128);

float last_R_kOhms      = -1.0f;
float last_mc_indicated  = -1.0f;
float last_wood_temp_c   = -100.0f;
float last_mc_corrected  = -1.0f;
float last_battery_v     = -1.0f;
float last_esp_temp_c    = -100.0f;

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    // TODO: For production deployments, remove or wrap this 2-second delay in a debug flag.
    // Idling at full power every wake cycle significantly degrades long-term battery life.
    while (!Serial && millis() < 2000);
    DEBUG_PRINTLN(F("\n========================================"));
    DEBUG_PRINTLN(F(" Wood Moisture Sensor (LoRaWAN/RadioLib)"));
    DEBUG_PRINTLN(F("========================================"));

    // Print firmware version
    DEBUG_PRINT(F("Firmware v"));
    DEBUG_PRINT(FIRMWARE_VERSION_MAJOR); DEBUG_PRINT(F("."));
    DEBUG_PRINT(FIRMWARE_VERSION_MINOR); DEBUG_PRINT(F("."));
    DEBUG_PRINTLN(FIRMWARE_VERSION_PATCH);

    print_wakeup_reason();

    // --- Watchdog Timer ---
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SECONDS, true);
    esp_task_wdt_add(NULL);
    DEBUG_PRINT(F("[WDT] Enabled. Timeout: "));
    DEBUG_PRINT(WATCHDOG_TIMEOUT_SECONDS);
    DEBUG_PRINTLN(F(" s"));

    // --- Determine boot type ---
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    bool cold_boot = (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) || (wakeup_reason == 0);
    if (cold_boot) {
        DEBUG_PRINTLN(F("Cold boot or reset. Full initialization."));
        lorawan_joined = false;
        join_retry_count = 0;

        Preferences prefs;
        prefs.begin("app_config", true); // Open read-only
        current_interval_seconds = prefs.getUInt("interval", NORMAL_SEND_INTERVAL_SECONDS);
        selected_species_index = prefs.getUChar("species", SELECTED_WOOD_SPECIES_INDEX);
        prefs.end();

        session_invalidate();
    } else {
        DEBUG_PRINTLN(F("Woke from deep sleep."));
    }

    // --- Probe power pin (OFF initially) ---
    pinMode(MOISTURE_PROBE_POWER_PIN, OUTPUT);
    digitalWrite(MOISTURE_PROBE_POWER_PIN, LOW);

    // --- Print selected species ---
    WoodSpecies sp;
    memcpy_P(&sp, &species_data[selected_species_index], sizeof(WoodSpecies));
    DEBUG_PRINT(F("Selected species ["));
    DEBUG_PRINT(selected_species_index);
    DEBUG_PRINT(F("]: "));
    const char* name_ptr = (const char*)pgm_read_ptr(&sp.name);
    char name_buf[40];
    strncpy_P(name_buf, name_ptr, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    DEBUG_PRINTLN(name_buf);

    // =====================================================================
    // PHASE 1: PMIC Setup
    // =====================================================================
    DEBUG_PRINTLN(F("[Phase] PMIC Setup"));
    #ifdef USE_AXP_POWER_MANAGEMENT
        setup_axp();
    #endif

    // =====================================================================
    // PHASE 1b: Critical Battery Check
    // =====================================================================
    // Read battery voltage immediately after PMIC init and abort before the
    // expensive radio init / OTAA join: on a critically low battery there is no
    // point spending the most power-hungry operation just to skip the cycle.
    #ifdef USE_AXP_POWER_MANAGEMENT
    if (pmic_initialized) {
        last_battery_v = pmic_batt_voltage();
        DEBUG_PRINT(F("[Battery] Voltage: ")); DEBUG_PRINT(last_battery_v); DEBUG_PRINTLN(F(" V"));
        if (last_battery_v < CRITICAL_BATTERY_THRESHOLD_V) {
            DEBUG_PRINTLN(F("[Battery] CRITICAL! Skipping measurement. Extended sleep."));
            uint32_t sleep_s = current_interval_seconds * CRITICAL_BATTERY_SLEEP_MULTIPLIER;
            deep_sleep_with_timer(sleep_s);
            return;
        }
    }
    #endif

    // =====================================================================
    // PHASE 2: Sensor Init
    // =====================================================================
    DEBUG_PRINTLN(F("[Phase] Sensor Init"));
    sensor_init();

    // =====================================================================
    // PHASE 3: Radio Init + LoRaWAN Join/Restore
    // =====================================================================
    DEBUG_PRINTLN(F("[Phase] Radio Init"));

    // The T-Beam wires the LoRa modem to a dedicated SPI bus (not the ESP32
    // default VSPI pins) - bind the default SPIClass to it before radio.begin()
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_CS_PIN);

    // Initialize the SX1262 radio
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        DEBUG_PRINT(F("[Radio] Init FAILED, code: "));
        DEBUG_PRINTLN(state);
        DEBUG_PRINTLN(F("Check wiring: CS=18, DIO1=33, RST=23, BUSY=32"));
        // Sleep and retry on next wake
        deep_sleep_with_timer(LORAWAN_JOIN_RETRY_SLEEP_SECONDS);
        return; // won't reach here after deep_sleep_start
    }
    DEBUG_PRINTLN(F("[Radio] SX1262 initialized OK"));

    // --- Restore session persistence buffers ---
    // RadioLib v7.6.0 API: getBufferNonces()/getBufferSession() return pointers
    //   to internal buffers. setBufferNonces()/setBufferSession() take a single pointer.
    //   Buffer sizes are RADIOLIB_LORAWAN_NONCES_BUF_SIZE and RADIOLIB_LORAWAN_SESSION_BUF_SIZE.

    // Load nonces from NVS (survives power loss)
    uint8_t noncesTemp[RADIOLIB_LORAWAN_NONCES_BUF_SIZE] = {0};
    size_t noncesLen = session_load_nonces(noncesTemp, sizeof(noncesTemp));
    if (noncesLen > 0) {
        node.setBufferNonces(noncesTemp);
    }

    // Load session from RTC (survives deep sleep only)
    uint8_t sessionTemp[RADIOLIB_LORAWAN_SESSION_BUF_SIZE] = {0};
    size_t sessionLen = 0;
    if (session_restore_rtc(sessionTemp, sizeof(sessionTemp), sessionLen)) {
        node.setBufferSession(sessionTemp);
    }

    // --- Attempt OTAA activation (join or restore) ---
    DEBUG_PRINTLN(F("[LoRaWAN] Activating OTAA..."));
    node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);

    state = node.activateOTAA();

    if (state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        DEBUG_PRINTLN(F("[LoRaWAN] Session restored from saved state!"));
        lorawan_joined = true;
        join_retry_count = 0;
    } else if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        DEBUG_PRINTLN(F("[LoRaWAN] New session - fresh join successful!"));
        lorawan_joined = true;
        join_retry_count = 0;

        // Save the new nonces to NVS (they only change on join)
        uint8_t* noncesPtr = node.getBufferNonces();
        if (noncesPtr != nullptr) {
            session_save_nonces(noncesPtr, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
        }
    } else {
        DEBUG_PRINT(F("[LoRaWAN] Activation FAILED, code: "));
        DEBUG_PRINTLN(state);
        lorawan_joined = false;
        join_retry_count++;

        if (join_retry_count >= LORAWAN_JOIN_MAX_RETRIES) {
            DEBUG_PRINTLN(F("[LoRaWAN] Max join retries reached. Extended sleep."));
            join_retry_count = 0;  // Reset for next cycle
            deep_sleep_with_timer(LORAWAN_JOIN_RETRY_SLEEP_SECONDS * 5);
        } else {
            DEBUG_PRINT(F("[LoRaWAN] Join attempt "));
            DEBUG_PRINT(join_retry_count);
            DEBUG_PRINT(F("/"));
            DEBUG_PRINTLN(LORAWAN_JOIN_MAX_RETRIES);
            deep_sleep_with_timer(LORAWAN_JOIN_RETRY_SLEEP_SECONDS);
        }
        return;
    }

    // =====================================================================
    // PHASE 5: Sensor Measurement
    // =====================================================================
    DEBUG_PRINTLN(F("[Phase] Sensor Measurement"));
    esp_task_wdt_reset();

    // Power up moisture probe
    digitalWrite(MOISTURE_PROBE_POWER_PIN, HIGH);

    // --- Resistance Measurement ---
    float R_ohms = read_wood_resistance_ohms();
    last_R_kOhms = R_ohms / 1000.0f;
    DEBUG_PRINT(F("Wood Resistance: "));
    DEBUG_PRINT(last_R_kOhms);
    DEBUG_PRINTLN(F(" kOhms"));

    bool resistance_valid = is_resistance_in_valid_range(R_ohms);

    // --- Indicated Moisture Content ---
    WoodSpecies current_species;
    memcpy_P(&current_species, &species_data[selected_species_index], sizeof(WoodSpecies));
    last_mc_indicated = calculate_indicated_mc(R_ohms, current_species);
    DEBUG_PRINT(F("Indicated MC: "));
    DEBUG_PRINT(last_mc_indicated);
    DEBUG_PRINTLN(F(" %"));

    if (!resistance_valid) {
        DEBUG_PRINTLN(F("WARNING: MC may be unreliable (out-of-range resistance)."));
    }

    // --- Temperature ---
    bool wood_temp_fallback = false;
    last_wood_temp_c = read_wood_temperature(wood_temp_fallback);
    last_esp_temp_c = read_esp_temperature_celsius();
    DEBUG_PRINT(F("Wood Temp: ")); DEBUG_PRINT(last_wood_temp_c); DEBUG_PRINTLN(F(" C"));
    DEBUG_PRINT(F("ESP32 Temp: ")); DEBUG_PRINT(last_esp_temp_c); DEBUG_PRINTLN(F(" C"));

    // --- Temperature Correction ---
    #if ENABLE_TEMPERATURE_COMPENSATION == true
        float temp_correction = get_temperature_correction(last_mc_indicated, last_wood_temp_c);
        DEBUG_PRINT(F("Temp Correction: ")); DEBUG_PRINT(temp_correction); DEBUG_PRINTLN(F(" % MC"));
        last_mc_corrected = constrain(last_mc_indicated + temp_correction, 0.0f, 100.0f);
    #else
        last_mc_corrected = constrain(last_mc_indicated, 0.0f, 100.0f);
        DEBUG_PRINTLN(F("Temperature compensation disabled."));
    #endif
    DEBUG_PRINT(F("FINAL Corrected MC: ")); DEBUG_PRINT(last_mc_corrected); DEBUG_PRINTLN(F(" %"));

    // --- Battery Voltage (re-read for payload) ---
    #ifdef USE_AXP_POWER_MANAGEMENT
    if (pmic_initialized) {
        last_battery_v = pmic_batt_voltage();
        DEBUG_PRINT(F("Battery: ")); DEBUG_PRINT(last_battery_v); DEBUG_PRINTLN(F(" V"));
    }
    #endif

    // Power down moisture probe
    digitalWrite(MOISTURE_PROBE_POWER_PIN, LOW);

    // =====================================================================
    // PHASE 6: Build Payload
    // =====================================================================
    DEBUG_PRINTLN(F("[Phase] Build Payload"));
    lpp.reset();
    if (last_mc_corrected >= 0 && last_mc_corrected <= 100)
        lpp.addAnalogInput(LPP_CHANNEL_WOOD_MC, last_mc_corrected);
    // Only send wood temp when actually measured - never report the
    // fallback default as a measurement
    if (!wood_temp_fallback && last_wood_temp_c > -50 && last_wood_temp_c < 100)
        lpp.addTemperature(LPP_CHANNEL_WOOD_TEMP, last_wood_temp_c);
    if (last_battery_v > 0)
        lpp.addAnalogInput(LPP_CHANNEL_BATTERY_VOLTAGE, last_battery_v);
    // Always sent: 1 = corrected MC used DEFAULT_WOOD_TEMP_CELSIUS, not a
    // measured temp - lets downstream analysis filter affected readings
    lpp.addDigitalInput(LPP_CHANNEL_TEMP_FALLBACK, wood_temp_fallback ? 1 : 0);

    // Optional debug channels (uncomment to include in payload)
    // if (last_mc_indicated >= 0)
    //     lpp.addAnalogInput(LPP_CHANNEL_INDICATED_MC, last_mc_indicated);
    // if (last_R_kOhms >= 0)
    //     lpp.addAnalogInput(LPP_CHANNEL_RESISTANCE, last_R_kOhms);
    // if (last_esp_temp_c > -50)
    //     lpp.addTemperature(LPP_CHANNEL_ESP_TEMP, last_esp_temp_c);

    if (lpp.getSize() == 0) {
        DEBUG_PRINTLN(F("No data to send. Skipping TX."));
        // Skip to sleep
    }

    // =====================================================================
    // PHASE 7: LoRaWAN Uplink (with downlink receive)
    // =====================================================================
    else {
        DEBUG_PRINTLN(F("[Phase] LoRaWAN Uplink"));
        esp_task_wdt_reset();

        uint8_t downlinkPayload[256];
        size_t downlinkLen = sizeof(downlinkPayload);

        // sendReceive: sends uplink on fPort 1, receives any downlink
        // Returns: RADIOLIB_ERR_NONE (no downlink), >0 (downlink fPort),
        //          or negative error code
        int txResult = node.sendReceive(
            lpp.getBuffer(), lpp.getSize(),  // uplink data
            1,                                // fPort = 1
            downlinkPayload, &downlinkLen     // downlink buffer
        );

        if (txResult >= 0) {
            DEBUG_PRINTLN(F("[TX] Uplink successful!"));

            // Check for downlink
            if (txResult > 0 && downlinkLen > 0) {
                DEBUG_PRINT(F("[RX] Downlink on fPort "));
                DEBUG_PRINT(txResult);
                DEBUG_PRINT(F(", "));
                DEBUG_PRINT(downlinkLen);
                DEBUG_PRINTLN(F(" bytes"));
                process_downlink(downlinkPayload, downlinkLen);
            }

            // Save session after successful TX (frame counters updated)
            uint8_t* sessionPtr = node.getBufferSession();
            if (sessionPtr != nullptr) {
                session_save_rtc(sessionPtr, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
            }
            // Also save nonces in case they were updated
            uint8_t* noncesPtr2 = node.getBufferNonces();
            if (noncesPtr2 != nullptr) {
                session_save_nonces(noncesPtr2, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
            }
        } else {
            DEBUG_PRINT(F("[TX] Uplink FAILED, code: "));
            DEBUG_PRINTLN(txResult);

            // On TX failure, still save session to preserve frame counter
            uint8_t* sessionPtr = node.getBufferSession();
            if (sessionPtr != nullptr) {
                session_save_rtc(sessionPtr, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
            }
        }
    }

    // =====================================================================
    // PHASE 8: Deep Sleep
    // =====================================================================
    uint32_t sleep_seconds = calculate_sleep_interval();
    DEBUG_PRINT(F("[Sleep] Entering deep sleep for "));
    DEBUG_PRINT(sleep_seconds);
    DEBUG_PRINTLN(F(" seconds"));
    Serial.flush();

    // Disable watchdog before sleep
    esp_task_wdt_delete(NULL);
    deep_sleep_with_timer(sleep_seconds);
}

// =============================================================================
// LOOP - Not used (everything runs in setup, then deep sleep)
// =============================================================================
// RadioLib uses a synchronous/blocking API. The entire measure-send-sleep cycle
// runs in setup(). After deep sleep, the ESP32 restarts and setup() runs again.
void loop() {
    // Should never reach here. If it does, sleep immediately.
    DEBUG_PRINTLN(F("[WARN] Entered loop() unexpectedly. Sleeping..."));
    deep_sleep_with_timer(NORMAL_SEND_INTERVAL_SECONDS);
}

// =============================================================================
// DOWNLINK COMMAND PROCESSING
// =============================================================================
void process_downlink(uint8_t *data, uint8_t len) {
    if (len < 1) return;

    uint8_t cmd = data[0];
    switch (cmd) {
        case DOWNLINK_CMD_SET_INTERVAL:
            if (len >= 3) {
                uint16_t interval_minutes = (data[1] << 8) | data[2];
                if (interval_minutes >= 1 && interval_minutes <= 1440) {
                    current_interval_seconds = (uint32_t)interval_minutes * 60;
                    Preferences prefs;
                    prefs.begin("app_config", false);
                    prefs.putUInt("interval", current_interval_seconds);
                    prefs.end();
                    DEBUG_PRINT(F("  [DL] Interval set to "));
                    DEBUG_PRINT(interval_minutes);
                    DEBUG_PRINTLN(F(" minutes"));
                } else {
                    DEBUG_PRINTLN(F("  [DL] Invalid interval value"));
                }
            }
            break;

        case DOWNLINK_CMD_SET_SPECIES:
            if (len >= 2) {
                uint8_t species_idx = data[1];
                if (species_idx < NUM_WOOD_SPECIES) {
                    selected_species_index = species_idx;
                    Preferences prefs;
                    prefs.begin("app_config", false);
                    prefs.putUChar("species", selected_species_index);
                    prefs.end();
                    DEBUG_PRINT(F("  [DL] Species changed to index "));
                    DEBUG_PRINTLN(species_idx);
                } else {
                    DEBUG_PRINTLN(F("  [DL] Invalid species index"));
                }
            }
            break;

        case DOWNLINK_CMD_FORCE_REJOIN:
            DEBUG_PRINTLN(F("  [DL] Force rejoin requested"));
            session_invalidate();
            lorawan_joined = false;
            join_retry_count = 0;
            break;

        case DOWNLINK_CMD_SET_TX_POWER:
            if (len >= 2) {
                DEBUG_PRINT(F("  [DL] TX power index: "));
                DEBUG_PRINTLN(data[1]);
                // RadioLib can set TX power via node.setTxPower(data[1])
                // but ADR typically manages this. Left for future use.
            }
            break;

        default:
            DEBUG_PRINT(F("  [DL] Unknown command: 0x"));
            if (cmd < 16) Serial.print('0');
            Serial.println(cmd, HEX);
            break;
    }
}

// =============================================================================
// BATTERY-AWARE SLEEP INTERVAL
// =============================================================================
uint32_t calculate_sleep_interval() {
    uint32_t interval = current_interval_seconds;

    #ifdef USE_AXP_POWER_MANAGEMENT
    if (last_battery_v > 0) {
        if (last_battery_v < CRITICAL_BATTERY_THRESHOLD_V) {
            interval *= CRITICAL_BATTERY_SLEEP_MULTIPLIER;
            DEBUG_PRINT(F("[Battery] CRITICAL ("));
            DEBUG_PRINT(last_battery_v);
            DEBUG_PRINT(F("V). Sleep x"));
            DEBUG_PRINTLN(CRITICAL_BATTERY_SLEEP_MULTIPLIER);
        } else if (last_battery_v < LOW_BATTERY_THRESHOLD_V) {
            interval *= LOW_BATTERY_SLEEP_MULTIPLIER;
            DEBUG_PRINT(F("[Battery] LOW ("));
            DEBUG_PRINT(last_battery_v);
            DEBUG_PRINT(F("V). Sleep x"));
            DEBUG_PRINTLN(LOW_BATTERY_SLEEP_MULTIPLIER);
        }
    }
    #endif

    return interval;
}

// =============================================================================
// MOISTURE CONTENT CALCULATION
// =============================================================================
float calculate_indicated_mc(float R_wood_ohms, const WoodSpecies& species) {
    if (R_wood_ohms <= 0) return -1.0f;
    float R_kOhms = R_wood_ohms / 1000.0f;
    if (R_kOhms <= 1e-3f) return 250.0f;
    if (R_kOhms >= 1e9f)  return 5.0f;
    float log10_R_kOhms = log10(R_kOhms);
    return pow(10, species.A + (species.B * log10_R_kOhms));
}

// =============================================================================
// TEMPERATURE CORRECTION (FPL GTR-06 Table 2)
// =============================================================================
float get_temperature_correction(float indicated_mc, float wood_temp_celsius) {
    if (!ENABLE_TEMPERATURE_COMPENSATION) return 0.0f;

    float wood_temp_f = (wood_temp_celsius * 9.0f / 5.0f) + 32.0f;

    float first_temp = pgm_read_float(&temp_points_f[0]);
    float last_temp  = pgm_read_float(&temp_points_f[TEMP_POINTS_COUNT - 1]);
    float first_mc   = pgm_read_float(&mc_points_indicated[0]);
    float last_mc    = pgm_read_float(&mc_points_indicated[MC_POINTS_COUNT - 1]);

    wood_temp_f  = constrain(wood_temp_f, first_temp, last_temp);
    indicated_mc = constrain(indicated_mc, first_mc, last_mc);

    return bilinear_interpolation(wood_temp_f, indicated_mc,
                                  temp_points_f, TEMP_POINTS_COUNT,
                                  mc_points_indicated, MC_POINTS_COUNT,
                                  correction_table);
}

float bilinear_interpolation(float x, float y,
                             const float x_points[], int x_count,
                             const float y_points[], int y_count,
                             const float table[][MC_POINTS_COUNT]) {
    int x_idx = 0;
    while (x_idx < x_count - 2 && x > pgm_read_float(&x_points[x_idx + 1])) x_idx++;
    int y_idx = 0;
    while (y_idx < y_count - 2 && y > pgm_read_float(&y_points[y_idx + 1])) y_idx++;

    float x1  = pgm_read_float(&x_points[x_idx]);
    float x2  = pgm_read_float(&x_points[x_idx + 1]);
    float y1  = pgm_read_float(&y_points[y_idx]);
    float y2  = pgm_read_float(&y_points[y_idx + 1]);
    float q11 = pgm_read_float(&table[x_idx][y_idx]);
    float q12 = pgm_read_float(&table[x_idx][y_idx + 1]);
    float q21 = pgm_read_float(&table[x_idx + 1][y_idx]);
    float q22 = pgm_read_float(&table[x_idx + 1][y_idx + 1]);

    if ((x2 - x1) == 0 || (y2 - y1) == 0) return q11;

    float r1 = ((x2 - x) / (x2 - x1)) * q11 + ((x - x1) / (x2 - x1)) * q21;
    float r2 = ((x2 - x) / (x2 - x1)) * q12 + ((x - x1) / (x2 - x1)) * q22;
    return ((y2 - y) / (y2 - y1)) * r1 + ((y - y1) / (y2 - y1)) * r2;
}

// =============================================================================
// HARDWARE SETUP & UTILITY FUNCTIONS
// =============================================================================

#ifdef USE_AXP_POWER_MANAGEMENT
// Detect the PMIC (AXP192 on T-Beam v1.1, AXP2101 on v1.2), power the LoRa
// rail, cut GPS power, and configure the charger.
void setup_axp() {
    DEBUG_PRINTLN(F("Init PMIC (AXP192/AXP2101)..."));

    // --- AXP192 (T-Beam v1.1) ---
    PMU192 = new XPowersAXP192();
    if (PMU192 && PMU192->init(Wire, 21, 22, AXP192_SLAVE_ADDRESS)) {
        DEBUG_PRINTLN(F("PMIC: AXP192 (T-Beam v1.1) initialized OK."));
        pmic_initialized = true;

        PMU192->enableLDO2();    // LoRa radio power
        PMU192->disableLDO3();   // GPS off - saves ~50mA
        PMU192->enableDC1();     // ESP32 core power
        PMU192->clearIrqStatus();
        PMU192->setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);
        PMU192->setChargerConstantCurr(XPOWERS_AXP192_CHG_CUR_100MA);
        return;
    }
    delete PMU192;
    PMU192 = nullptr;

    // --- AXP2101 (T-Beam v1.2) ---
    PMU2101 = new XPowersAXP2101();
    if (PMU2101 && PMU2101->init(Wire, 21, 22, AXP2101_SLAVE_ADDRESS)) {
        DEBUG_PRINTLN(F("PMIC: AXP2101 (T-Beam v1.2) initialized OK."));
        pmic_initialized = true;

        PMU2101->enableALDO2();  // LoRa radio power
        PMU2101->disableALDO3(); // GPS off
        PMU2101->enableDC1();    // ESP32 core power
        PMU2101->clearIrqStatus();
        PMU2101->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
        PMU2101->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_100MA);
        return;
    }
    delete PMU2101;
    PMU2101 = nullptr;

    DEBUG_PRINTLN(F("PMIC init failed! (no AXP192/AXP2101 found)"));
    pmic_initialized = false;
}

/**
 * Battery voltage in volts from whichever PMIC was detected (-1 if none).
 */
float pmic_batt_voltage() {
    if (PMU192)  return PMU192->getBattVoltage() / 1000.0f;
    if (PMU2101) return PMU2101->getBattVoltage() / 1000.0f;
    return -1.0f;
}
#endif

void deep_sleep_with_timer(uint32_t seconds) {
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    DEBUG_PRINTLN(F("Entering deep sleep..."));
    Serial.flush();
    esp_deep_sleep_start();
}

void print_wakeup_reason() {
    esp_sleep_wakeup_cause_t r = esp_sleep_get_wakeup_cause();
    switch (r) {
        case ESP_SLEEP_WAKEUP_TIMER:
            DEBUG_PRINTLN(F("Wake reason: Timer"));
            break;
        default:
            DEBUG_PRINT(F("Wake reason: Other ("));
            DEBUG_PRINT(r);
            DEBUG_PRINTLN(F(")"));
            break;
    }
}
