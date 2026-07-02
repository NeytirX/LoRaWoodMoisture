// config.h - Project Configuration File (LoRaWAN Wood Moisture Version)
//
// Every tunable parameter for the wood moisture sensor firmware:
// pins, thresholds, intervals, LPP channels, and downlink command bytes.
// Uses RadioLib (SX1262). The LoRaWAN region is runtime-selected from NVS
// (see LORAWAN_REGION_DEFAULT).

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "wood_species_data.h"      // Include for species selection
#include "lorawan_keys.h"           // Include for LoRaWAN OTAA keys

// =============================================================================
// DEBUG OPTIONS
// =============================================================================
#define SERIAL_BAUD 115200
// Set to 0 for deployment builds: skips the boot serial-ready wait that idles
// at full power every wake cycle (significant long-term battery cost).
#define DEBUG_MODE 1
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINT(x)   Serial.print(x)
// Uncomment to disable all debug prints (saves flash/RAM in release builds):
// #define DEBUG_PRINTLN(x)
// #define DEBUG_PRINT(x)

// =============================================================================
// FIRMWARE VERSION
// =============================================================================
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 1
#define FIRMWARE_VERSION_PATCH 1

// =============================================================================
// POWER MANAGEMENT (AXP192 on T-Beam v1.1, AXP2101 on v1.2 - auto-detected)
// =============================================================================
#define USE_AXP_POWER_MANAGEMENT true
// Note: AXP192_SLAVE_ADDRESS / AXP2101_SLAVE_ADDRESS are defined by XPowersLib.
// Do NOT redefine them here.

// =============================================================================
// WOOD MOISTURE SENSOR (RESISTIVE PROBE)
// =============================================================================
// NOTE: GPIO 32 is the SX1262 BUSY line on the T-Beam - do not use it here.
// GPIO 35 is ADC1_CH7, input-only, free on the T-Beam header.
#define MOISTURE_PROBE_ADC_PIN   35
#define MOISTURE_PROBE_POWER_PIN 25
#define ADC_READ_STABILIZATION_MS 100
#define ADC_SAMPLES_TO_AVERAGE    10

#define R_PULLUP_OHMS      100000.0f  // Pull-up resistor value (Ohms). CRITICAL for accuracy.
#define ADC_MAX_READING    4095.0f    // 12-bit ADC max (ESP32)
#define VCC_PROBE_VOLTAGE  3.3f       // Divider top voltage (probe power pin HIGH).
                                      // Measure the pin under load and set this for best accuracy.
#define ADC_SHORT_CIRCUIT_MV 5.0f     // Node below this (mV) = short / extremely wet
// ESP32 ADC (11 dB) linearity knee. Above this node voltage the converter is
// nonlinear and the divider R is compressed (the "silent zone"): readings are
// still returned but flagged low-confidence (LPP channel 8). ~2.45 V per the
// front-end plan (docs/ai/2026-06-05-001-feat-measurement-front-end-plan.md).
#define ADC_LINEARITY_LIMIT_MV 2450.0f

// ADC Attenuation - ADC_11db gives full 0-3.3V range on ESP32
// Options: ADC_0db (0-1.1V), ADC_2_5db (0-1.5V), ADC_6db (0-2.2V), ADC_11db (0-3.3V)
#define ADC_ATTENUATION    ADC_11db

// Species selection (index into species_data[] in wood_species_data.h)
#define SELECTED_WOOD_SPECIES_INDEX 0

// Resistance sanity bounds (Ohms) - coarse value-based check that drives a
// serial warning only. The trustworthy-measurement signal is the ADC linearity
// flag (ADC_LINEARITY_LIMIT_MV, LPP channel 8), NOT these bounds: in the silent
// compression zone the returned R is itself corrupted, so a value bound would
// pass readings that are quantitatively wrong. MAX is the practical ADC clip
// (~1.55 MOhm at ~3.1 V); above it the node is rail-pinned (see front-end plan).
#define MIN_VALID_RESISTANCE_OHMS   1000.0f       // 1 kOhm (very wet / short)
#define MAX_VALID_RESISTANCE_OHMS   1550000.0f    // ~1.55 MOhm (practical ADC clip)

// =============================================================================
// TEMPERATURE SENSING
// =============================================================================
#define ENABLE_TEMPERATURE_COMPENSATION true
#define DEFAULT_WOOD_TEMP_CELSIUS 21.0f   // ~70F, used if temp sensor fails

// DS18B20 1-Wire Temperature Sensor (integrated in probe assembly)
// GPIO 14 is free on the T-Beam v1.1 and suitable for 1-Wire
#define ONEWIRE_PIN              14
#define DS18B20_RESOLUTION_BITS  12       // 9..12 bits (12 = 0.0625C, ~750ms conversion)
#define DS18B20_READ_TIMEOUT_MS  1000     // Max wait for conversion

