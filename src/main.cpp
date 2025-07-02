// src/main.cpp - Main Firmware (LoRaWAN Wood Moisture Version)

#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include "config.h"
#include "wood_species_data.h"
#include "wood_temp_correction_data.h"
#include <CayenneLPP.h>

#ifdef USE_AXP_POWER_MANAGEMENT
#include <XPowersLib.h>
XPowersLibInterface *PMU = NULL;
bool pmic_initialized = false;
#endif

// ESP32 Internal Temperature Sensor
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read(); // Function to read ESP32 internal temperature
#ifdef __cplusplus
}
#endif

// --- FORWARD DECLARATIONS ---
void setup_axp();
void deep_sleep_with_timer(uint32_t seconds);
void print_wakeup_reason();
void print_hex2(unsigned v);
void setup_lora_pins();

float read_wood_resistance_ohms();
float calculate_indicated_mc(float R_wood_ohms, const WoodSpecies& species);
float read_esp_temperature_celsius();
float get_temperature_correction(float indicated_mc, float wood_temp_celsius);
float bilinear_interpolation(float x, float y, const float x_points[], int x_count, const float y_points[], int y_count, const float table[][MC_POINTS_COUNT]);

// --- STATE MACHINE ---
typedef enum {
    STATE_INIT,
    STATE_PMIC_SETUP,
    STATE_LORA_INIT,
    STATE_JOIN_LORAWAN,
    STATE_IDLE,
    STATE_POWER_UP_SENSOR,
    STATE_READ_SENSOR_AND_PREPARE_DATA,
    STATE_TRANSMIT_DATA,
    STATE_WAIT_FOR_TX_COMPLETE,
    STATE_POWER_DOWN_SENSOR,
    STATE_ENTER_SLEEP,
    STATE_ERROR
} device_state_t;

RTC_DATA_ATTR device_state_t currentState = STATE_INIT;
RTC_DATA_ATTR bool lorawan_joined = false;
RTC_DATA_ATTR uint32_t current_interval_seconds = NORMAL_SEND_INTERVAL_SECONDS;
RTC_DATA_ATTR uint8_t join_retry_count = 0;
RTC_DATA_ATTR uint8_t tx_retry_count = 0; // For application-level retries if LMIC fails a send

// --- LORAWAN & LMIC ---
osjob_t sendjob;
const lmic_pinmap lmic_pins = {
    .nss = LORA_CS_PIN, .rxtx = LMIC_UNUSED_PIN, .rst = LORA_RST_PIN,
    .dio = {LORA_DIO1_PIN, LORA_DIO1_PIN, LMIC_UNUSED_PIN},
    .rxtx_rx_active = 0, .rssi_cal = 10, .spi_freq = 8000000,
    .busy = LORA_BUSY_PIN, .tcxo_vcc_pin = LMIC_UNUSED_PIN, .board_power_pin = LMIC_UNUSED_PIN
};

CayenneLPP lpp(128); // Increased LPP buffer size for more data points

// --- SENSOR DATA (results of measurement to be put in LPP) ---
float last_R_kOhms = -1.0f;
float last_mc_indicated = -1.0f;
float last_wood_temp_c = -100.0f;
float last_mc_corrected = -1.0f;
float last_battery_v = -1.0f;
float last_esp_temp_c = -100.0f;

// --- LMIC CALLBACKS ---
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); } // Ensure DEVEUI in config.h is MSB
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

// --- SETUP FUNCTION ---
void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 2000);
    DEBUG_PRINTLN(F("\nStarting ESP32 Wood Moisture Sensor (LoRaWAN Mode)..."));
    print_wakeup_reason();

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (currentState == STATE_INIT || wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED || wakeup_reason == 0) {
        DEBUG_PRINTLN(F("Cold boot or reset. Initializing..."));
        lorawan_joined = false; join_retry_count = 0; tx_retry_count = 0;
        current_interval_seconds = NORMAL_SEND_INTERVAL_SECONDS;
        currentState = STATE_PMIC_SETUP;
    } else {
        DEBUG_PRINTLN(F("Woke from sleep."));
    }

    pinMode(MOISTURE_PROBE_POWER_PIN, OUTPUT);
    digitalWrite(MOISTURE_PROBE_POWER_PIN, LOW);
    DEBUG_PRINT(F("Selected Wood Species: ")); DEBUG_PRINTLN(pgm_read_ptr(&species_data[SELECTED_WOOD_SPECIES_INDEX].name));
}

