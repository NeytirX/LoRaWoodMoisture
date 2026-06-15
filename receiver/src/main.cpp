// receiver/src/main.cpp - Gateway-less LoRa receiver for the wood moisture sensor.
//
// Target: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262), 433 MHz variant.
//
// Listens on the fixed P2P channel (see p2p_frame.h), decodes the framed
// Cayenne LPP payload the T-Beam transmits in RADIO_P2P_MODE, and surfaces each
// reading two ways:
//   - JSON line over USB serial (always; zero setup)
//   - MQTT publish to a local broker (when WiFi creds are set in secrets.h)
//
// This is a continuous-receive program: unlike the sensor (which runs once in
// setup() then deep-sleeps), the receiver lives in loop().

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SSD1306Wire.h>

#include "p2p_frame.h"
#include "secrets.h"

// =============================================================================
// HELTEC WIFI LORA 32 V3 - SX1262 PIN MAP (fixed by the board)
// =============================================================================
#define HELTEC_LORA_NSS   8
#define HELTEC_LORA_SCK   9
#define HELTEC_LORA_MOSI  10
#define HELTEC_LORA_MISO  11
#define HELTEC_LORA_RST   12
#define HELTEC_LORA_BUSY  13
#define HELTEC_LORA_DIO1  14
// The V3 uses a TCXO (powered via DIO3) and DIO2 as the TX/RX RF switch.
#define HELTEC_TCXO_VOLTAGE 1.8f

// =============================================================================
// HELTEC WIFI LORA 32 V3 - BUILT-IN OLED (SSD1306 128x64, its own I2C bus)
// =============================================================================
#define HELTEC_OLED_SDA   17
#define HELTEC_OLED_SCL   18
#define HELTEC_OLED_RST   21
#define HELTEC_VEXT_PIN   36   // drive LOW to power the Vext rail that feeds the OLED

// =============================================================================
// GLOBALS
// =============================================================================
SX1262 radio = new Module(HELTEC_LORA_NSS, HELTEC_LORA_DIO1,
                          HELTEC_LORA_RST, HELTEC_LORA_BUSY);

SSD1306Wire display(0x3c, HELTEC_OLED_SDA, HELTEC_OLED_SCL);

// Latest reading mirrored to the OLED. Until the first packet decodes a wood_mc
// value, haveReading stays false and the screen shows the waiting message.
bool     haveReading  = false;
float    dispMc       = 0.0f;
float    dispTemp     = NAN;
int      dispRssi     = 0;
uint8_t  dispNode     = 0;
bool     dispFallback = false;
uint32_t lastReadingMs = 0;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
WiFiMulti    wifiMulti;
bool mqttEnabled = false;   // set in setup() iff at least one AP connected

volatile bool packetReady = false;
void IRAM_ATTR onPacketReceived() { packetReady = true; }

// =============================================================================
// OLED DISPLAY
// =============================================================================
static void display_init() {
    // The V3 powers the OLED through the Vext rail; pull GPIO36 LOW to enable it,
    // then pulse the panel's reset line before talking to it over I2C.
    pinMode(HELTEC_VEXT_PIN, OUTPUT);
    digitalWrite(HELTEC_VEXT_PIN, LOW);
    pinMode(HELTEC_OLED_RST, OUTPUT);
    digitalWrite(HELTEC_OLED_RST, LOW);
    delay(20);
    digitalWrite(HELTEC_OLED_RST, HIGH);
    delay(20);

    display.init();
    display.flipScreenVertically();   // panel is mounted upside-down on the V3
    display.setTextAlignment(TEXT_ALIGN_LEFT);
}

static void display_waiting() {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 14, "Waiting for");
    display.drawString(64, 34, "sensor...");
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.display();
}

// Draws the latest reading from the disp* globals. Called on each new packet and
// once a second from loop() so the "Ns ago" age keeps ticking.
static void display_reading() {
    display.clear();

    // Header line: node id (left) and signal strength (right).
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "Node " + String(dispNode));
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 0, String(dispRssi) + " dBm");

    // The number that matters: wood moisture content, large and centered.
    display.setFont(ArialMT_Plain_24);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 16, String(dispMc, 1) + " %");

    // Footer: wood temperature, temp-fallback marker, and reading age.
    display.setFont(ArialMT_Plain_10);
    String foot;
    if (!isnan(dispTemp)) foot += String(dispTemp, 1) + "C";
    if (dispFallback)     foot += " t?";
    foot += "  " + String((millis() - lastReadingMs) / 1000) + "s ago";
    display.drawString(64, 52, foot);

    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.display();
}

