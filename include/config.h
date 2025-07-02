// config.h - Project Configuration File (LoRa P2P Wood Moisture Version)

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "wood_species_data.h" // Include for species selection

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
#define MOISTURE_PROBE_ADC_PIN 32      // ADC pin for voltage divider reading
#define MOISTURE_PROBE_POWER_PIN 25    // GPIO pin to power the pull-up resistor
#define ADC_READ_STABILIZATION_MS 100  // Delay after powering probe before ADC reading
#define ADC_SAMPLES_TO_AVERAGE 10      // Number of ADC samples to average for stability

#define R_PULLUP_OHMS 100000.0f        // Value of the pull-up resistor in Ohms (e.g., 100k). CRITICAL for accuracy.
#define ADC_MAX_READING 4095.0f        // Max ADC reading (12-bit for ESP32)
#define VCC_PROBE_VOLTAGE 3.3f         // Voltage supplied to the voltage divider (usually 3.3V from GPIO)

// Wood Species Selection (Index into the species_data[] array in wood_species_data.h)
#define SELECTED_WOOD_SPECIES_INDEX 0  // 0 = Douglas-Fir, 1 = Pine, etc. (refer to wood_species_data.h)

// Temperature Compensation
#define ENABLE_TEMPERATURE_COMPENSATION true
#define DEFAULT_WOOD_TEMP_CELSIUS 21.0f // Used if temp sensor fails or compensation is off (approx 70°F)
// If using an external temp sensor, define its pins and type here if necessary.
// For ESP32 internal sensor, no extra pins needed.

// --- LORA P2P CONFIGURATION (SX1262) ---
#define LORA_FREQUENCY 433.0
#define LORA_BANDWIDTH 125.0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODING_RATE 5
#define LORA_PREAMBLE_LENGTH 8
#define LORA_TX_POWER 14
#define LORA_SYNC_WORD 0x12

// --- LORA RADIO PINS (SX1262 on T-Beam v1.1) ---
#define LORA_CS_PIN   5
#define LORA_DIO1_PIN 33
#define LORA_RST_PIN  27
#define LORA_BUSY_PIN 26

// --- OPERATIONAL INTERVAL ---
#define P2P_SEND_INTERVAL_SECONDS (15 * 60) // e.g., 15 minutes

#endif // CONFIG_H
