// lmic_project_config.h - MCCI LMIC Project Configuration
// This file is used by the MCCI LoRaWAN LMIC library to set region and radio type.
// It must be in the include path.

#ifndef LMIC_PROJECT_CONFIG_H_
#define LMIC_PROJECT_CONFIG_H_

// --- CHOOSE YOUR REGION AND RADIO TYPE ---
// This project is configured for EU433 and SX1262 by default in platformio.ini
// Ensure these settings match what is defined in your platformio.ini build_flags.

// Example for EU433 and SX1262 radio:
// (These are typically set by -D flags in platformio.ini, but can be hardcoded here as a fallback)
#if !defined(CFG_eu433)
// #define CFG_eu433 1 //Fallback if not defined in platformio.ini
#endif

#if !defined(CFG_sx1262_radio)
// #define CFG_sx1262_radio 1 //Fallback if not defined in platformio.ini
#endif

// Example for US915 and SX1262 radio:
// #define CFG_us915 1
// #define CFG_sx1262_radio 1

// Example for EU868 and SX1276 radio:
// #define CFG_eu868 1
// #define CFG_sx1276_radio 1

// --- OTHER LMIC CONFIGURATIONS ---

// Increase TX_COMPLETE_PING_SLOT_DATARATE if you have issues with PING_SLOT_INFO_REQ
// #define TX_COMPLETE_PING_SLOT_DATARATE DR_SF12

// Enable this if you are using AXP192/AXP202 PMU for LDO and TCXO power control
// This is often specific to the board hardware design.
// For T-Beam with SX1262, TCXO control might be linked to a GPIO or always on.
// LMIC has its own radio power up/down logic. AXP control is more about overall system power.
// #define ENABLE_TCXO_CONTROL 1 // Example: if TCXO is controlled by a GPIO
// #define TCXO_LDO_CTRL_PIN XX // Define the pin if ENABLE_TCXO_CONTROL is 1

// Define this to disable all prints from LMIC library, saving flash and RAM
// #define DISABLE_LMIC_FAILURE_TO_SERIAL

// Define the SPI port used by the LoRa chip.
// For ESP32, an spi_device_handle_t is used by LMIC, typically VSPI.
// This is usually handled by the HAL and board support package.
// #define USE_SPI_TRANSACTION // Already default for ESP32

// In the MCCI LMIC library, the clock error is often set in the main code using LMIC_setClockError.
// Default is MAX_CLOCK_ERROR_PERCENT for a crystal.
// For TCXO, it can be set lower, e.g., MAX_CLOCK_ERROR_TCXO_PERCENT.
// #define LMIC_CLOCK_ERROR_PERCENTAGE 1 // For 1% clock error (crystal)
// #define LMIC_CLOCK_ERROR_PERCENTAGE 0.005 // For 0.005% clock error (TCXO) if you have one and set it.

#endif // LMIC_PROJECT_CONFIG_H_
