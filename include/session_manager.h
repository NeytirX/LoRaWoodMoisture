// include/session_manager.h — LoRaWAN Session Persistence (RadioLib v7.6.0)
//
// Saves and restores RadioLib LoRaWAN session buffers across ESP32 deep sleep
// and power cycles, avoiding a costly OTAA rejoin on every wake.
//
// Strategy:
//   - Nonces buffer  → NVS (survives power loss; only changes on fresh join)
//   - Session buffer → RTC RAM (survives deep sleep; lost on power cycle)
//
// RadioLib v7.6.0 API used:
//   node.getBufferNonces()  → uint8_t*  (RADIOLIB_LORAWAN_NONCES_BUF_SIZE bytes)
//   node.getBufferSession() → uint8_t*  (RADIOLIB_LORAWAN_SESSION_BUF_SIZE bytes)
//   node.setBufferNonces(const uint8_t*)
//   node.setBufferSession(const uint8_t*)

#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>   // ESP32 NVS wrapper (part of Arduino-ESP32 framework)
#include <RadioLib.h>      // For RADIOLIB_LORAWAN_SESSION_BUF_SIZE / NONCES_BUF_SIZE
#include "config.h"        // For NVS_NAMESPACE, NVS_KEY_NONCES

// =============================================================================
// RTC RAM — Session buffer (survives deep sleep, lost on power cycle)
// =============================================================================
RTC_DATA_ATTR bool     rtc_session_valid = false;
RTC_DATA_ATTR uint8_t  rtc_session_buf[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

// =============================================================================
// NVS helpers — Nonces (survives power loss)
// =============================================================================

/**
 * Save nonces buffer to NVS.
 * Call after a successful fresh OTAA join (NEW_SESSION).
 */
inline void session_save_nonces(const uint8_t* buf, size_t len) {
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putBytes(NVS_KEY_NONCES, buf, len);
        prefs.end();
        Serial.println(F("[Session] Nonces saved to NVS."));
    } else {
        Serial.println(F("[Session] ERROR: Could not open NVS namespace for write."));
    }
}

/**
 * Load nonces buffer from NVS into the provided buffer.
 * Returns the number of bytes read (0 if not found or error).
 */
inline size_t session_load_nonces(uint8_t* buf, size_t maxLen) {
    Preferences prefs;
    size_t bytesRead = 0;
    if (prefs.begin(NVS_NAMESPACE, true)) {  // read-only
        size_t stored = prefs.getBytesLength(NVS_KEY_NONCES);
        if (stored > 0 && stored <= maxLen) {
            bytesRead = prefs.getBytes(NVS_KEY_NONCES, buf, maxLen);
            Serial.print(F("[Session] Nonces loaded from NVS ("));
            Serial.print(bytesRead);
            Serial.println(F(" bytes)."));
        } else if (stored == 0) {
            Serial.println(F("[Session] No nonces found in NVS."));
        } else {
            Serial.println(F("[Session] ERROR: Stored nonces size mismatch — ignoring."));
        }
        prefs.end();
    } else {
        Serial.println(F("[Session] NVS namespace not found (first boot?)."));
    }
    return bytesRead;
}

// =============================================================================
// RTC helpers — Session state (survives deep sleep)
// =============================================================================

/**
 * Save session buffer to RTC RAM.
 * Call after every successful sendReceive() to keep frame counters current.
 */
inline void session_save_rtc(const uint8_t* buf, size_t len) {
    if (len > sizeof(rtc_session_buf)) {
        Serial.println(F("[Session] ERROR: Session buffer too large for RTC!"));
        return;
    }
    memcpy(rtc_session_buf, buf, len);
    rtc_session_valid = true;
    Serial.println(F("[Session] Session saved to RTC RAM."));
}

/**
 * Restore session buffer from RTC RAM into the provided buffer.
 * Returns true if a valid session was found, false otherwise.
 */
inline bool session_restore_rtc(uint8_t* buf, size_t maxLen, size_t& outLen) {
    if (!rtc_session_valid) {
        Serial.println(F("[Session] No valid session in RTC RAM."));
        outLen = 0;
        return false;
    }
    size_t len = RADIOLIB_LORAWAN_SESSION_BUF_SIZE;
    if (len > maxLen) {
        Serial.println(F("[Session] ERROR: RTC session too large for buffer!"));
        outLen = 0;
        return false;
    }
    memcpy(buf, rtc_session_buf, len);
    outLen = len;
    Serial.println(F("[Session] Session restored from RTC RAM."));
    return true;
}

// =============================================================================
// Invalidation
// =============================================================================

/**
 * Invalidate both RTC session and NVS nonces.
 * Call on cold boot, force-rejoin downlink, or detected session corruption.
 */
inline void session_invalidate() {
    rtc_session_valid = false;
    memset(rtc_session_buf, 0, sizeof(rtc_session_buf));

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.remove(NVS_KEY_NONCES);
        prefs.end();
    }
    Serial.println(F("[Session] Session invalidated (RTC + NVS cleared)."));
}

#endif // SESSION_MANAGER_H