// --- LOOP FUNCTION (STATE MACHINE) ---
void loop() {
    switch (currentState) {
        case STATE_INIT:
            DEBUG_PRINTLN(F("[S] INIT"));
            lorawan_joined = false; join_retry_count = 0; tx_retry_count = 0;
            current_interval_seconds = NORMAL_SEND_INTERVAL_SECONDS;
            currentState = STATE_PMIC_SETUP;
            break;

        case STATE_PMIC_SETUP:
            DEBUG_PRINTLN(F("[S] PMIC_SETUP"));
            #ifdef USE_AXP_POWER_MANAGEMENT
                setup_axp();
            #endif
            currentState = STATE_LORA_INIT;
            break;

        case STATE_LORA_INIT:
            DEBUG_PRINTLN(F("[S] LORA_INIT"));
            setup_lora_pins();
            os_init();
            LMIC_reset();
            // LMIC_setClockError(MAX_CLOCK_ERROR_M_PPM * 5 / 100); // For TCXO (5PPM example)
            LMIC_setClockError(1 * MAX_CLOCK_ERROR_PERCENT); // Default 1% for crystal if TCXO setting is problematic

            #if defined(CFG_eu433)
                DEBUG_PRINTLN(F("LMIC: EU433 selected."));
                // LMIC uses default channels for EU433 based on regional parameters.
            #elif defined(CFG_us915)
                DEBUG_PRINTLN(F("LMIC: US915 selected."));
                LMIC_selectSubBand(1); // Example sub-band
            #else
                #warning "LoRaWAN region not explicitly handled in main.cpp for sub-band selection if required."
            #endif

            if (lorawan_joined) { // If already joined (e.g. after deep sleep with session saved)
                 // LMIC.devaddr, LMIC.nwkKey, LMIC.artKey should be restored if session persistence is implemented by LMIC itself for ESP32
                 // or if manually restored from RTC. For OTAA, best to rejoin if uncertain.
                 // For simplicity, we will always attempt to join if lorawan_joined is false from RTC.
                 // If true, we assume LMIC might have restored or we can try to set it.
                 // LMIC_setSession (0x1, LMIC.devaddr, (u1_t*)LMIC.nwkKey, (u1_t*)LMIC.artKey); // This would be for ABP or manual session restore
                DEBUG_PRINTLN(F("Attempting to use existing session (if LMIC preserved it). Else will need to join."));
                currentState = STATE_IDLE; 
            } else {
                currentState = STATE_JOIN_LORAWAN;
            }
            break;

        case STATE_JOIN_LORAWAN:
            DEBUG_PRINTLN(F("[S] JOIN_LORAWAN"));
            if (join_retry_count < LORAWAN_JOIN_MAX_RETRIES) {
                DEBUG_PRINT(F("Attempting LoRaWAN join (Attempt: ")); DEBUG_PRINT(join_retry_count + 1); DEBUG_PRINTLN(F(")"));
                LMIC_startJoining();
                // Event callback will handle EV_JOINED or EV_JOIN_FAILED
            } else {
                DEBUG_PRINTLN(F("Max join retries. Sleeping before next cycle."));
                current_interval_seconds = LORAWAN_JOIN_RETRY_SLEEP_SECONDS * 5; 
                currentState = STATE_ENTER_SLEEP;
            }
            break;

        case STATE_IDLE:
            DEBUG_PRINTLN(F("[S] IDLE -> Power Up Sensor"));
            currentState = STATE_POWER_UP_SENSOR;
            break;

        case STATE_POWER_UP_SENSOR:
            DEBUG_PRINTLN(F("[S] POWER_UP_SENSOR"));
            digitalWrite(MOISTURE_PROBE_POWER_PIN, HIGH);
            delay(ADC_READ_STABILIZATION_MS); 
            currentState = STATE_READ_SENSOR_AND_PREPARE_DATA;
            break;

        case STATE_READ_SENSOR_AND_PREPARE_DATA:
            DEBUG_PRINTLN(F("[S] READ_SENSOR_AND_PREPARE_DATA"));
            {
                last_R_kOhms = read_wood_resistance_ohms() / 1000.0f;
                DEBUG_PRINT(F("Wood Resistance: ")); DEBUG_PRINT(last_R_kOhms); DEBUG_PRINTLN(F(" kOhms"));

                WoodSpecies current_species;
                memcpy_P(&current_species, &species_data[SELECTED_WOOD_SPECIES_INDEX], sizeof(WoodSpecies));
                last_mc_indicated = calculate_indicated_mc(last_R_kOhms * 1000.0f, current_species);
                DEBUG_PRINT(F("Indicated MC: ")); DEBUG_PRINT(last_mc_indicated); DEBUG_PRINTLN(F(" %"));

                last_esp_temp_c = read_esp_temperature_celsius(); // Read ESP32 chip temp
                DEBUG_PRINT(F("ESP32 Temp: ")); DEBUG_PRINT(last_esp_temp_c); DEBUG_PRINTLN(F(" C"));

                last_wood_temp_c = last_esp_temp_c; // Use ESP32 temp as proxy for wood temp
                #if ENABLE_TEMPERATURE_COMPENSATION == true
                    // If a dedicated wood temp sensor existed, it would be read here.
                    // For now, last_wood_temp_c is already set to last_esp_temp_c.
                    DEBUG_PRINT(F("Using Wood Temp (Proxy from ESP): ")); DEBUG_PRINT(last_wood_temp_c); DEBUG_PRINTLN(F(" C for correction."));
                    float temp_correction = get_temperature_correction(last_mc_indicated, last_wood_temp_c);
                    DEBUG_PRINT(F("Temp Correction Factor: ")); DEBUG_PRINT(temp_correction); DEBUG_PRINTLN(F(" % MC"));
                    last_mc_corrected = constrain(last_mc_indicated + temp_correction, 0.0f, 100.0f);
                #else
                    last_mc_corrected = constrain(last_mc_indicated, 0.0f, 100.0f);
                    DEBUG_PRINTLN(F("Temperature compensation disabled."));
                #endif
                DEBUG_PRINT(F("FINAL Corrected MC: ")); DEBUG_PRINT(last_mc_corrected); DEBUG_PRINTLN(F(" %"));

                #ifdef USE_AXP_POWER_MANAGEMENT
                    if (pmic_initialized && PMU) last_battery_v = PMU->getBattVoltage() / 1000.0f;
                    DEBUG_PRINT(F("Battery Voltage: ")); DEBUG_PRINT(last_battery_v); DEBUG_PRINTLN(F(" V"));
                #endif

                lpp.reset();
                if (last_mc_corrected >= 0 && last_mc_corrected <= 100) 
                    lpp.addAnalogInput(LPP_CHANNEL_WOOD_MC, last_mc_corrected); // Cayenne LPP wants float here
                if (last_wood_temp_c > -50 && last_wood_temp_c < 100) // Basic sanity check for temp
                    lpp.addTemperature(LPP_CHANNEL_WOOD_TEMP, last_wood_temp_c);
                if (last_battery_v > 0)
                    lpp.addAnalogInput(LPP_CHANNEL_BATTERY_VOLTAGE, last_battery_v);
                
                // Optional: send more debug data if payload size allows
                // if (last_mc_indicated >= 0) lpp.addAnalogInput(LPP_CHANNEL_INDICATED_MC, last_mc_indicated);
                // if (last_R_kOhms >= 0) lpp.addAnalogInput(LPP_CHANNEL_RESISTANCE, last_R_kOhms);
                // if (last_esp_temp_c > -50) lpp.addTemperature(LPP_CHANNEL_ESP_TEMP, last_esp_temp_c);
            }
            if (lpp.getSize() == 0) {
                DEBUG_PRINTLN(F("No data to send. Skipping TX."));
                currentState = STATE_POWER_DOWN_SENSOR;
            } else {
                currentState = STATE_TRANSMIT_DATA;
            }
            break;

        case STATE_TRANSMIT_DATA:
            DEBUG_PRINTLN(F("[S] TRANSMIT_DATA"));
            if (LMIC.opmode & OP_TXRXPEND) {
                DEBUG_PRINTLN(F("LoRaWAN busy (OP_TXRXPEND)."));
            } else if (!lorawan_joined) {
                DEBUG_PRINTLN(F("Not joined. Cannot TX. -> LORA_INIT"));
                currentState = STATE_LORA_INIT; join_retry_count = 0;
            } else {
                DEBUG_PRINT(F("Queueing LoRaWAN packet. Size: ")); DEBUG_PRINTLN(lpp.getSize());
                LMIC_setTxData2(1, lpp.getBuffer(), lpp.getSize(), 0); // Port 1, unconfirmed
                currentState = STATE_WAIT_FOR_TX_COMPLETE;
                tx_retry_count = 0;
            }
            break;

        case STATE_WAIT_FOR_TX_COMPLETE:
            //DEBUG_PRINTLN(F("[S] WAIT_FOR_TX_COMPLETE")); // Can be verbose
            // EV_TXCOMPLETE in onEvent will change state.
            break; 

        case STATE_POWER_DOWN_SENSOR:
            DEBUG_PRINTLN(F("[S] POWER_DOWN_SENSOR"));
            digitalWrite(MOISTURE_PROBE_POWER_PIN, LOW);
            currentState = STATE_ENTER_SLEEP;
            break;

        case STATE_ENTER_SLEEP:
            DEBUG_PRINTLN(F("[S] ENTER_SLEEP"));
            DEBUG_PRINT(F("Sleeping for ")); DEBUG_PRINT(current_interval_seconds); DEBUG_PRINTLN(F(" s."));
            Serial.flush(); 
            deep_sleep_with_timer(current_interval_seconds);
            break;

        case STATE_ERROR:
            DEBUG_PRINTLN(F("[S] ERROR. Resetting."));
            delay(5000); ESP.restart();
            break;
        default:
            DEBUG_PRINTLN(F("Unknown state! -> INIT")); currentState = STATE_INIT; break;
    }
    os_runloop_once();
}

