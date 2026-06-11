// tools/p2p_tx_smoke - canned P2P frame transmitter for receiver bring-up.
//
// Purpose: prove the Heltec receiver decodes the wire format over a real RF
// link, BEFORE refactoring the sensor firmware. Sends a fake-but-plausible
// reading (wood MC, wood temp, battery, temp-fallback flag) every 5 s.
//
// Runs on the TTGO T-Beam. The LoRa rail on the T-Beam is gated by the PMIC
// (ALDO2 on v1.2 / LDO2 on v1.1), so we do a minimal PMIC init first or the
// SX1262 never answers (CHIP_NOT_FOUND). This mirrors setup_axp() in the
// sensor firmware, trimmed to just powering the radio.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <XPowersAXP192.tpp>
#include <XPowersAXP2101.tpp>

#include "p2p_frame.h"

// T-Beam SX1262 pins (from the sensor firmware's include/config.h)
#define LORA_SCK   5
#define LORA_MISO  19
#define LORA_MOSI  27
#define LORA_CS    18
#define LORA_RST   23
#define LORA_DIO1  33
#define LORA_BUSY  32

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
uint32_t counter = 0;

static void power_lora_rail() {
    Wire.begin(21, 22);

    XPowersAXP2101 *axp2101 = new XPowersAXP2101();
    if (axp2101->init(Wire, 21, 22, AXP2101_SLAVE_ADDRESS)) {
        axp2101->enableALDO2();   // LoRa radio power (T-Beam v1.2)
        Serial.println("[smoke] PMIC AXP2101: LoRa rail on");
        return;
    }
    delete axp2101;

    XPowersAXP192 *axp192 = new XPowersAXP192();
    if (axp192->init(Wire, 21, 22, AXP192_SLAVE_ADDRESS)) {
        axp192->enableLDO2();     // LoRa radio power (T-Beam v1.1)
        Serial.println("[smoke] PMIC AXP192: LoRa rail on");
        return;
    }
    delete axp192;
    Serial.println("[smoke] WARNING: no PMIC found, radio may be unpowered");
}

// Append one Cayenne LPP entry; returns new write index.
static uint8_t put_analog(uint8_t *b, uint8_t i, uint8_t ch, float v) {
    int16_t raw = (int16_t)lroundf(v * 100.0f);
    b[i++] = ch; b[i++] = LPP_TYPE_ANALOG_INPUT;
    b[i++] = (uint8_t)(raw >> 8); b[i++] = (uint8_t)(raw & 0xFF);
    return i;
}
static uint8_t put_temp(uint8_t *b, uint8_t i, uint8_t ch, float v) {
    int16_t raw = (int16_t)lroundf(v * 10.0f);
    b[i++] = ch; b[i++] = LPP_TYPE_TEMPERATURE;
    b[i++] = (uint8_t)(raw >> 8); b[i++] = (uint8_t)(raw & 0xFF);
    return i;
}
static uint8_t put_digital(uint8_t *b, uint8_t i, uint8_t ch, uint8_t v) {
    b[i++] = ch; b[i++] = LPP_TYPE_DIGITAL_INPUT; b[i++] = v;
    return i;
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000);
    Serial.println("\n[smoke] P2P TX smoke test");

    power_lora_rail();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int st = radio.begin(P2P_FREQUENCY_MHZ, P2P_BANDWIDTH_KHZ, P2P_SPREADING_FACTOR,
                         P2P_CODING_RATE, P2P_SYNC_WORD, P2P_TX_POWER_DBM,
                         P2P_PREAMBLE_LENGTH);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[smoke] SX1262 init FAILED, code %d - halting\n", st);
        while (true) delay(1000);
    }
    Serial.printf("[smoke] radio OK on %.3f MHz - transmitting every 5 s\n", P2P_FREQUENCY_MHZ);
}

void loop() {
    uint8_t lpp[P2P_MAX_LPP];
    uint8_t n = 0;
    float mc = 12.0f + (counter % 10) * 0.5f;   // sweeps 12.0 .. 16.5 %
    n = put_analog (lpp, n, LPP_CHANNEL_WOOD_MC,          mc);
    n = put_temp   (lpp, n, LPP_CHANNEL_WOOD_TEMP,        21.5f);
    n = put_analog (lpp, n, LPP_CHANNEL_BATTERY_VOLTAGE,  3.95f);
    n = put_digital(lpp, n, LPP_CHANNEL_TEMP_FALLBACK,    0);

    uint8_t frame[P2P_HEADER_LEN + P2P_MAX_LPP];
    frame[0] = P2P_MAGIC_0;
    frame[1] = P2P_MAGIC_1;
    frame[2] = P2P_PROTO_VERSION;
    frame[3] = 1;            // node id
    frame[4] = n;
    memcpy(frame + P2P_HEADER_LEN, lpp, n);

    int st = radio.transmit(frame, P2P_HEADER_LEN + n);
    Serial.printf("[smoke] tx #%lu (%u bytes, mc=%.1f) -> %d\n",
                  (unsigned long)counter++, P2P_HEADER_LEN + n, mc, st);
    delay(5000);
}
