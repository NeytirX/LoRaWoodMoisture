// p2p_frame.h - Wire format for the gateway-less raw-LoRa bench/interim path.
//
// THIS FILE IS THE CONTRACT shared by the T-Beam transmitter and the Heltec
// receiver. It is intentionally dependency-free (only <stdint.h>) so both the
// receiver project and the smoke-test transmitter can include it via -I.
//
// During the later transport-seam refactor this becomes the single shared
// header in the firmware's include/. UNTIL THEN, the T-Beam firmware mirrors
// these values in include/config.h (the LPP_CHANNEL_* defines) and in its P2P
// radio params. THEY MUST MATCH on both sides or the link will not decode.

#ifndef P2P_FRAME_H
#define P2P_FRAME_H

#include <stdint.h>

// =============================================================================
// RADIO CHANNEL - must match the T-Beam P2P transmit build byte for byte
// =============================================================================
// EU433 ISM band is 433.05 - 434.79 MHz. Sync word 0x12 is "private" LoRa,
// deliberately distinct from LoRaWAN's 0x34 so this link never collides with
// (or is mistaken for) a real LoRaWAN network.
#define P2P_FREQUENCY_MHZ     433.5f
#define P2P_BANDWIDTH_KHZ     125.0f
#define P2P_SPREADING_FACTOR  9
#define P2P_CODING_RATE       5       // RadioLib: 5..8 == 4/5..4/8
#define P2P_SYNC_WORD         0x12    // private LoRa (LoRaWAN uses 0x34)
#define P2P_TX_POWER_DBM      10      // ~10 mW, EU433 ERP-polite
#define P2P_PREAMBLE_LENGTH   8

// =============================================================================
// FRAME ENVELOPE
// =============================================================================
// Layout: [MAGIC0][MAGIC1][VERSION][NODE_ID][LPP_LEN][ ... Cayenne LPP ... ]
// The magic bytes let the receiver reject stray traffic; NODE_ID lets a single
// receiver disambiguate multiple sensors later.
#define P2P_MAGIC_0        0x57   // 'W'
#define P2P_MAGIC_1        0x4D   // 'M'
#define P2P_PROTO_VERSION  1
#define P2P_HEADER_LEN     5      // magic0 + magic1 + version + nodeId + lppLen
#define P2P_MAX_LPP        128    // matches CayenneLPP lpp(128) on the sensor

// =============================================================================
// CAYENNE LPP CHANNEL MAP - must match include/config.h LPP_CHANNEL_*
// =============================================================================
#define LPP_CHANNEL_WOOD_MC          1   // analog, % MC (corrected)
#define LPP_CHANNEL_WOOD_TEMP        2   // temperature, C
#define LPP_CHANNEL_INDICATED_MC     3   // analog, % MC (pre-correction)
#define LPP_CHANNEL_RESISTANCE       4   // analog, kOhm
#define LPP_CHANNEL_BATTERY_VOLTAGE  5   // analog, V
#define LPP_CHANNEL_ESP_TEMP         6   // temperature, C
#define LPP_CHANNEL_TEMP_FALLBACK    7   // digital, 1 = default temp substituted

// =============================================================================
// CAYENNE LPP DATA TYPE IDS (subset the sensor actually emits)
// =============================================================================
#define LPP_TYPE_DIGITAL_INPUT  0x00   // 1 byte, unsigned
#define LPP_TYPE_ANALOG_INPUT   0x02   // 2 bytes, signed, scale 0.01
#define LPP_TYPE_TEMPERATURE    0x67   // 2 bytes, signed, scale 0.1

#endif // P2P_FRAME_H