// --- LORAWAN EVENT CALLBACK ---
void onEvent(ev_t ev) {
    Serial.print(os_getTime()); Serial.print(F(": "));
    switch (ev) {
        case EV_SCAN_TIMEOUT: DEBUG_PRINTLN(F("EV_SCAN_TIMEOUT")); break;
        case EV_BEACON_FOUND: DEBUG_PRINTLN(F("EV_BEACON_FOUND")); break;
        case EV_BEACON_MISSED: DEBUG_PRINTLN(F("EV_BEACON_MISSED")); break;
        case EV_BEACON_TRACKED: DEBUG_PRINTLN(F("EV_BEACON_TRACKED")); break;
        case EV_JOINING: DEBUG_PRINTLN(F("EV_JOINING")); break;
        case EV_JOINED:
            DEBUG_PRINTLN(F("EV_JOINED"));
            lorawan_joined = true; join_retry_count = 0;
            LMIC_setLinkCheckMode(0); // Disable link check validation after join
            current_interval_seconds = NORMAL_SEND_INTERVAL_SECONDS; // Ensure normal interval after join
            currentState = STATE_IDLE;
            break;
        case EV_JOIN_FAILED:
            DEBUG_PRINTLN(F("EV_JOIN_FAILED"));
            lorawan_joined = false; join_retry_count++;
            current_interval_seconds = LORAWAN_JOIN_RETRY_SLEEP_SECONDS;
            currentState = STATE_ENTER_SLEEP; // Sleep then retry LORA_INIT and JOIN
            break;
        case EV_REJOIN_FAILED: DEBUG_PRINTLN(F("EV_REJOIN_FAILED")); lorawan_joined = false; currentState = STATE_LORA_INIT; join_retry_count = 0; break;
        case EV_TXCOMPLETE:
            DEBUG_PRINTLN(F("EV_TXCOMPLETE (includes RX win)"));
            if (LMIC.txrxFlags & TXRX_ACK) DEBUG_PRINTLN(F("Received ACK"));
            if (LMIC.dataLen) {
                DEBUG_PRINT(F("Received ")); DEBUG_PRINT(LMIC.dataLen); DEBUG_PRINTLN(F(" bytes of payload (downlink)"));
                // TODO: Process downlink if any
            }
            tx_retry_count = 0;
            currentState = STATE_POWER_DOWN_SENSOR;
            break;
        case EV_LOST_TSYNC: DEBUG_PRINTLN(F("EV_LOST_TSYNC")); break;
        case EV_RESET: DEBUG_PRINTLN(F("EV_RESET")); break;
        case EV_RXCOMPLETE: DEBUG_PRINTLN(F("EV_RXCOMPLETE")); break;
        case EV_LINK_DEAD: DEBUG_PRINTLN(F("EV_LINK_DEAD")); lorawan_joined = false; currentState = STATE_LORA_INIT; join_retry_count = 0; break;
        case EV_LINK_ALIVE: DEBUG_PRINTLN(F("EV_LINK_ALIVE")); break;
        case EV_TXSTART: DEBUG_PRINTLN(F("EV_TXSTART")); break;
        case EV_TXCANCELED: 
            DEBUG_PRINTLN(F("EV_TXCANCELED"));
            tx_retry_count++;
            if (tx_retry_count < LORAWAN_MAX_TX_RETRIES) {
                 DEBUG_PRINTLN(F("TX Canceled/Failed, retrying TX."));
                 currentState = STATE_TRANSMIT_DATA; 
            } else {
                DEBUG_PRINTLN(F("Max TX retries for packet. Giving up."));
                currentState = STATE_POWER_DOWN_SENSOR;
            }
            break;
        case EV_JOIN_TXCOMPLETE: DEBUG_PRINTLN(F("EV_JOIN_TXCOMPLETE: Join Req Sent.")); break;
        default: DEBUG_PRINT(F("Unknown event: ")); DEBUG_PRINTLN((unsigned)ev); break;
    }
}

