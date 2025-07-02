// include/wood_species_data.h
// Stores species-specific coefficients for moisture content calculation based on FPL GTR-06.
// M = 10^(A + B * log10(R_kOhms))

#ifndef WOOD_SPECIES_DATA_H
#define WOOD_SPECIES_DATA_H

#include <Arduino.h>

struct WoodSpecies {
    const char* name;
    float A; // Coefficient A
    float B; // Coefficient B
};

// --- SPECIES DATA TABLE ---
// Add more species here. Data can be found in FPL GTR-06, Table 1, or other reliable sources.
// Ensure R is in kOhms when using these coefficients.
// The constants below are illustrative examples and might need verification/averaging from the source document.
const WoodSpecies species_data[] PROGMEM = {
    // Name, A, B
    {"Douglas-Fir (Coast)", 1.725f, -0.02820f},      // Example from FPL GTR-06 (average values, check table for specifics)
    {"Pine, Southern Yellow (Loblolly)", 1.806f, -0.02994f}, // Example
    {"Spruce, Sitka", 1.621f, -0.02641f},            // Example
    {"Oak, Red", 1.912f, -0.03218f},                // Example
    {"Maple, Sugar", 1.850f, -0.03100f},             // Example (Hypothetical - verify from sources)
    // Add other common species you might work with.
    // For "Generic Hardwood" or "Generic Softwood", an average can be used but accuracy will be lower.
    {"Generic Softwood (Avg)", 1.700f, -0.0270f}, // Generic placeholder
    {"Generic Hardwood (Avg)", 1.850f, -0.0310f}  // Generic placeholder
};

const int NUM_WOOD_SPECIES = sizeof(species_data) / sizeof(WoodSpecies);

#endif // WOOD_SPECIES_DATA_H
