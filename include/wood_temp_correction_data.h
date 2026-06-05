// include/wood_temp_correction_data.h
// Temperature correction data C_t (% MC) for resistance-type moisture readings,
// digitized from James, W.L. (1988), "Electric Moisture Meters for Wood",
// FPL-GTR-6, Figure 5 (calibration temperature 70 F / 21.1 C).
//
// Sign convention (verified against the figure's worked example, 2026-06-05):
// cold wood reads LOW -> C_t is POSITIVE below ~21 C; warm wood reads HIGH ->
// C_t is NEGATIVE above. The value is *added* to the indicated MC.
//
// Provenance: curves extracted programmatically from the GTR-6 scan
// (y-dewarped against local gridlines, anchored on the 70 F identity where
// curve k reads exactly k, EM-fitted as anchored cubics; per-curve rms vs the
// measured chart < 0.15 % MC; the figure's worked example - indicated 18 % at
// 120 F -> true 14 % - reproduces as 14.1). Pipeline: scripts/digitize_gtr6_fig5.py
// + scripts/generate_temp_table.py (local-only, untracked).
//
// Validity: the chart's curve family spans true MC 6-28 %. Cells implying
// true MC above 28 (very cold + very wet, upper-right of the -20/-15 C rows)
// are linear extrapolations beyond the chart and carry extra uncertainty.
// Temperatures are in CELSIUS (the firmware reads the DS18B20 in Celsius and
// uses this grid directly, no unit conversion).

#ifndef WOOD_TEMP_CORRECTION_DATA_H
#define WOOD_TEMP_CORRECTION_DATA_H

#include <Arduino.h>

// Temperatures (Celsius) for rows in the table
const float temp_points_c[] PROGMEM = {
    -20.0f, -15.0f, -10.0f, -5.0f, 0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 35.0f, 40.0f, 45.0f, 50.0f
};
const int TEMP_POINTS_COUNT = sizeof(temp_points_c) / sizeof(float);

// Indicated moisture content (%) for columns in the table
const float mc_points_indicated[] PROGMEM = {
    6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f, 19.0f, 20.0f, 21.0f, 22.0f, 23.0f, 24.0f, 25.0f
};
const int MC_POINTS_COUNT = sizeof(mc_points_indicated) / sizeof(float);