// --- SENSOR, CALCULATION, AND HELPER FUNCTIONS (Wood Moisture Logic from P2P branch) ---
float read_wood_resistance_ohms() {
    digitalWrite(MOISTURE_PROBE_POWER_PIN, HIGH);
    delay(ADC_READ_STABILIZATION_MS);
    uint32_t adc_sum = 0;
    for (int i = 0; i < ADC_SAMPLES_TO_AVERAGE; i++) {
        adc_sum += analogRead(MOISTURE_PROBE_ADC_PIN);
        delay(5); // Small delay
    }
    digitalWrite(MOISTURE_PROBE_POWER_PIN, LOW);
    float adc_raw_avg = (float)adc_sum / ADC_SAMPLES_TO_AVERAGE;
    DEBUG_PRINT(F("Avg Raw ADC: ")); DEBUG_PRINTLN(adc_raw_avg);
    if (adc_raw_avg >= (ADC_MAX_READING - 1.0f) ) return 1.0e12; 
    if (adc_raw_avg < 1.0f) return 1.0e-3; 
    float R_wood = R_PULLUP_OHMS * (adc_raw_avg / (ADC_MAX_READING - adc_raw_avg));
    return R_wood;
}

float calculate_indicated_mc(float R_wood_ohms, const WoodSpecies& species) {
    if (R_wood_ohms <= 0) return -1.0f; // Invalid resistance
    float R_kOhms = R_wood_ohms / 1000.0f;
    if (R_kOhms <= 1e-3) return 250.0f; // Effectively a short, very high MC (cap to avoid math errors)
    if (R_kOhms >= 1e9) return 5.0f;  // Effectively open, very low MC (cap)
    float log10_R_kOhms = log10(R_kOhms);
    return pow(10, species.A + (species.B * log10_R_kOhms));
}

