// include/lorawan_keys.h
// LoRaWAN OTAA keys for RadioLib
//
// IMPORTANT: This file should be added to .gitignore if you are using a public repository!

#ifndef LORAWAN_KEYS_H
#define LORAWAN_KEYS_H

#include <stdint.h>

// LoRaWAN Keys (OTAA) - IMPORTANT: FILL THESE IN!
//
// RadioLib requires:
//   - JoinEUI (AppEUI) and DevEUI as uint64_t in MSB (big-endian) hex literal
//   - NwkKey (AppKey in LoRaWAN 1.0.x) and AppKey as uint8_t[16] in MSB byte order
//
// How to get values from TTN Console:
//   In the TTN Console, go to your device -> Overview.
//   For JoinEUI / DevEUI: use the MSB hex string, prefix with 0x
//     e.g., if TTN shows 70B3D57ED0000001 -> use 0x70B3D57ED0000001
//   For NwkKey / AppKey: use the MSB byte array
//     e.g., if TTN shows 01 02 03 ... 10 -> { 0x01, 0x02, 0x03, ..., 0x10 }
//
// NOTE on LoRaWAN 1.0.x vs 1.1:
//   In LoRaWAN 1.0.x, there is only one key called "AppKey".
//   RadioLib calls it "nwkKey" for forward compatibility with 1.1.
//   Set both nwkKey and appKey to the same value for LoRaWAN 1.0.x.

// JoinEUI (formerly AppEUI) - MSB hex literal
static const uint64_t joinEUI = 0x0000000000000000;  // <-- FILL THIS

// DevEUI - MSB hex literal
static const uint64_t devEUI  = 0x0000000000000000;  // <-- FILL THIS

// NwkKey (AppKey in LoRaWAN 1.0.x) - MSB byte array
static const uint8_t nwkKey[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // <-- FILL THIS
};

// AppKey (same as NwkKey for LoRaWAN 1.0.x) - MSB byte array
static const uint8_t appKey[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // <-- FILL THIS (same as nwkKey for 1.0.x)
};

#endif // LORAWAN_KEYS_H
