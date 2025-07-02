// config.h - Project Configuration File (LoRaWAN Wood Moisture Version)

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <lmic.h> // For LoRaWAN constants
#include "wood_species_data.h" // Include for species selection
#include "lorawan_keys.h"      // Include for LoRaWAN OTAA keys

// --- DEBUG OPTIONS ---
#define SERIAL_BAUD 115200
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINT(x) Serial.print(x)

// --- POWER MANAGEMENT (AXP192/AXP2101) ---
#define USE_AXP_POWER_MANAGEMENT true
#define AXP_CHIP_AXP192 0
#define AXP_CHIP_AXP2101 1
#define AXP_CHIP_TYPE AXP_CHIP_AXP192 // T-Beam v1.1 uses AXP192

// --- WOOD MOISTURE SENSOR (RESISTIVE PROBE) ---
#define MOISTURE_PROBE_ADC_PIN 32
#define MOISTURE_PROBE_POWER_PIN 25
#define ADC_READ_STABILIZATION_MS 100
#define ADC_SAMPLES_TO_AVERAGE 10

#define R_PULLUP_OHMS 100000.0f        // Value of the pull-up resistor in Ohms (e.g., 100k). CRITICAL.
#define ADC_MAX_READING 4095.0f        // Max ADC reading (12-bit for ESP32)
#define VCC_PROBE_VOLTAGE 3.3f         // Voltage supplied to the voltage divider

#define SELECTED_WOOD_SPECIES_INDEX 0  // Index from species_data[] in wood_species_data.h

#define ENABLE_TEMPERATURE_COMPENSATION true
#define DEFAULT_WOOD_TEMP_CELSIUS 21.0f // Approx 70°F, used if temp sensor fails

// --- LORAWAN CONFIGURATION ---
// LoRaWAN Keys (APPEUI, DEVEUI, APPKEY) are now in lorawan_keys.h

// --- LORA RADIO PINS (SX1262 for MCCI LMIC on T-Beam) ---
#define LORA_CS_PIN   5  // NSS, SPI Chip Select
#define LORA_RST_PIN  27 // RESET
#define LORA_DIO1_PIN 33 // DIO1 (IRQ)
#define LORA_BUSY_PIN 26 // BUSY
// SPI pins (SCK, MISO, MOSI) are handled by the SPIClass instance.

// --- OPERATIONAL INTERVALS ---
#define NORMAL_SEND_INTERVAL_SECONDS (60 * 60) // e.g., 60 minutes
// #define ALERT_SEND_INTERVAL_SECONDS (15 * 60) // Alternative interval if needed later

// --- DEEP SLEEP & LORAWAN RETRIES ---
#define LORAWAN_JOIN_MAX_RETRIES 5
#define LORAWAN_JOIN_RETRY_SLEEP_SECONDS 30
#define LORAWAN_MAX_TX_RETRIES 3 // Retries for a single packet (LMIC may also have internal retries)

// --- CAYENNE LPP CHANNELS ---
#define LPP_CHANNEL_WOOD_MC 1          // Wood Moisture Content (%)
#define LPP_CHANNEL_WOOD_TEMP 2        // Wood Temperature (°C)
#define LPP_CHANNEL_INDICATED_MC 3     // Indicated MC (before temp correction)
#define LPP_CHANNEL_RESISTANCE 4       // Wood Resistance (kOhms) - for debugging/analysis
#define LPP_CHANNEL_BATTERY_VOLTAGE 5  // Battery Voltage (V)
#define LPP_CHANNEL_ESP_TEMP 6         // ESP32 Internal Chip Temperature (°C)


#endif // CONFIG_H
