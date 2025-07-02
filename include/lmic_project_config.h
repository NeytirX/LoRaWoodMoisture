// lmic_project_config.h - MCCI LMIC Project Configuration
// This file is used by the MCCI LoRaWAN LMIC library to set region and radio type.
// It must be in the include path.

#ifndef LMIC_PROJECT_CONFIG_H_
#define LMIC_PROJECT_CONFIG_H_

// --- CHOOSE YOUR REGION AND RADIO TYPE ---
// This project is configured by default in platformio.ini via build_flags.
// Ensure these settings match what is defined in your platformio.ini build_flags.

// Example for EU433 and SX1262 radio:
// (These are typically set by -D flags in platformio.ini, but can be hardcoded here as a fallback)
#if defined(CFG_eu433) && !defined(CFG_LMIC_EU_like)
    #define CFG_LMIC_EU_like 1 // EU433 uses EU-like band plan structure
#endif

#if !defined(CFG_eu433)
// #define CFG_eu433 1 // Fallback if not defined in platformio.ini
#endif

#if !defined(CFG_sx1262_radio)
// #define CFG_sx1262_radio 1 // Fallback if not defined in platformio.ini
#endif

// Example for US915 and SX1262 radio:
// #define CFG_us915 1
// #define CFG_sx1262_radio 1

// Example for EU868 and SX1276 radio:
// #define CFG_eu868 1
// #define CFG_sx1276_radio 1

// --- LMIC FREQUENCY SETTINGS (ADVANCED) ---
// For regions like EU433, you might need to specify channel frequencies if not default.
// However, LMIC usually has defaults for CFG_eu433.
// #define CFG_LMIC_MIN_FREQ 433000000
// #define CFG_LMIC_MAX_FREQ 434000000

// --- OTHER LMIC CONFIGURATIONS ---
// In the MCCI LMIC library, the clock error is often set in the main code using LMIC_setClockError.
// For T-Beam with SX1262, it has a TCXO, so a lower clock error can be set.
// #define LMIC_CLOCK_ERROR_PPM 5 // Example: 5 PPM for a TCXO

// To disable all prints from LMIC library, saving flash and RAM (for release builds)
// #define DISABLE_LMIC_FAILURE_TO_SERIAL

#endif // LMIC_PROJECT_CONFIG_H_