float read_esp_temperature_celsius() {
    float temp_f = temprature_sens_read();
    return (temp_f - 32.0f) * 5.0f / 9.0f;
}

float get_temperature_correction(float indicated_mc, float wood_temp_celsius) {
    if (!ENABLE_TEMPERATURE_COMPENSATION) return 0.0f;
    float wood_temp_f = (wood_temp_celsius * 9.0f / 5.0f) + 32.0f;
    float first_temp_point = pgm_read_float(&temp_points_f[0]);
    float last_temp_point = pgm_read_float(&temp_points_f[TEMP_POINTS_COUNT - 1]);
    float first_mc_point = pgm_read_float(&mc_points_indicated[0]);
    float last_mc_point = pgm_read_float(&mc_points_indicated[MC_POINTS_COUNT - 1]);

    wood_temp_f = constrain(wood_temp_f, first_temp_point, last_temp_point);
    indicated_mc = constrain(indicated_mc, first_mc_point, last_mc_point);
    return bilinear_interpolation(wood_temp_f, indicated_mc, temp_points_f, TEMP_POINTS_COUNT, mc_points_indicated, MC_POINTS_COUNT, correction_table);
}

float bilinear_interpolation(float x, float y, 
                           const float x_points[], int x_count, 
                           const float y_points[], int y_count, 
                           const float table[][MC_POINTS_COUNT]) {
    int x_idx = 0; while (x_idx < x_count - 2 && x > pgm_read_float(&x_points[x_idx+1])) x_idx++;
    int y_idx = 0; while (y_idx < y_count - 2 && y > pgm_read_float(&y_points[y_idx+1])) y_idx++;

    float x1 = pgm_read_float(&x_points[x_idx]);
    float x2 = pgm_read_float(&x_points[x_idx + 1]);
    float y1 = pgm_read_float(&y_points[y_idx]);
    float y2 = pgm_read_float(&y_points[y_idx + 1]);
    float q11 = pgm_read_float(&table[x_idx][y_idx]);
    float q12 = pgm_read_float(&table[x_idx][y_idx + 1]);
    float q21 = pgm_read_float(&table[x_idx + 1][y_idx]);
    float q22 = pgm_read_float(&table[x_idx + 1][y_idx + 1]);

    if ((x2 - x1) == 0 || (y2 - y1) == 0) return q11; 
    float r1 = ((x2 - x) / (x2 - x1)) * q11 + ((x - x1) / (x2 - x1)) * q21;
    float r2 = ((x2 - x) / (x2 - x1)) * q12 + ((x - x1) / (x2 - x1)) * q22;
    return ((y2 - y) / (y2 - y1)) * r1 + ((y - y1) / (y2 - y1)) * r2;
}

