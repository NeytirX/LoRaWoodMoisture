// src/main.cpp - Main Firmware

#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include "config.h"
#include <CayenneLPP.h>

#ifdef USE_AXP_POWER_MANAGEMENT
#include <XPowersLib.h>
XPowersLibInterface *PMU = NULL;
#endif

// --- FORWARD DECLARATIONS ---
void do_send(osjob_t *j);
void printHex2(unsigned v);
void deep_sleep_with_timer(uint32_t seconds);
void setup_lora_pins();
void setup_axp();
void power_down_peripheral(uint8_t peripheral_mask, const char* name);
power_save_level_t disable_all_irrelevant_peripherals(void);
void restore_all_irrelevant_peripherals(power_save_level_t level);

// --- STATE MACHINE ---
typedef enum {
    STATE_INIT,
    STATE_PMIC_SETUP,
    STATE_LORA_INIT,
    STATE_JOIN_LORAWAN,
    STATE_IDLE, // Waiting for next measurement interval
    STATE_POWER_UP_SENSOR,
    STATE_READ_SENSOR,
    STATE_PREPARE_TX_DATA,
    STATE_TRANSMIT_DATA,
    STATE_WAIT_FOR_TX_COMPLETE,
    STATE_POWER_DOWN_SENSOR,
    STATE_ENTER_SLEEP,
    STATE_ERROR
} device_state_t;

RTC_DATA_ATTR device_state_t currentState = STATE_INIT;
RTC_DATA_ATTR bool lorawan_joined = false;
RTC_DATA_ATTR uint32_t current_interval_seconds = NORMAL_INTERVAL_SECONDS;
RTC_DATA_ATTR uint8_t join_retry_count = 0;
RTC_DATA_ATTR uint8_t tx_retry_count = 0;

// --- LORAWAN & LMIC ---
// LMIC job scheduling
osjob_t sendjob;

// LoRaWAN Pin mapping for MCCI LMIC and T-Beam SX1262
// This is provided as a struct, and then lmic_pinmap is set to its address.
const lmic_pinmap lmic_pins = {
    .nss = LORA_CS_PIN, // NSS (SPI Chip Select)
    .rxtx = LMIC_UNUSED_PIN, // For SX1276, set to 0 for auto, 1 for active high, 2 for active low. Not used for SX126x.
    .rst = LORA_RST_PIN, // Reset
    .dio = {LORA_DIO1_PIN, LORA_DIO1_PIN, LMIC_UNUSED_PIN}, // DIO0, DIO1, DIO2. For SX126x, DIO1 is used for IRQ.
                                                        // Some SX126x boards might use DIO2 or DIO3 for specific events like TX_DONE, RX_DONE if configured.
                                                        // MCCI LMIC with SX1262 primarily relies on DIO1.
    .rxtx_rx_active = 0, // Not used for SX126x
    .rssi_cal = 10,      // LBT channel calibration factor, adjust if needed
    .spi_freq = 8000000, // SPI frequency, 8MHz is common for SX126x.
                        // Check SX1262 datasheet for max SPI speed.
    // Radio specific for SX126X
    .busy = LORA_BUSY_PIN, // BUSY pin for SX1262
    .tcxo_vcc_pin = LMIC_UNUSED_PIN, // If TCXO voltage is controlled by a GPIO
    .board_power_pin = LMIC_UNUSED_PIN // If overall radio board power is controlled by a GPIO
};

// Buffer for CayenneLPP payload
CayenneLPP lpp(51); // Max payload size for LoRaWAN is region-dependent, 51 is generally safe

// --- SENSOR DATA ---
RTC_DATA_ATTR float last_soil_moisture_percent = -1.0;
RTC_DATA_ATTR float last_battery_voltage = -1.0;

// --- FUNCTION PROTOTYPES (from LMIC library) ---
// These are callbacks LMIC needs. User code needs to provide them.
// Provide DEVEUI, APPEUI, and APPKEY for OTAA
// These are defined in config.h
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

