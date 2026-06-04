// src/ttn_test.cpp - Minimal LoRaWAN network connectivity test (TTN)
//
// Validates the network configuration end to end, independent of the
// measurement firmware: PMIC + radio init, OTAA join, one fixed LPP
// uplink per minute, downlink printout. No sensors, no deep sleep -
// the device stays awake on USB so the serial log is continuous.
//
// Build/flash:  pio run -e ttn-test --target upload
// Restore real firmware afterwards:  pio run --target upload
//
// Self-diagnosing hardware bring-up (found during 2026-06-04 bench test):
//   - PMIC: tries AXP192 (T-Beam v1.1) then AXP2101 (v1.2)
//   - Radio: tries SX1262 then SX1278 (both shipped on 433 MHz T-Beams)
//   - LoRa pins come from config.h, corrected 2026-06-04 to the real T-Beam
//     wiring after this test exposed the wrong pin map
//
// Reuses session_manager.h nonce persistence so DevNonce stays monotonic
// across test flashes and the network server keeps accepting joins.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <CayenneLPP.h>
#include <XPowersAXP192.tpp>
#include <XPowersAXP2101.tpp>

#include "config.h"
#include "session_manager.h"

static const uint32_t TEST_SEND_INTERVAL_MS = 60UL * 1000UL;
static const uint8_t  TEST_LPP_CHANNEL      = 99;   // clearly not a real sensor
static const float    TEST_LPP_VALUE        = 42.0f;

// LoRa wiring comes from config.h (corrected to the real T-Beam pin map).
// SX127x boards have no BUSY line; DIO0 sits on GPIO 26 instead.
#define TB_SX127X_DIO0 26
#define TB_SX127X_DIO1 33

SX1262 radio62 = new Module(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1278 radio78 = new Module(LORA_CS_PIN, TB_SX127X_DIO0, LORA_RST_PIN, TB_SX127X_DIO1);
LoRaWANNode* node = nullptr;

XPowersAXP192 PMU192;
XPowersAXP2101 PMU2101;
CayenneLPP lpp(32);

static uint32_t uplink_count = 0;
static bool joined = false;

// Identify the PMIC (AXP192 on T-Beam v1.1, AXP2101 on v1.2) and power the
// LoRa rail. Prints an I2C scan if neither chip answers.
static void pmic_init_and_power_lora() {
    if (PMU192.init(Wire, 21, 22, AXP192_SLAVE_ADDRESS)) {
        PMU192.enableLDO2();   // LoRa radio power (T-Beam v1.1)
        PMU192.disableLDO3();  // GPS off
        Serial.println(F("[Test] PMIC: AXP192 (T-Beam v1.1) - LoRa powered (LDO2)"));
        return;
    }
    if (PMU2101.init(Wire, 21, 22, AXP2101_SLAVE_ADDRESS)) {
        PMU2101.enableALDO2();   // LoRa radio power (T-Beam v1.2)
        PMU2101.disableALDO3();  // GPS off
        Serial.println(F("[Test] PMIC: AXP2101 (T-Beam v1.2) - LoRa powered (ALDO2)"));
        return;
    }
    Serial.println(F("[Test] WARNING: no AXP192/AXP2101 found - I2C scan:"));
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print(F("[Test]   device at 0x"));
            if (addr < 16) Serial.print('0');
            Serial.println(addr, HEX);
        }
    }
}