// --- STANDARD PMIC, SLEEP, LORA PIN SETUP, UTILITY FUNCTIONS ---
void setup_lora_pins() {
    pinMode(lmic_pins.nss, OUTPUT); digitalWrite(lmic_pins.nss, HIGH);
    if (lmic_pins.rst != LMIC_UNUSED_PIN) {
        pinMode(lmic_pins.rst, OUTPUT); digitalWrite(lmic_pins.rst, HIGH); delay(5);
        digitalWrite(lmic_pins.rst, LOW); delay(5); digitalWrite(lmic_pins.rst, HIGH); delay(5);
    }
    if (lmic_pins.dio[0] != LMIC_UNUSED_PIN) pinMode(lmic_pins.dio[0], INPUT);
    if (lmic_pins.dio[1] != LMIC_UNUSED_PIN) pinMode(lmic_pins.dio[1], INPUT);
    if (lmic_pins.busy != LMIC_UNUSED_PIN) pinMode(lmic_pins.busy, INPUT);
}

#ifdef USE_AXP_POWER_MANAGEMENT
void setup_axp() {
    DEBUG_PRINTLN(F("Init AXP...")); PMU = new XPowersLib();
    if (!PMU) {DEBUG_PRINTLN(F("PMU alloc fail")); pmic_initialized = false; return;}
    int ret = PMU->init(Wire, 21, 22, AXP192_SLAVE_ADDRESS);
    if (ret == XPOWERS_SUCCESS) {
        DEBUG_PRINTLN(F("PMIC Ok.")); pmic_initialized = true;
        PMU->setPowerOutPut(XPOWERS_LDO2, XPOWERS_ON); PMU->setPowerOutPut(XPOWERS_LDO3, XPOWERS_ON);
        PMU->setPowerOutPut(XPOWERS_DCDC1, XPOWERS_ON);
        PMU->clearIrqStatus(); PMU->setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);
        PMU->setChargeConstantCurrent(XPOWERS_AXP192_CHG_CUR_100MA);
    } else {DEBUG_PRINT(F("PMIC Fail: ")); DEBUG_PRINTLN(ret); delete PMU; PMU = NULL; pmic_initialized = false;}
}
#endif

void deep_sleep_with_timer(uint32_t seconds) {
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    DEBUG_PRINTLN(F("Sleep...")); Serial.flush(); esp_deep_sleep_start();
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t r = esp_sleep_get_wakeup_cause();
  switch(r){ case ESP_SLEEP_WAKEUP_TIMER : DEBUG_PRINTLN(F("Wake: Timer")); break;
  default : DEBUG_PRINT(F("Wake: Other (")); DEBUG_PRINT(r); DEBUG_PRINTLN(F(")"));break;}
}
void print_hex2(unsigned v) { v &= 0xff; if (v < 16) Serial.print('0'); Serial.print(v, HEX);}

// Required for MCCI LMIC on ESP32
extern "C" uint32_t arduino_ticks() { return millis(); }
