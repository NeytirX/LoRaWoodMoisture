// config.h - Project Configuration File

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <lmic.h> // For LoRaWAN constants

// --- DEBUG OPTIONS ---
#define SERIAL_BAUD 115200
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINT(x) Serial.print(x)
// To disable debug prints, comment out the above lines and uncomment these:
// #define DEBUG_PRINTLN(x)
// #define DEBUG_PRINT(x)

// --- POWER MANAGEMENT (AXP192/AXP2101) ---
// Set to true if your T-Beam has an AXP PMIC and you want to use XPowersLib
#define USE_AXP_POWER_MANAGEMENT true // Or false if not used or causing issues
// Which AXP chip? (Typically AXP192 for older T-Beams, AXP2101 for newer ones with SX1262)
#define AXP_CHIP_AXP192 0
#define AXP_CHIP_AXP2101 1
#define AXP_CHIP_TYPE AXP_CHIP_AXP192 // Change to AXP_CHIP_AXP2101 if you have a newer T-Beam

// --- SOIL MOISTURE SENSOR ---
#define SOIL_MOISTURE_ADC_PIN 32      // ADC pin connected to the sensor
#define SOIL_MOISTURE_POWER_PIN 25    // GPIO pin to power the sensor'''s pull-up resistor
#define ADC_READ_STABILIZATION_MS 100 // Wait time after powering sensor before reading
// Calibration values for mapping raw ADC to percentage (0-100%)
// These need to be determined experimentally for your specific sensor and resistor setup.
// Value for sensor in dry air (or completely dry soil)
#define ADC_RAW_DRY 0
// Value for sensor submerged in water (or completely saturated soil)
#define ADC_RAW_WET 4095 // Max value for 12-bit ADC on ESP32

// --- LORAWAN CONFIGURATION ---
// LoRaWAN Keys (OTAA) - IMPORTANT: FILL THESE IN!
// These are MSB, copy them from your LoRaWAN Network Server (e.g., TTN)
// For TTN, go to your device -> Overview -> Device EUI / App EUI / App Key
// Click the <> icon to get the C-style array format and copy-paste here.

// Example: static const u1_t PROGMEM APPEUI[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const u1_t PROGMEM APPEUI[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // FILL THIS

// Example: static const u1_t PROGMEM DEVEUI[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
static const u1_t PROGMEM DEVEUI[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // FILL THIS - This is LSB, TTN uses LSB for DevEUI

// Example: static const u1_t PROGMEM APPKEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
static const u1_t PROGMEM APPKEY[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // FILL THIS

// LoRaWAN Region (LMIC definition, must match platformio.ini and lmic_project_config.h)
// Options: LMIC_REGION_EU868, LMIC_REGION_US915, LMIC_REGION_AU915, LMIC_REGION_AS923, LMIC_REGION_KR920, LMIC_REGION_IN865
// For EU433, specific configuration is in lmic_project_config.h and platformio.ini
// The lmic_project_config.h will typically handle the specific sub-band for EU433.
// We will set this in main.cpp based on platformio.ini flags.

// --- LORA RADIO PINS (SX1262 on T-Beam) ---
// These are common for T-Beam SX1262. Verify with your board schematic if unsure.
#define LORA_CS_PIN   5  // NSS, SPI Chip Select
#define LORA_RST_PIN  27 // RESET
#define LORA_DIO1_PIN 33 // DIO1 (IRQ)
#define LORA_BUSY_PIN 26 // BUSY
// SPI pins for ESP32 (typically fixed for a given board, check your T-Beam version)
// VSPI: MOSI=23, MISO=19, SCK=18 (default on many ESP32 boards)
// HSPI: MOSI=13, MISO=12, SCK=14
// The LMIC library will often default to VSPI if not explicitly set by board variant.
// For T-Beam SX1262, RadioLib uses: SCK=5, MISO=19, MOSI=27, SS=18, DIO1=26, RST=23, BUSY=33
// However, for MCCI LMIC with SX1262, we usually use specific DIO pins.
// The pins below are based on common T-Beam SX1262 examples with MCCI LMIC.
// The SPI pins (SCK, MISO, MOSI) are usually implicitly handled by the SPIClass instance.

// Power pin for LoRa module (if controlled by GPIO, often handled by PMIC on T-Beam)
// #define LORA_POWER_PIN 12 // Example, if you need to control LoRa VDD directly

// --- OPERATIONAL INTERVALS ---
// The device will primarily use NORMAL_INTERVAL_SECONDS.
// ALERT_INTERVAL_SECONDS is provided as a configurable alternative that could be activated
// by future mechanisms (e.g., downlink command), but automatic switching based on sensor data is removed for this version.
#define NORMAL_INTERVAL_SECONDS (60 * 60) // e.g., 60 minutes (default operational interval)
#define ALERT_INTERVAL_SECONDS (15 * 60)  // e.g., 15 minutes (alternative interval, not automatically used by default)

// --- DEEP SLEEP ---
// Maximum LoRaWAN duty cycle for EU bands is 1%.
// Consider this when setting intervals, especially alert intervals.
// A very short alert interval might violate fair use policies if many alerts occur.
// Retry joining LoRaWAN after this many failed attempts before deep sleeping for a longer period
#define LORAWAN_JOIN_MAX_RETRIES 5
#define LORAWAN_JOIN_RETRY_SLEEP_SECONDS 30 // Sleep duration between join retries
#define LORAWAN_MAX_TX_RETRIES 3            // Max retries for sending a packet

// --- CAYENNE LPP CHANNELS ---
#define LPP_CHANNEL_MOISTURE 1
#define LPP_CHANNEL_BATTERY_VOLTAGE 2
#define LPP_CHANNEL_RSSI 3
#define LPP_CHANNEL_SNR 4

#endif // CONFIG_H