// --- SETUP FUNCTION ---
void setup() {
    // ESP32 specific: Disable core 0 WDT if it causes issues during long LMIC operations or deep sleep.
    // For robust applications, it'''s better to feed the WDT.
    // disableCore0WDT(); // If needed, test thoroughly

    // Initialize Serial for debugging
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 2000); // Wait for serial, but not indefinitely

    DEBUG_PRINTLN(F("Starting ESP32 Soil Moisture Sensor..."));
    DEBUG_PRINT(F("Current State: ")); DEBUG_PRINTLN(currentState);
    DEBUG_PRINT(F("Wakeup reason: "));
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0 : DEBUG_PRINTLN(F("External signal using RTC_IO")); break;
        case ESP_SLEEP_WAKEUP_EXT1 : DEBUG_PRINTLN(F("External signal using RTC_CNTL")); break;
        case ESP_SLEEP_WAKEUP_TIMER : DEBUG_PRINTLN(F("Timer")); break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD : DEBUG_PRINTLN(F("Touchpad")); break;
        case ESP_SLEEP_WAKEUP_ULP : DEBUG_PRINTLN(F("ULP program")); break;
        default : DEBUG_PRINT(F("Other (")); DEBUG_PRINT(wakeup_reason); DEBUG_PRINTLN(F(")"));break;
    }

    if (currentState == STATE_INIT || wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED || wakeup_reason == 0) { // ESP_SLEEP_WAKEUP_UNDEFINED is 0, first boot
        DEBUG_PRINTLN(F("Device cold boot or reset. Initializing..."));
        lorawan_joined = false;
        join_retry_count = 0;
        tx_retry_count = 0;
        current_interval_seconds = NORMAL_INTERVAL_SECONDS;
        last_soil_moisture_percent = -1.0; // Indicate no valid reading yet
        last_battery_voltage = -1.0;
        currentState = STATE_PMIC_SETUP;
    } else {
        DEBUG_PRINTLN(F("Woke from sleep. Continuing state machine."));
        // State is already restored from RTC memory
    }

    // Setup sensor power pin
    pinMode(SOIL_MOISTURE_POWER_PIN, OUTPUT);
    digitalWrite(SOIL_MOISTURE_POWER_PIN, LOW); // Ensure sensor is off initially

    // ADC Configuration for soil moisture sensor
    // ESP32 ADC can be noisy. Consider using adc_power_acquire() and release() for better readings if needed.
    // For this example, a simple analogRead is used.
    // You might want to set ADC attenuation for full 0-3.3V range if your sensor outputs that.
    // e.g. analogSetCycles(32); analogSetSamples(1); analogSetClockDiv(1);
    // analogSetPinAttenuation(SOIL_MOISTURE_ADC_PIN, ADC_11db); // For 0-3.3V range
    // Default is ADC_0db (0-1.1V approximately). Check ESP32 ADC docs for your specific setup.

    // Print LoRaWAN keys (first few bytes for verification, not full keys for security)
    DEBUG_PRINT(F("DevEUI: ")); for(int i=0; i<2; ++i) { printHex2(DEVEUI[i]); DEBUG_PRINT(F(" "));} DEBUG_PRINTLN(F("..."));
    DEBUG_PRINT(F("AppEUI: ")); for(int i=0; i<2; ++i) { printHex2(APPEUI[i]); DEBUG_PRINT(F(" "));} DEBUG_PRINTLN(F("..."));
    DEBUG_PRINT(F("AppKey: ")); for(int i=0; i<2; ++i) { printHex2(APPKEY[i]); DEBUG_PRINT(F(" "));} DEBUG_PRINTLN(F("..."));

    // The state machine will handle PMIC and LoRa init from loop()
}

