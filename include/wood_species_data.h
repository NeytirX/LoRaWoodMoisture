// include/wood_species_data.h
// Stores species-specific coefficients for moisture content calculation based on FPL GTR-06.
// M = 10^(A + B * log10(R_kOhms))

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
static const char SPECIES_NAME_1[] PROGMEM = "Pine, Southern Yellow (Loblolly)";
static const char SPECIES_NAME_2[] PROGMEM = "Spruce, Sitka";
static const char SPECIES_NAME_3[] PROGMEM = "Oak, Red";
static const char SPECIES_NAME_4[] PROGMEM = "Maple, Sugar";
static const char SPECIES_NAME_5[] PROGMEM = "Generic Softwood (Avg)";
static const char SPECIES_NAME_6[] PROGMEM = "Generic Hardwood (Avg)";

// --- SPECIES DATA TABLE ---
// Data sourced from FPL GTR-06, Table 1 (or similar references).
// R must be in kOhms when using these coefficients.
// The constants below are illustrative examples - verify against the original document.
const WoodSpecies species_data[] PROGMEM = {
    // Name,                              A,       B
    {SPECIES_NAME_0, 1.725f, -0.02820f},  // Douglas-Fir (Coast)
    {SPECIES_NAME_1, 1.806f, -0.02994f},  // Pine, Southern Yellow (Loblolly)
    {SPECIES_NAME_2, 1.621f, -0.02641f},  // Spruce, Sitka
    {SPECIES_NAME_3, 1.912f, -0.03218f},  // Oak, Red
    {SPECIES_NAME_4, 1.850f, -0.03100f},  // Maple, Sugar (hypothetical - verify)
    {SPECIES_NAME_5, 1.700f, -0.0270f},   // Generic Softwood
    {SPECIES_NAME_6, 1.850f, -0.0310f}    // Generic Hardwood
};

const int NUM_WOOD_SPECIES = sizeof(species_data) / sizeof(WoodSpecies);

#endif // WOOD_SPECIES_DATA_H
