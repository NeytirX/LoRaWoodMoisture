// include/lorawan_keys.h
// IMPORTANT: This file should be added to .gitignore if you are using a public repository!

#ifndef LORAWAN_KEYS_H
#define LORAWAN_KEYS_H

#include <lmic.h> // For u1_t type

// LoRaWAN Keys (OTAA) - IMPORTANT: FILL THESE IN!
// These are MSB, copy them from your LoRaWAN Network Server (e.g., TTN)
// For TTN, go to your device -> Overview -> Device EUI / App EUI / App Key
// Click the <> icon to get the C-style array format and copy-paste here.

// Application EUI (APPEUI) - MSB format
static const u1_t PROGMEM APPEUI[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // <-- FILL THIS

// Device EUI (DEVEUI) - MSB format for MCCI LMIC os_getDevEUI callback
// (Note: TTN console often shows DevEUI in LSB first. Ensure you convert/copy the MSB version for this array)
static const u1_t PROGMEM DEVEUI[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // <-- FILL THIS

// Application Key (APPKEY) - MSB format
static const u1_t PROGMEM APPKEY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // <-- FILL THIS
};

#endif // LORAWAN_KEYS_H