// --- LOOP FUNCTION (STATE MACHINE) ---
void loop() {
    switch (currentState) {
        case STATE_INIT:
            // This state should ideally be handled fully in setup after a cold boot.
            // If we somehow re-enter STATE_INIT, re-initialize and move to PMIC setup.
            DEBUG_PRINTLN(F("[State] INIT: Re-initializing critical variables."));
            lorawan_joined = false;
            join_retry_count = 0;
            tx_retry_count = 0;
            current_interval_seconds = NORMAL_INTERVAL_SECONDS;
            currentState = STATE_PMIC_SETUP;
            break;

        case STATE_PMIC_SETUP:
            DEBUG_PRINTLN(F("[State] PMIC_SETUP"));
            setup_axp(); // Initialize AXP Power Management
            currentState = STATE_LORA_INIT;
            break;

        case STATE_LORA_INIT:
            DEBUG_PRINTLN(F("[State] LORA_INIT"));
            #ifdef USE_AXP_POWER_MANAGEMENT
            if (PMU) {
                // Example: Enable LoRa power rail if controlled by AXP
                // This depends on your T-Beam version and AXP configuration.
                // For many T-Beams, LoRa power (LDO3 or similar) is enabled by default
                // or tied to ESP32'''s 3.3V rail. Check your schematic.
                // PMU->enableControl(XPOWERS_LDO3); // Or whichever rail powers LoRa
                DEBUG_PRINTLN(F("PMIC: Ensuring LoRa power rail is on (if applicable)."));
            }
            #endif
            setup_lora_pins(); // Configure GPIOs for LoRa module
            os_init();         // Initialize LMIC runtime environment
            LMIC_reset();      // Reset LoRaWAN state

            // Set LoRaWAN region. LMIC_setupChannel is used for US/AU style bands.
            // For EU433 (or EU868), LMIC configures channels based on compile-time settings.
            // The CFG_eu433 flag in platformio.ini and lmic_project_config.h handles this.
            #if defined(CFG_eu433)
                DEBUG_PRINTLN(F("LMIC: Configuring for EU433."));
                // LMIC_setupChannel(0, 433175000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI); // Example for a specific channel
                // For EU433, often a set of default channels are enabled by the library core based on region definition.
                // Make sure lmic_project_config.h and platformio.ini define CFG_eu433.
            #elif defined(CFG_us915)
                DEBUG_PRINTLN(F("LMIC: Configuring for US915."));
                LMIC_selectSubBand(1); // Example: select sub-band 1 for US915 (0-7)
            #elif defined(CFG_au915)
                DEBUG_PRINTLN(F("LMIC: Configuring for AU915."));
                LMIC_selectSubBand(1); // Example: select sub-band 1 for AU915 (0-7)
            #elif defined(CFG_eu868)
                DEBUG_PRINTLN(F("LMIC: Configuring for EU868."));
                // Default channels usually fine for EU868
            #else
                #warning "LoRaWAN region not explicitly configured in main.cpp, relying on LMIC defaults / platformio.ini"
            #endif

            // Set data rate and transmit power (optional, LMIC defaults are usually fine)
            // LMIC_setDrTxpow(DR_SF7, 14); // Example: SF7, 14 dBm

            // Set clock error for better LoRaWAN timing, especially if using a TCXO
            // LMIC_setClockError(MAX_CLOCK_ERROR_M_PPM * 1 / 100); // 1% for crystal
            // LMIC_setClockError(MAX_CLOCK_ERROR_M_PPM * 0.02 / 100); // 0.02% for a good TCXO (20ppm)
            LMIC_setClockError(20); // MCCI LMIC expects PPM * 1000000 / 2^20, so 20ppm is approx 20. 
                                    // Or use MAX_CLOCK_ERROR_PPM for older LMIC.
                                    // Or better: LMIC_setClockError(MAX_CLOCK_ERROR_M_PPM * TCXO_ACCURACY_PPM / 100.0) 
                                    // For T-BEAM SX1262, TCXO is common. Let'''s assume 5ppm for TCXO.
            LMIC_setClockError(MAX_CLOCK_ERROR_M_PPM * 5 / 100); 

            if (lorawan_joined) {
                LMIC_setSession (0x1, LMIC.devaddr, (u1_t*)LMIC.nwkKey, (u1_t*)LMIC.artKey);
                DEBUG_PRINTLN(F("LMIC: Session restored. Device Address: "));
                printHex2(LMIC.devaddr >> 24);
                printHex2(LMIC.devaddr >> 16);
                printHex2(LMIC.devaddr >> 8);
                printHex2(LMIC.devaddr & 0xFF);
                Serial.println();
                currentState = STATE_IDLE; // Proceed to idle if already joined
            } else {
                currentState = STATE_JOIN_LORAWAN;
            }
            break;

        case STATE_JOIN_LORAWAN:
            DEBUG_PRINTLN(F("[State] JOIN_LORAWAN"));
            if (join_retry_count < LORAWAN_JOIN_MAX_RETRIES) {
                DEBUG_PRINT(F("Attempting LoRaWAN join (Attempt: "));
                DEBUG_PRINT(join_retry_count + 1); DEBUG_PRINTLN(F(")"));
                LMIC_startJoining();
                // The EV_JOINED or EV_JOIN_FAILED event will update state in onEvent()
                // For now, we stay in this state, os_runloop will handle LMIC events.
            } else {
                DEBUG_PRINTLN(F("Max join retries reached. Sleeping before next attempt cycle."));
                currentState = STATE_ENTER_SLEEP; // Sleep for a longer duration
                current_interval_seconds = LORAWAN_JOIN_RETRY_SLEEP_SECONDS * 5; // Longer sleep if all retries fail
            }
            break;

        case STATE_IDLE:
            DEBUG_PRINTLN(F("[State] IDLE"));
            // This state is effectively a wait before the next sensor reading.
            // The actual sleep will happen in STATE_ENTER_SLEEP.
            // We transition directly to powering up the sensor if interval has passed (handled by deep sleep timer).
            currentState = STATE_POWER_UP_SENSOR;
            break;

        case STATE_POWER_UP_SENSOR:
            DEBUG_PRINTLN(F("[State] POWER_UP_SENSOR"));
            digitalWrite(SOIL_MOISTURE_POWER_PIN, HIGH);
            delay(ADC_READ_STABILIZATION_MS); // Allow sensor to stabilize
            currentState = STATE_READ_SENSOR;
            break;

        case STATE_READ_SENSOR:
            DEBUG_PRINTLN(F("[State] READ_SENSOR"));
            {
                uint32_t adc_raw = analogRead(SOIL_MOISTURE_ADC_PIN);
                DEBUG_PRINT(F("Raw ADC value: ")); DEBUG_PRINTLN(adc_raw);

                // Map ADC raw value to percentage
                // Ensure ADC_RAW_WET is not equal to ADC_RAW_DRY to avoid division by zero.
                if (ADC_RAW_WET == ADC_RAW_DRY) {
                    DEBUG_PRINTLN(F("ERROR: ADC_RAW_WET and ADC_RAW_DRY are the same. Cannot calculate percentage."));
                    last_soil_moisture_percent = -1; // Indicate error
                } else {
                    // Ensure ADC values are properly clamped for mapping.
                    // Soil moisture is typically inverse to ADC reading (wetter = lower resistance = higher ADC value if pull-up is on ADC side)
                    // OR (wetter = lower resistance = lower ADC value if sensor forms voltage divider to GND and ADC reads midpoint)
                    // Assuming sensor to GND, and GPIO powers a pull-up resistor to 3.3V, and ADC reads the junction.
                    // So, WET = higher voltage/ADC reading, DRY = lower voltage/ADC reading.
                    // If your sensor is opposite (e.g. resistive sensor to VCC, and ADC measures voltage drop over a fixed resistor to GND)
                    // then swap ADC_RAW_DRY and ADC_RAW_WET in the map function.
                    // The provided config expects DRY = 0, WET = 4095 (typical for ESP32 12-bit ADC).
                    long mapped_value = map(adc_raw, ADC_RAW_DRY, ADC_RAW_WET, 0, 100);
                    last_soil_moisture_percent = constrain(mapped_value, 0, 100); // Constrain to 0-100%
                }
                DEBUG_PRINT(F("Soil Moisture: ")); DEBUG_PRINT(last_soil_moisture_percent); DEBUG_PRINTLN(F(" %"));

                // Interval is now fixed to NORMAL_INTERVAL_SECONDS by default.
                // ALERT_INTERVAL_SECONDS can be set via other means (e.g. downlink) if implemented later.
                current_interval_seconds = NORMAL_INTERVAL_SECONDS;
                DEBUG_PRINT(F("Using interval: ")); DEBUG_PRINT(current_interval_seconds); DEBUG_PRINTLN(F(" seconds."));
            }
            currentState = STATE_PREPARE_TX_DATA;
            break;

        case STATE_PREPARE_TX_DATA:
            DEBUG_PRINTLN(F("[State] PREPARE_TX_DATA"));
            lpp.reset();
            if (last_soil_moisture_percent != -1) {
                lpp.addAnalogInput(LPP_CHANNEL_MOISTURE, last_soil_moisture_percent); // Cayenne LPP expects float for analog input
            }

            #ifdef USE_AXP_POWER_MANAGEMENT
            if (PMU) {
                // It'''s good practice to check if PMU init was successful
                last_battery_voltage = PMU->getBattVoltage() / 1000.0f; // Voltage in mV, convert to V
                if (last_battery_voltage > 0) { // Basic check for valid reading
                    lpp.addAnalogInput(LPP_CHANNEL_BATTERY_VOLTAGE, last_battery_voltage);
                    DEBUG_PRINT(F("Battery Voltage: ")); DEBUG_PRINT(last_battery_voltage); DEBUG_PRINTLN(F(" V"));
                }
            }
            #endif
            // Optionally add RSSI and SNR if available and meaningful before TX
            // lpp.addAnalogInput(LPP_CHANNEL_RSSI, LMIC.rssi);
            // lpp.addAnalogInput(LPP_CHANNEL_SNR, LMIC.snr / 4.0); // SNR is reported as value * 4

            if (lpp.getSize() == 0) {
                DEBUG_PRINTLN(F("No data to send. Skipping transmission."));
                currentState = STATE_POWER_DOWN_SENSOR;
            } else {
                currentState = STATE_TRANSMIT_DATA;
            }
            break;

        case STATE_TRANSMIT_DATA:
            DEBUG_PRINTLN(F("[State] TRANSMIT_DATA"));
            if (LMIC.opmode & OP_TXRXPEND) {
                DEBUG_PRINTLN(F("LoRaWAN busy (OP_TXRXPEND). Waiting..."));
                // Stay in this state, os_runloop will eventually clear OP_TXRXPEND
            } else if (!lorawan_joined) {
                DEBUG_PRINTLN(F("Not joined to LoRaWAN. Cannot transmit. Returning to JOIN state."));
                currentState = STATE_LORA_INIT; // Re-initialize and try joining again
                join_retry_count = 0; // Reset join retries for a fresh attempt cycle
            } else {
                // Prepare upstream data transmission at the next possible time.
                DEBUG_PRINT(F("Preparing to send LoRaWAN packet. Size: ")); DEBUG_PRINTLN(lpp.getSize());
                // LMIC_setTxData2(port, data, dataLen, confirmed)
                // Port 1-223. Confirmed (1) or unconfirmed (0) message.
                // For sensor data, unconfirmed is usually preferred for battery life and network load.
                LMIC_setTxData2(1, lpp.getBuffer(), lpp.getSize(), 0); // Port 1, unconfirmed
                DEBUG_PRINTLN(F("Packet queued for transmission."));
                currentState = STATE_WAIT_FOR_TX_COMPLETE;
                tx_retry_count = 0; // Reset tx retry for this new packet
            }
            break;

        case STATE_WAIT_FOR_TX_COMPLETE:
            // DEBUG_PRINTLN(F("[State] WAIT_FOR_TX_COMPLETE"));
            // LMIC handles transmission in the background via os_runloop().
            // The onEvent() callback (specifically EV_TXCOMPLETE) will change the state.
            // If TX fails multiple times (handled in onEvent), we might re-try or go to sleep.
            // Add a timeout here? If LMIC gets stuck, this state might persist.
            // For now, relying on EV_TXCOMPLETE.
            break; 

        case STATE_POWER_DOWN_SENSOR:
            DEBUG_PRINTLN(F("[State] POWER_DOWN_SENSOR"));
            digitalWrite(SOIL_MOISTURE_POWER_PIN, LOW);
            currentState = STATE_ENTER_SLEEP;
            break;

        case STATE_ENTER_SLEEP:
            DEBUG_PRINTLN(F("[State] ENTER_SLEEP"));
            DEBUG_PRINT(F("Entering deep sleep for ")); DEBUG_PRINT(current_interval_seconds); DEBUG_PRINTLN(F(" seconds."));
            
            #ifdef USE_AXP_POWER_MANAGEMENT
            if (PMU) {
                // Example: Power down LoRa module via PMIC if controlled (LDO3 or other rail)
                // This is board specific. Some PMIC configurations might cut power automatically or require explicit command.
                // PMU->disableControl(XPOWERS_LDO3); // If LDO3 powers LoRa
                // PMU->setPowerOutPin(XPOWERS_DCDC1, XPOWERS_OFF); // Example if a GPIO like output is used for main system power stages
                DEBUG_PRINTLN(F("PMIC: Preparing for deep sleep (e.g. disabling specific rails if configured)."));
            }
            #endif

            // Ensure Serial buffer is flushed before sleep
            Serial.flush(); 

            deep_sleep_with_timer(current_interval_seconds);
            // Execution stops here until wake-up
            break;

        case STATE_ERROR:
            DEBUG_PRINTLN(F("[State] ERROR: An unrecoverable error occurred. Resetting."));
            // Potentially log error to persistent storage if available
            delay(5000); // Brief delay before reset
            ESP.restart();
            break;

        default:
            DEBUG_PRINTLN(F("Unknown state! Resetting state machine."));
            currentState = STATE_INIT;
            break;
    }

    // Let LMIC do its background processing for joining, transmission, and receiving downlinks.
    os_runloop_once();
}

