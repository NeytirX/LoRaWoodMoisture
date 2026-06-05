// include/wood_species_data.h
// Species-specific coefficients for the resistance-MC model
//   M = 10^(A + B * log10(R_kOhms))
//
// Source: A/B fitted by log-log regression of FPL-GTR-6 (James 1988) Table 1
// resistance-vs-MC data over 7..25 % MC. Table 1 values transcribed from the
// scan (42 Master thesis/9823.pdf, PDF page 6) and cross-checked cell by cell;
// the fit is reproducible via scripts/fit_species_coefficients.py (local-only).
// Single-log fit quality: R2 0.989-0.997, max residual <= 2 % MC; this form
// fits James's data slightly better than the Samuelsson double-log alternative,
// so calculate_indicated_mc() keeps the single-log form (see issue #11).
//
// CAVEATS (these are literature SEEDS, not this project's calibration):
//   - Table 1 resistance is measured at 80 F (26.7 C); the temperature-
//     correction grid (wood_temp_correction_data.h) is referenced to 70 F
//     (21.1 C). Pairing the two leaves a small (~0.5 % MC) systematic offset
//     until the M3 regression replaces these with own-data coefficients.
//   - These are the North American species in James 1988; Bella's European
//     stock (esp. oak/ash/walnut) will differ. Seeds, not ground truth.
//   - Beech is NOT in James 1988 Table 1: index 5 is a hardwood-average
//     placeholder. Replace with a European beech source (e.g. Forsen &
//     Tarvainen 2000) or the M3 regression before trusting beech readings.

#ifndef WOOD_SPECIES_DATA_H
#define WOOD_SPECIES_DATA_H

#include <Arduino.h>

struct WoodSpecies {
    const char* name;  // PROGMEM string pointer
    float A;           // Coefficient A
    float B;           // Coefficient B
};

// --- Species Name Strings (stored in PROGMEM) ---
// String literals must be declared separately so both the pointer AND the string
// live in flash, not just the pointer.
static const char SPECIES_NAME_0[] PROGMEM = "Douglas-Fir (Coast)";
static const char SPECIES_NAME_1[] PROGMEM = "Oak, White";
static const char SPECIES_NAME_2[] PROGMEM = "Oak, Northern Red";
static const char SPECIES_NAME_3[] PROGMEM = "Ash, Black";
static const char SPECIES_NAME_4[] PROGMEM = "Walnut, Black";
static const char SPECIES_NAME_5[] PROGMEM = "Beech (placeholder)";

// --- SPECIES DATA TABLE ---
// Thesis species (Douglas-fir, Oak, Ash, Walnut, Beech). R must be in kOhms.
const WoodSpecies species_data[] PROGMEM = {
    // Name,           A,        B          // FPL-GTR-6 Table 1 fit, 7..25 % MC
    {SPECIES_NAME_0, 1.7003f, -0.12007f},   // Douglas-Fir (Coast)  R2 0.996
    {SPECIES_NAME_1, 1.6691f, -0.11800f},   // Oak, White           R2 0.992
    {SPECIES_NAME_2, 1.7078f, -0.12209f},   // Oak, Northern Red    R2 0.997 (veneer data)
    {SPECIES_NAME_3, 1.6213f, -0.11454f},   // Ash, Black           R2 0.989
    {SPECIES_NAME_4, 1.6676f, -0.11069f},   // Walnut, Black        R2 0.995
    {SPECIES_NAME_5, 1.6665f, -0.11633f}    // Beech: hardwood-average placeholder (NOT in James 1988)
};

const int NUM_WOOD_SPECIES = sizeof(species_data) / sizeof(WoodSpecies);

#endif // WOOD_SPECIES_DATA_H
