// include/wood_temp_correction_data.h
// Stores temperature correction data for wood moisture content based on FPL GTR-06, Table 2.
// Values are C_t (% MC) to be *added* to the indicated MC.

#ifndef WOOD_TEMP_CORRECTION_DATA_H
#define WOOD_TEMP_CORRECTION_DATA_H

#include <Arduino.h>

// --- TEMPERATURE CORRECTION TABLE (FPL GTR-06, Table 2) ---
// Table indices for temperature and indicated moisture content.
// Temperatures are in Fahrenheit.

// Temperatures (°F) for rows in the table
const float temp_points_f[] PROGMEM = {
    0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 
    80.0f, 90.0f, 100.0f, 110.0f, 120.0f
    // Note: FPL GTR-06 table goes up to 250°F. For typical ambient, 120°F is a reasonable upper bound for this array.
    // If higher temps are needed, extend this array and the correction_table below.
};
const int TEMP_POINTS_COUNT = sizeof(temp_points_f) / sizeof(float);

// Indicated Moisture Content (%) for columns in the table
const float mc_points_indicated[] PROGMEM = {
    // Using a subset of FPL GTR-06 Table 2 columns for practicality.
    // Original table has columns for MC 6% to 30% (and sometimes beyond).
    // We will interpolate between these.
    6.0f,  7.0f,  8.0f,  9.0f,  10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 
    15.0f, 16.0f, 17.0f, 18.0f, 19.0f, 20.0f, 21.0f, 22.0f, 23.0f, 
    24.0f, 25.0f // FPL table often goes to 30% or more, but changes are smaller there.
                 // This range covers the most sensitive part.
    // For MC > 25%, the correction factor changes less dramatically.
    // We can extrapolate or use the 25% MC correction value for MC > 25% as an approximation.
};
const int MC_POINTS_COUNT = sizeof(mc_points_indicated) / sizeof(float);

// Correction values C_t (% MC) to be added.
// Rows: Temperature Index, Columns: Indicated MC Index
// Data from FPL GTR-06, Table 2. Values are approximate and transcribed.
// Verify against the original document for highest accuracy.
// This is a large table, so using PROGMEM is essential.
const float correction_table[TEMP_POINTS_COUNT][MC_POINTS_COUNT] PROGMEM = {
    // MC Indicated:  6      7      8      9      10     11     12     13     14     15     16     17     18     19     20     21     22     23     24     25
    /* 0°F */    {-1.8f, -2.1f, -2.3f, -2.6f, -2.9f, -3.1f, -3.4f, -3.7f, -3.9f, -4.2f, -4.5f, -4.7f, -5.0f, -5.3f, -5.5f, -5.8f, -6.1f, -6.3f, -6.6f, -6.9f},
    /* 10°F */   {-1.5f, -1.7f, -2.0f, -2.2f, -2.4f, -2.7f, -2.9f, -3.1f, -3.4f, -3.6f, -3.8f, -4.1f, -4.3f, -4.5f, -4.8f, -5.0f, -5.2f, -5.5f, -5.7f, -5.9f},
    /* 20°F */   {-1.2f, -1.4f, -1.6f, -1.8f, -2.0f, -2.2f, -2.4f, -2.6f, -2.8f, -3.0f, -3.2f, -3.4f, -3.6f, -3.8f, -4.0f, -4.2f, -4.4f, -4.6f, -4.8f, -5.0f},
    /* 30°F */   {-0.9f, -1.1f, -1.2f, -1.4f, -1.6f, -1.7f, -1.9f, -2.1f, -2.3f, -2.4f, -2.6f, -2.8f, -2.9f, -3.1f, -3.3f, -3.4f, -3.6f, -3.8f, -3.9f, -4.1f},
    /* 40°F */   {-0.7f, -0.8f, -0.9f, -1.0f, -1.1f, -1.3f, -1.4f, -1.5f, -1.7f, -1.8f, -1.9f, -2.1f, -2.2f, -2.3f, -2.5f, -2.6f, -2.7f, -2.9f, -3.0f, -3.1f},
    /* 50°F */   {-0.4f, -0.5f, -0.6f, -0.7f, -0.8f, -0.9f, -1.0f, -1.0f, -1.1f, -1.2f, -1.3f, -1.4f, -1.5f, -1.6f, -1.7f, -1.8f, -1.9f, -2.0f, -2.1f, -2.2f},
    /* 60°F */   {-0.2f, -0.2f, -0.3f, -0.3f, -0.4f, -0.4f, -0.5f, -0.5f, -0.6f, -0.6f, -0.7f, -0.7f, -0.8f, -0.8f, -0.9f, -0.9f, -1.0f, -1.0f, -1.1f, -1.1f},
    /* 70°F */   { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f}, // Reference temperature
    /* 80°F */   { 0.2f,  0.2f,  0.3f,  0.3f,  0.4f,  0.4f,  0.5f,  0.5f,  0.5f,  0.6f,  0.6f,  0.7f,  0.7f,  0.8f,  0.8f,  0.9f,  0.9f,  0.9f,  1.0f,  1.0f},
    /* 90°F */   { 0.3f,  0.4f,  0.5f,  0.6f,  0.7f,  0.7f,  0.8f,  0.9f,  1.0f,  1.0f,  1.1f,  1.2f,  1.2f,  1.3f,  1.4f,  1.4f,  1.5f,  1.5f,  1.6f,  1.7f},
    /* 100°F */  { 0.5f,  0.6f,  0.7f,  0.8f,  0.9f,  1.0f,  1.1f,  1.2f,  1.3f,  1.4f,  1.5f,  1.6f,  1.7f,  1.8f,  1.9f,  2.0f,  2.0f,  2.1f,  2.2f,  2.3f},
    /* 110°F */  { 0.6f,  0.7f,  0.9f,  1.0f,  1.1f,  1.3f,  1.4f,  1.5f,  1.6f,  1.8f,  1.9f,  2.0f,  2.1f,  2.3f,  2.4f,  2.5f,  2.6f,  2.7f,  2.8f,  2.9f},
    /* 120°F */  { 0.7f,  0.9f,  1.0f,  1.2f,  1.4f,  1.5f,  1.7f,  1.8f,  2.0f,  2.1f,  2.3f,  2.4f,  2.6f,  2.7f,  2.9f,  3.0f,  3.1f,  3.3f,  3.4f,  3.5f}
};

#endif // WOOD_TEMP_CORRECTION_DATA_H