// --- LORAWAN EVENT CALLBACK ---
void onEvent(ev_t ev) {
    DEBUG_PRINT(os_getTime());
    DEBUG_PRINT(F(": "));
    switch (ev) {
        case EV_SCAN_TIMEOUT:
            DEBUG_PRINTLN(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            DEBUG_PRINTLN(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            DEBUG_PRINTLN(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            DEBUG_PRINTLN(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            DEBUG_PRINTLN(F("EV_JOINING"));
            // currentState is already STATE_JOIN_LORAWAN
            break;
        case EV_JOINED:
            DEBUG_PRINTLN(F("EV_JOINED"));
            {
                u4_t netid = 0;
                devaddr_t devaddr = 0;
                u1_t nwkKey[16];
                u1_t artKey[16];
                LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
                DEBUG_PRINT(F("NetID: ")); DEBUG_PRINTLN(netid, DEC);
                DEBUG_PRINT(F("DevAddr: ")); DEBUG_PRINTLN(devaddr, HEX); 
                // Store session keys and DevAddr in RTC memory if needed for ABP resume after sleep
                // For OTAA, LMIC handles this internally if `LMIC_setSession` is used after wake-up.
            }
            // Disable link check validation (automatically enabled during join)
            // Consider if this is needed for your network/region.
            // LMIC_setLinkCheckMode(0);
            lorawan_joined = true;
            join_retry_count = 0; // Reset join retry counter on successful join
            // Transition to idle, then sensor reading will be triggered by timer
            currentState = STATE_IDLE; 
            DEBUG_PRINTLN(F("LoRaWAN Join successful."));
            break;
        case EV_RFU1: // Reserved for Future Use
            DEBUG_PRINTLN(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            DEBUG_PRINTLN(F("EV_JOIN_FAILED"));
            lorawan_joined = false;
            join_retry_count++;
            if (join_retry_count < LORAWAN_JOIN_MAX_RETRIES) {
                DEBUG_PRINTLN(F("Retrying join..."));
                // LMIC will retry based on its schedule, or we can force it.
                // For now, let os_runloop handle retries. If it doesn'''t, we might need to call LMIC_startJoining() again
                // or go to sleep and retry after wake up.
                // For simplicity, we will go to a short sleep and then retry the join state.
                current_interval_seconds = LORAWAN_JOIN_RETRY_SLEEP_SECONDS;
                currentState = STATE_ENTER_SLEEP; // Enter sleep, then try joining again via STATE_LORA_INIT
            } else {
                DEBUG_PRINTLN(F("Max join retries reached. Going to long sleep."));
                current_interval_seconds = LORAWAN_JOIN_RETRY_SLEEP_SECONDS * 10; // Longer sleep
                currentState = STATE_ENTER_SLEEP; // Will reset join_retry_count on next cycle if it goes through full init
            }
            break;
        case EV_REJOIN_FAILED:
            DEBUG_PRINTLN(F("EV_REJOIN_FAILED"));
            // Similar to JOIN_FAILED, perhaps try re-joining from scratch
            lorawan_joined = false;
            currentState = STATE_LORA_INIT; // Try a full re-init and join
            join_retry_count = 0;
            break;
        case EV_TXCOMPLETE:
            DEBUG_PRINTLN(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK) {
                DEBUG_PRINTLN(F("Received ACK"));
            }
            if (LMIC.dataLen) {
                DEBUG_PRINT(F("Received "));
                DEBUG_PRINT(LMIC.dataLen);
                DEBUG_PRINTLN(F(" bytes of payload (downlink)"));
                // Process downlink data if any
                // Example: for (int i = 0; i < LMIC.dataLen; i++) { Serial.print((char)LMIC.frame[LMIC.dataBeg + i], HEX); }
                // Serial.println();
            }
            tx_retry_count = 0; // Reset TX retry on successful send
            currentState = STATE_POWER_DOWN_SENSOR; // Proceed to power down sensor and sleep
            break;
        case EV_LOST_TSYNC:
            DEBUG_PRINTLN(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            DEBUG_PRINTLN(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            DEBUG_PRINTLN(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            DEBUG_PRINTLN(F("EV_LINK_DEAD"));
            lorawan_joined = false; // Assume connection is lost
            currentState = STATE_LORA_INIT; // Try to re-join
            join_retry_count = 0;
            break;
        case EV_LINK_ALIVE:
            DEBUG_PRINTLN(F("EV_LINK_ALIVE"));
            break;
        case EV_TXSTART:
            DEBUG_PRINTLN(F("EV_TXSTART"));
            break;
        case EV_TXCANCELED:
            DEBUG_PRINTLN(F("EV_TXCANCELED"));
            tx_retry_count++;
            if (tx_retry_count < LORAWAN_MAX_TX_RETRIES) {
                 DEBUG_PRINTLN(F("TX Canceled or failed, will retry transmission."));
                 currentState = STATE_TRANSMIT_DATA; // Retry sending the same data
            } else {
                DEBUG_PRINTLN(F("Max TX retries reached. Giving up on this packet."));
                currentState = STATE_POWER_DOWN_SENSOR; // Move to sleep
            }
            break;
        case EV_JOIN_TXCOMPLETE: // For some regions, JOIN_ACCEPT is not piggybacked on beacon
            DEBUG_PRINTLN(F("EV_JOIN_TXCOMPLETE: Join Request Sent."));
            // EV_JOINED or EV_JOIN_FAILED will follow
            break;
        case EV_SCAN_FOUND: // Only for pure FSK, not LoRa
            DEBUG_PRINTLN(F("EV_SCAN_FOUND"));
            break;
        default:
            DEBUG_PRINT(F("Unknown event: "));
            DEBUG_PRINTLN((unsigned)ev);
            break;
    }
}

// --- HELPER FUNCTIONS ---

void setup_axp() {
#ifdef USE_AXP_POWER_MANAGEMENT
    DEBUG_PRINTLN(F("Initializing AXP PMIC..."));
    PMU = new XPowersLib();
    if (!PMU) {
        DEBUG_PRINTLN(F("Failed to allocate XPowersLib object!"));
        return;
    }

    #if AXP_CHIP_TYPE == AXP_CHIP_AXP192
        int ret = PMU->init(Wire, 21, 22, AXP192_SLAVE_ADDRESS); // SDA, SCL, Address for AXP192
    #elif AXP_CHIP_TYPE == AXP_CHIP_AXP2101
        int ret = PMU->init(Wire, 21, 22, AXP2101_SLAVE_ADDRESS); // SDA, SCL, Address for AXP2101
    #else
        #error "Invalid AXP_CHIP_TYPE defined in config.h"
        int ret = -1; // Ensure ret is defined
    #endif

    if (ret == XPOWERS_SUCCESS) {
        DEBUG_PRINTLN(F("PMIC Initialized Successfully."));
        // Basic AXP192/AXP2101 setup for T-Beam
        // These settings are typical for T-Beams. Adjust if your board is different.
        PMU->setPowerOutPut(XPOWERS_LDO2, XPOWERS_ON); // LDO2 is often used for LoRa module power
        PMU->setPowerOutPut(XPOWERS_LDO3, XPOWERS_ON); // LDO3 is also sometimes used for LoRa or GPS
        PMU->setPowerOutPut(XPOWERS_DCDC1, XPOWERS_ON); // DCDC1 is often ESP32 VDD
        PMU->setPowerOutPut(XPOWERS_DCDC2, XPOWERS_ON); // Peripheral power
        PMU->setPowerOutPut(XPOWERS_DCDC3, XPOWERS_ON); // Peripheral power

        // Clear PMU IRQ unprocessed bits (important for waking from light sleep correctly)
        PMU->clearIrqStatus();

        // Set charging current and voltage (example values)
        PMU->setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2); // Or XPOWERS_AXP2101_CHG_VOL_4V2 etc.
        PMU->setChargeConstantCurrent(XPOWERS_AXP192_CHG_CUR_100MA); // Or another current setting

        DEBUG_PRINT(F("PMIC Battery voltage: ")); DEBUG_PRINT(PMU->getBattVoltage() / 1000.0f); DEBUG_PRINTLN(F("V"));
        DEBUG_PRINT(F("PMIC VIN voltage: ")); DEBUG_PRINT(PMU->getVinVoltage() / 1000.0f); DEBUG_PRINTLN(F("V"));

    } else {
        DEBUG_PRINT(F("PMIC Initialization Failed, error code: ")); DEBUG_PRINTLN(ret);
        delete PMU;
        PMU = NULL;
    }
#else
    DEBUG_PRINTLN(F("PMIC not used (USE_AXP_POWER_MANAGEMENT is false)."));
#endif
}

void setup_lora_pins() {
    DEBUG_PRINTLN(F("Setting up LoRa module pins..."));
    // SPI pins (MOSI, MISO, SCK) are typically configured by SPI.begin()
    // which is called by os_init() -> hal_init() in LMIC.
    // Ensure your board variant correctly defines these for the LoRa SPI bus (usually VSPI).

    // Explicitly set Reset and NSS pins as output
    pinMode(lmic_pins.nss, OUTPUT);
    digitalWrite(lmic_pins.nss, HIGH); // Deselect slave initially
    if (lmic_pins.rst != LMIC_UNUSED_PIN) {
        pinMode(lmic_pins.rst, OUTPUT);
        digitalWrite(lmic_pins.rst, HIGH);
        delay(10);
        digitalWrite(lmic_pins.rst, LOW);
        delay(10);
        digitalWrite(lmic_pins.rst, HIGH);
        delay(10);
        DEBUG_PRINTLN(F("LoRa Reset complete."));
    }

    // DIO pins are inputs, configured by LMIC HAL
    if (lmic_pins.dio[0] != LMIC_UNUSED_PIN) pinMode(lmic_pins.dio[0], INPUT);
    if (lmic_pins.dio[1] != LMIC_UNUSED_PIN) pinMode(lmic_pins.dio[1], INPUT);
    if (lmic_pins.dio[2] != LMIC_UNUSED_PIN) pinMode(lmic_pins.dio[2], INPUT);
    if (lmic_pins.busy != LMIC_UNUSED_PIN) pinMode(lmic_pins.busy, INPUT);

    DEBUG_PRINTLN(F("LoRa Pin setup complete."));
}

void deep_sleep_with_timer(uint32_t seconds) {
    DEBUG_PRINT(F("Configuring deep sleep for "));
    DEBUG_PRINT(seconds);
    DEBUG_PRINTLN(F(" seconds."));

    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL); // Time in microseconds

    // Optional: Enable wakeup on specific GPIO if needed (e.g., user button)
    // esp_sleep_enable_ext0_wakeup(GPIO_NUM_XX, 1); // 1 for high level, 0 for low

    // Optional: Reduce power consumption during deep sleep further
    // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF); // Power down RTC peripherals
    // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF); // Power down RTC slow memory (RTC_DATA_ATTR variables will be lost!)
    // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF); // Power down RTC fast memory
    // Use with caution. RTC_DATA_ATTR variables are stored in RTC_SLOW_MEM by default.

    DEBUG_PRINTLN(F("Going to sleep now."));
    Serial.flush(); // Ensure all serial output is sent

    #ifdef USE_AXP_POWER_MANAGEMENT
    if (PMU) {
        // Example: Set AXP to a low power mode or disable outputs before sleep
        // PMU->setSleep(); // May cut power to ESP32, requires PMU wake-up source
        // This needs careful handling based on how AXP is wired and configured to wake up.
        // For simple timer wakeup, ESP32 controls sleep. AXP settings focus on peripheral power.
    }
    #endif

    esp_deep_sleep_start();
    // Code should not reach here after esp_deep_sleep_start()
}

// Helper to print byte in HEX
void printHex2(unsigned v) {
    v &= 0xff;
    if (v < 16)
        Serial.print('0');
    Serial.print(v, HEX);
}

/* Placeholder for more advanced peripheral power management if needed */
/*
typedef enum {
    POWER_SAVE_LEVEL_NONE = 0,
    POWER_SAVE_LEVEL_CPU_LIGHT_SLEEP = 1, // ESP32 light sleep
    POWER_SAVE_LEVEL_PERIPHERALS_OFF = 2, // Manually power down specific peripherals
    POWER_SAVE_LEVEL_MODEM_SLEEP = 3 // ESP32 modem sleep (WiFi/BT off)
} power_save_level_t;

power_save_level_t disable_all_irrelevant_peripherals(void)
{
    // Example: turn off WiFi and Bluetooth
    // WiFi.mode(WIFI_OFF);
    // btStop();
    // adc_power_off(); // Deprecated, use adc_power_release()

#ifdef USE_AXP_POWER_MANAGEMENT
    if(PMU){
        // Selectively turn off power rails if not needed during specific operations
        // E.g., PMU->setPowerOutPut(XPOWERS_LDO_GPS, XPOWERS_OFF);
    }
#endif
    DEBUG_PRINTLN(F("Peripherals powered down (example)."));
    return POWER_SAVE_LEVEL_PERIPHERALS_OFF;
}

void restore_all_irrelevant_peripherals(power_save_level_t level)
{
    if (level >= POWER_SAVE_LEVEL_PERIPHERALS_OFF) {
        // Example: turn on components disabled earlier
        // adc_power_on(); // Deprecated, use adc_power_acquire()
#ifdef USE_AXP_POWER_MANAGEMENT
        if(PMU){
            // PMU->setPowerOutPut(XPOWERS_LDO_GPS, XPOWERS_ON);
        }
#endif
        DEBUG_PRINTLN(F("Peripherals restored (example)."));
    }
}
*/

// --- LMIC HAL Requirement for ESP32 ---
// MCCI LMIC requires this to be defined for ESP32 HAL
// It provides the basis for os_time and related timing functions.
extern "C" uint32_t arduino_ticks() {
    return millis();
}

// Required for MCCI LMIC on ESP32 platform to link against. 
// Provides a stub for non-existent function in the ESP32 Arduino core related to SPI device management.
// This might vary depending on the ESP32 core version and LMIC library version.
// If you encounter linking errors related to `spiDetachเพิ่มเติม`, this might be a workaround.
// extern "C" void spiDetachเพิ่มเติม(uint8_t){} // Using Thai character as a unique suffix to avoid collision
                                        // Update: Recent MCCI LMIC versions and ESP32 cores might not need this hack.
                                        // Remove if it causes compilation errors.

// Callback for LMIC radio transmission and reception.
// For SX126x, this is handled internally by LMIC if using the correct HAL configuration.
// For SX127x, you might need to implement radio_irq_handler.
// extern "C" void radio_irq_handler(uint8_t dio, uint32_t timestamp) {
//    LMIC_radio_irq_handler(dio, timestamp);
// }

// On ESP32, you might need to explicitly set the SPI pins if not using default VSPI
// or if there are conflicts. LMIC_set ರಲ್ಲಿspi_pins can be used if the HAL supports it.