// Probe SX1262 first, then SX1278. Returns the working radio or nullptr.
static PhysicalLayer* radio_detect() {
    Serial.println(F("[Test] Probing SX1262..."));
    int state = radio62.begin();
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("[Test] Radio: SX1262 found"));
        return &radio62;
    }
    Serial.print(F("[Test] SX1262 not responding (code "));
    Serial.print(state);
    Serial.println(F("), probing SX1278..."));
    state = radio78.begin(434.0);
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("[Test] Radio: SX1278 found"));
        return &radio78;
    }
    Serial.print(F("[Test] SX1278 not responding either (code "));
    Serial.print(state);
    Serial.println(F(")"));
    return nullptr;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 2000);
    Serial.println(F("\n========================================"));
    Serial.println(F(" TTN CONNECTIVITY TEST (no sensors)"));
    Serial.println(F("========================================"));

    // --- PMIC: the radio rail comes from the PMIC, must be enabled first ---
    pmic_init_and_power_lora();
    delay(100);  // let the LoRa rail stabilize

    // --- SPI on the actual T-Beam LoRa pins (NOT the ESP32 defaults) ---
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_CS_PIN);

    // --- Radio ---
    PhysicalLayer* phy = radio_detect();
    if (phy == nullptr) {
        Serial.println(F("[Test] Halting. No radio found - check power/wiring."));
        while (true) delay(1000);
    }
    node = new LoRaWANNode(phy, &EU433);

    // --- Nonces: restore from NVS so DevNonce stays monotonic ---
    uint8_t noncesTemp[RADIOLIB_LORAWAN_NONCES_BUF_SIZE] = {0};
    if (session_load_nonces(noncesTemp, sizeof(noncesTemp)) > 0) {
        node->setBufferNonces(noncesTemp);
    }

    // --- OTAA join (fresh - this is exactly what we want to test) ---
    Serial.println(F("[Test] Joining via OTAA (EU433)..."));
    node->beginOTAA(joinEUI, devEUI, nwkKey, appKey);
    int state = node->activateOTAA();

    if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
        state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        joined = true;
        Serial.println(state == RADIOLIB_LORAWAN_NEW_SESSION
                       ? F("[Test] JOIN OK - new session")
                       : F("[Test] JOIN OK - session restored"));
        uint8_t* noncesPtr = node->getBufferNonces();
        if (noncesPtr != nullptr) {
            session_save_nonces(noncesPtr, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
        }
    } else {
        Serial.print(F("[Test] JOIN FAILED, code: "));
        Serial.println(state);
        Serial.println(F("[Test] Check on TTN: device registered? keys match (MSB)?"));
        Serial.println(F("[Test] Check gateway: online? EU433 frequency plan?"));
        Serial.println(F("[Test] Retrying every 60 s..."));
    }
}

void loop() {
    if (!joined) {
        delay(TEST_SEND_INTERVAL_MS);
        Serial.println(F("[Test] Retrying OTAA join..."));
        int state = node->activateOTAA();
        if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
            joined = true;
            Serial.println(F("[Test] JOIN OK - new session"));
            uint8_t* noncesPtr = node->getBufferNonces();
            if (noncesPtr != nullptr) {
                session_save_nonces(noncesPtr, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
            }
        } else {
            Serial.print(F("[Test] JOIN FAILED, code: "));
            Serial.println(state);
        }
        return;
    }

    // --- Uplink: fixed LPP payload, decodes as analog_in_99 = 42 on TTN ---
    lpp.reset();
    lpp.addAnalogInput(TEST_LPP_CHANNEL, TEST_LPP_VALUE);

    uint8_t downlinkPayload[256];
    size_t downlinkLen = sizeof(downlinkPayload);

    uplink_count++;
    Serial.print(F("[Test] Sending uplink #"));
    Serial.print(uplink_count);
    Serial.print(F(" (FCntUp "));
    Serial.print((uint32_t)node->getFCntUp());
    Serial.println(F(")..."));

    int txResult = node->sendReceive(lpp.getBuffer(), lpp.getSize(), 1,
                                     downlinkPayload, &downlinkLen);

    if (txResult >= 0) {
        Serial.println(F("[Test] TX OK - check the TTN console live data"));
        if (txResult > 0 && downlinkLen > 0) {
            Serial.print(F("[Test] Downlink received on fPort "));
            Serial.print(txResult);
            Serial.print(F(": "));
            for (size_t i = 0; i < downlinkLen; i++) {
                if (downlinkPayload[i] < 16) Serial.print('0');
                Serial.print(downlinkPayload[i], HEX);
                Serial.print(' ');
            }
            Serial.println();
        }
    } else {
        Serial.print(F("[Test] TX FAILED, code: "));
        Serial.println(txResult);
    }

    delay(TEST_SEND_INTERVAL_MS);
}