// =============================================================================
// LORA RADIO PINS (SX1262 on T-Beam v1.1/v1.2)
// =============================================================================
// The T-Beam wires the LoRa modem to a dedicated SPI bus - these are NOT the
// ESP32 default VSPI pins. main.cpp must call
// SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_CS_PIN)
// before radio.begin(), otherwise the SX1262 never responds (CHIP_NOT_FOUND).
#define LORA_SCK_PIN  5   // SPI clock
#define LORA_MISO_PIN 19  // SPI MISO
#define LORA_MOSI_PIN 27  // SPI MOSI
#define LORA_CS_PIN   18  // NSS / SPI Chip Select
#define LORA_RST_PIN  23  // RESET
#define LORA_DIO1_PIN 33  // DIO1 (IRQ)
#define LORA_BUSY_PIN 32  // BUSY

// =============================================================================
// OPERATIONAL INTERVALS
// =============================================================================
#define NORMAL_SEND_INTERVAL_SECONDS   (60 * 60)  // 60 minutes
// #define ALERT_SEND_INTERVAL_SECONDS (15 * 60)  // Alternative shorter interval

// =============================================================================
// BATTERY-AWARE SLEEP
// =============================================================================
#define LOW_BATTERY_THRESHOLD_V        3.4f    // Below this, extend sleep to conserve power
#define CRITICAL_BATTERY_THRESHOLD_V   3.2f    // Below this, sleep even longer
#define BATTERY_ABSENT_THRESHOLD_V     0.5f    // Below this + USB present = no battery connected
#define LOW_BATTERY_SLEEP_MULTIPLIER   2       // 2x normal interval when low
#define CRITICAL_BATTERY_SLEEP_MULTIPLIER 4    // 4x normal interval when critical

// =============================================================================
// DEEP SLEEP & LORAWAN RETRIES
// =============================================================================
#define LORAWAN_JOIN_MAX_RETRIES          5
#define LORAWAN_JOIN_RETRY_SLEEP_SECONDS  30

// =============================================================================
// WATCHDOG TIMER
// =============================================================================
#define WATCHDOG_TIMEOUT_SECONDS  120  // Reset if loop hangs for > 2 minutes

// =============================================================================
// CAYENNE LPP CHANNELS
// =============================================================================
#define LPP_CHANNEL_WOOD_MC         1  // Wood Moisture Content (%)
#define LPP_CHANNEL_WOOD_TEMP       2  // Wood Temperature (C)
#define LPP_CHANNEL_INDICATED_MC    3  // Indicated MC (before temp correction)
#define LPP_CHANNEL_RESISTANCE      4  // Wood Resistance (kOhms)
#define LPP_CHANNEL_BATTERY_VOLTAGE 5  // Battery Voltage (V)
#define LPP_CHANNEL_ESP_TEMP        6  // ESP32 Internal Chip Temperature (C)
#define LPP_CHANNEL_TEMP_FALLBACK   7  // Digital: 1 = no valid DS18B20 reading, MC corrected with DEFAULT_WOOD_TEMP_CELSIUS
#define LPP_CHANNEL_ADC_NONLINEAR   8  // Digital: 1 = ADC past linearity knee/saturated, returned R compressed, MC low-confidence

// =============================================================================
// DOWNLINK COMMAND BYTES (for remote configuration via LoRaWAN downlink)
// =============================================================================
// Downlink payload format: [CMD_BYTE] [VALUE_BYTE(s)]
#define DOWNLINK_CMD_SET_INTERVAL   0x01  // + 2 bytes: interval in minutes (uint16 big-endian)
#define DOWNLINK_CMD_SET_SPECIES    0x02  // + 1 byte: species index
#define DOWNLINK_CMD_FORCE_REJOIN   0x03  // No extra bytes - forces a fresh OTAA join
#define DOWNLINK_CMD_SET_TX_POWER   0x04  // + 1 byte: TX power index
#define DOWNLINK_CMD_SET_REGION     0x05  // + 1 byte: 0=EU868, 1=EU433 (persisted in NVS)

// =============================================================================
// NVS (Non-Volatile Storage) KEYS for RadioLib session persistence
// =============================================================================
#define NVS_NAMESPACE       "lorawan"
#define NVS_KEY_NONCES      "nonces"
#define NVS_KEY_REGION      "region"       // 0=EU868, 1=EU433 (persisted via downlink)

// Region used when NVS holds no value (fresh device): 0=EU868, 1=EU433.
// Runtime override is downlink 0x05, but a fresh device can only receive that
// downlink after joining in this default region - pick the region matching the
// board's RF matching/gateway before first flash.
#define LORAWAN_REGION_DEFAULT 0

#endif // CONFIG_H