// =============================================================================
// LPP DECODE HELPERS
// =============================================================================
static int16_t read_be16(const uint8_t *p) {
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

// Friendly JSON key for a known channel; falls back to "ch<N>" for the rest.
static const char *channel_key(uint8_t channel) {
    switch (channel) {
        case LPP_CHANNEL_WOOD_MC:         return "wood_mc";
        case LPP_CHANNEL_WOOD_TEMP:       return "wood_temp_c";
        case LPP_CHANNEL_INDICATED_MC:    return "indicated_mc";
        case LPP_CHANNEL_RESISTANCE:      return "resistance_kohm";
        case LPP_CHANNEL_BATTERY_VOLTAGE: return "battery_v";
        case LPP_CHANNEL_ESP_TEMP:        return "esp_temp_c";
        case LPP_CHANNEL_TEMP_FALLBACK:   return "temp_fallback";
        default:                          return nullptr;
    }
}

// Walk a Cayenne LPP buffer and add each entry to the JSON doc. Handles only
// the types the sensor emits; bails on an unknown type because we cannot know
// its byte length to resync. Returns true if fully parsed.
static bool decode_lpp(const uint8_t *lpp, uint8_t len, JsonDocument &doc) {
    uint8_t i = 0;
    while (i + 2 <= len) {
        uint8_t channel = lpp[i++];
        uint8_t type    = lpp[i++];

        // Known channels map to program-lifetime string literals, which
        // ArduinoJson links without copying. An unknown channel's "chN" key
        // lives on this stack frame, so it is handed to ArduinoJson as a
        // mutable char[] (see store_*), which forces a copy into the document -
        // otherwise serializeJson() in handle_packet() would read freed stack.
        const char *known = channel_key(channel);
        char fallback_key[8];
        if (!known) snprintf(fallback_key, sizeof(fallback_key), "ch%u", channel);

        switch (type) {
            case LPP_TYPE_DIGITAL_INPUT:
                if (i + 1 > len) return false;
                if (known) doc[known] = lpp[i]; else doc[fallback_key] = lpp[i];
                i += 1;
                break;
            case LPP_TYPE_ANALOG_INPUT: {
                if (i + 2 > len) return false;
                float v = read_be16(&lpp[i]) / 100.0f;
                if (known) doc[known] = v; else doc[fallback_key] = v;
                i += 2;
                break;
            }
            case LPP_TYPE_TEMPERATURE: {
                if (i + 2 > len) return false;
                float v = read_be16(&lpp[i]) / 10.0f;
                if (known) doc[known] = v; else doc[fallback_key] = v;
                i += 2;
                break;
            }
            default:
                Serial.printf("[RX] unknown LPP type 0x%02X on ch %u, stopping decode\n",
                              type, channel);
                return false;
        }
    }
    return true;
}

// =============================================================================
// WIFI + MQTT
// =============================================================================
static void wifi_connect() {
    // Register every configured AP, then let WiFiMulti associate to the
    // strongest one currently in range. Add more pairs in secrets.h as needed.
    int configured = 0;
    if (strlen(WIFI_SSID)  > 0) { wifiMulti.addAP(WIFI_SSID,  WIFI_PASSWORD);  configured++; }
#ifdef WIFI_SSID2
    if (strlen(WIFI_SSID2) > 0) { wifiMulti.addAP(WIFI_SSID2, WIFI_PASSWORD2); configured++; }
#endif

    if (configured == 0) {
        Serial.println("[NET] no WiFi SSID set - serial-only mode (no MQTT)");
        return;
    }

    Serial.printf("[NET] WiFi: %d AP(s) configured, joining strongest in range", configured);
    WiFi.mode(WIFI_STA);
    uint32_t start = millis();
    while (wifiMulti.run() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[NET] WiFi up on \"%s\", IP %s\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        mqtt.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
        mqtt.setBufferSize(512);
        mqttEnabled = true;
    } else {
        Serial.println("\n[NET] WiFi failed - falling back to serial-only");
    }
}

// Lazily (re)connect to the broker. Sensor uplinks are infrequent, so we just
// reconnect on demand rather than holding the link open between packets.
static bool mqtt_ensure() {
    if (!mqttEnabled) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    if (mqtt.connected()) return true;

    bool ok;
    if (strlen(MQTT_USERNAME) > 0) {
        ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
    } else {
        ok = mqtt.connect(MQTT_CLIENT_ID);
    }
    if (ok) {
        Serial.printf("[NET] MQTT connected to %s:%d\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    } else {
        Serial.printf("[NET] MQTT connect failed, state %d\n", mqtt.state());
    }
    return ok;
}

// =============================================================================
// PACKET HANDLING
// =============================================================================
static void handle_packet() {
    uint8_t buf[P2P_HEADER_LEN + P2P_MAX_LPP];
    int len = radio.getPacketLength();
    if (len <= 0 || len > (int)sizeof(buf)) {
        radio.startReceive();
        return;
    }

    int rs = radio.readData(buf, len);
    float rssi = radio.getRSSI();
    float snr  = radio.getSNR();
    radio.startReceive();   // re-arm immediately

    if (rs != RADIOLIB_ERR_NONE) {
        Serial.printf("[RX] readData error %d\n", rs);
        return;
    }

    // --- Validate envelope ---
    if (len < P2P_HEADER_LEN || buf[0] != P2P_MAGIC_0 || buf[1] != P2P_MAGIC_1) {
        Serial.printf("[RX] dropped foreign packet (%d bytes, rssi %.0f)\n", len, rssi);
        return;
    }
    uint8_t version = buf[2];
    uint8_t nodeId  = buf[3];
    uint8_t lppLen  = buf[4];
    if (version != P2P_PROTO_VERSION) {
        Serial.printf("[RX] proto version mismatch: got %u, expected %u\n",
                      version, P2P_PROTO_VERSION);
        return;
    }
    if (P2P_HEADER_LEN + lppLen > len) {
        Serial.printf("[RX] truncated frame: header says %u LPP bytes, only %d on air\n",
                      lppLen, len - P2P_HEADER_LEN);
        lppLen = len - P2P_HEADER_LEN;
    }

    // --- Decode ---
    JsonDocument doc;
    doc["node"] = nodeId;
    doc["rssi"] = rssi;
    doc["snr"]  = snr;
    bool ok = decode_lpp(&buf[P2P_HEADER_LEN], lppLen, doc);
    if (!ok) doc["decode_partial"] = true;

    // --- Update the OLED if this frame carried a moisture reading ---
    if (!doc["wood_mc"].isNull()) {
        dispMc        = doc["wood_mc"].as<float>();
        dispTemp      = doc["wood_temp_c"].isNull() ? NAN : doc["wood_temp_c"].as<float>();
        dispFallback  = !doc["temp_fallback"].isNull() && doc["temp_fallback"].as<int>() != 0;
        dispRssi      = (int)rssi;
        dispNode      = nodeId;
        lastReadingMs = millis();
        haveReading   = true;
        display_reading();
    }

    // --- Output: serial always, MQTT when available ---
    char out[384];
    size_t n = serializeJson(doc, out, sizeof(out));
    Serial.println(out);

    if (mqtt_ensure()) {
        if (!mqtt.publish(MQTT_TOPIC, (const uint8_t *)out, n, false)) {
            Serial.println("[NET] MQTT publish failed");
        }
    }
}

// =============================================================================
// SETUP / LOOP
// =============================================================================
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000);
    Serial.println("\n====================================");
    Serial.println(" Wood Moisture LoRa Receiver (P2P)");
    Serial.println("====================================");
    Serial.printf("[RX] channel: %.3f MHz  SF%d  BW%.0f  CR4/%d  sync 0x%02X\n",
                  P2P_FREQUENCY_MHZ, P2P_SPREADING_FACTOR, P2P_BANDWIDTH_KHZ,
                  P2P_CODING_RATE, P2P_SYNC_WORD);

    display_init();
    display_waiting();

    SPI.begin(HELTEC_LORA_SCK, HELTEC_LORA_MISO, HELTEC_LORA_MOSI, HELTEC_LORA_NSS);
    int st = radio.begin(P2P_FREQUENCY_MHZ, P2P_BANDWIDTH_KHZ, P2P_SPREADING_FACTOR,
                         P2P_CODING_RATE, P2P_SYNC_WORD, P2P_TX_POWER_DBM,
                         P2P_PREAMBLE_LENGTH, HELTEC_TCXO_VOLTAGE);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[RX] SX1262 init FAILED, code %d - halting\n", st);
        while (true) delay(1000);
    }
    radio.setDio2AsRfSwitch(true);
    Serial.println("[RX] SX1262 initialized OK");

    wifi_connect();

    radio.setPacketReceivedAction(onPacketReceived);
    st = radio.startReceive();
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[RX] startReceive FAILED, code %d - halting\n", st);
        while (true) delay(1000);
    }
    Serial.println("[RX] listening...");
}

void loop() {
    if (packetReady) {
        packetReady = false;
        handle_packet();
    }
    if (mqttEnabled && mqtt.connected()) mqtt.loop();

    // Refresh the reading once a second so the "Ns ago" age stays current.
    static uint32_t lastDisplayMs = 0;
    if (haveReading && millis() - lastDisplayMs >= 1000) {
        lastDisplayMs = millis();
        display_reading();
    }
}