// Correction values C_t (% MC) to be *added* to the indicated MC.
// Rows: temperature (Celsius), columns: indicated MC.
const float correction_table[TEMP_POINTS_COUNT][MC_POINTS_COUNT] PROGMEM = {
    /* -20C */ {  4.7f,   5.1f,   5.5f,   6.1f,   6.8f,   7.3f,   8.1f,   8.1f,   8.2f,   8.6f,   9.1f,   9.7f,  10.7f,  11.7f,  12.7f,  13.7f,  14.7f,  15.7f,  16.7f,  17.7f},
    /* -15C */ {  3.9f,   4.3f,   4.6f,   4.9f,   5.5f,   6.0f,   6.5f,   7.2f,   7.1f,   7.2f,   7.7f,   8.1f,   8.5f,   9.2f,   9.8f,  10.5f,  11.1f,  11.8f,  12.4f,  13.1f},
    /* -10C */ {  3.2f,   3.5f,   3.8f,   4.1f,   4.4f,   4.9f,   5.2f,   5.7f,   6.2f,   6.2f,   6.2f,   6.7f,   7.0f,   7.3f,   7.8f,   8.3f,   8.7f,   9.2f,   9.6f,  10.1f},
    /*  -5C */ {  2.5f,   2.9f,   3.1f,   3.3f,   3.5f,   3.8f,   4.1f,   4.4f,   4.8f,   5.2f,   5.2f,   5.2f,   5.7f,   5.9f,   6.1f,   6.5f,   6.8f,   7.2f,   7.5f,   7.9f},
    /*  +0C */ {  1.9f,   2.2f,   2.4f,   2.5f,   2.7f,   2.9f,   3.1f,   3.4f,   3.6f,   3.9f,   4.3f,   4.2f,   4.2f,   4.6f,   4.8f,   4.9f,   5.2f,   5.4f,   5.7f,   6.0f},
    /*  +5C */ {  1.3f,   1.6f,   1.8f,   1.9f,   2.0f,   2.1f,   2.2f,   2.4f,   2.6f,   2.7f,   3.0f,   3.3f,   3.2f,   3.2f,   3.5f,   3.6f,   3.7f,   3.9f,   4.1f,   4.3f},
    /* +10C */ {  0.9f,   1.0f,   1.1f,   1.3f,   1.3f,   1.4f,   1.5f,   1.6f,   1.7f,   1.8f,   1.9f,   2.1f,   2.2f,   2.2f,   2.2f,   2.4f,   2.5f,   2.6f,   2.7f,   2.8f},
    /* +15C */ {  0.5f,   0.5f,   0.6f,   0.7f,   0.7f,   0.7f,   0.8f,   0.8f,   0.9f,   1.0f,   1.0f,   1.1f,   1.2f,   1.2f,   1.2f,   1.2f,   1.3f,   1.4f,   1.4f,   1.4f},
    /* +20C */ {  0.1f,   0.1f,   0.1f,   0.1f,   0.1f,   0.1f,   0.1f,   0.1f,   0.2f,   0.2f,   0.2f,   0.2f,   0.2f,   0.2f,   0.2f,   0.2f,   0.2f,   0.2f,   0.2f,   0.2f},
    /* +25C */ { -0.3f,  -0.3f,  -0.3f,  -0.4f,  -0.4f,  -0.5f,  -0.5f,  -0.5f,  -0.5f,  -0.5f,  -0.6f,  -0.6f,  -0.6f,  -0.7f,  -0.7f,  -0.8f,  -0.8f,  -0.8f,  -0.8f,  -0.9f},
    /* +30C */ { -0.7f,  -0.7f,  -0.8f,  -0.8f,  -0.9f,  -1.0f,  -1.1f,  -1.1f,  -1.2f,  -1.2f,  -1.3f,  -1.4f,  -1.4f,  -1.5f,  -1.6f,  -1.7f,  -1.8f,  -1.8f,  -1.8f,  -1.9f},
    /* +35C */ { -1.0f,  -1.1f,  -1.2f,  -1.3f,  -1.4f,  -1.5f,  -1.6f,  -1.7f,  -1.8f,  -1.8f,  -1.9f,  -2.0f,  -2.1f,  -2.2f,  -2.4f,  -2.5f,  -2.6f,  -2.8f,  -2.8f,  -2.8f},
    /* +40C */ { -1.4f,  -1.5f,  -1.6f,  -1.7f,  -1.8f,  -2.0f,  -2.2f,  -2.3f,  -2.4f,  -2.5f,  -2.6f,  -2.7f,  -2.8f,  -2.9f,  -3.1f,  -3.2f,  -3.4f,  -3.6f,  -3.7f,  -3.8f},
    /* +45C */ { -1.8f,  -1.9f,  -2.0f,  -2.2f,  -2.3f,  -2.5f,  -2.7f,  -2.8f,  -2.9f,  -3.0f,  -3.2f,  -3.3f,  -3.4f,  -3.6f,  -3.7f,  -3.9f,  -4.1f,  -4.3f,  -4.5f,  -4.7f},
    /* +50C */ { -2.1f,  -2.3f,  -2.4f,  -2.6f,  -2.7f,  -2.9f,  -3.1f,  -3.3f,  -3.5f,  -3.6f,  -3.8f,  -3.9f,  -4.1f,  -4.2f,  -4.4f,  -4.6f,  -4.8f,  -5.1f,  -5.2f,  -5.4f}
};

#endif // WOOD_TEMP_CORRECTION_DATA_H
