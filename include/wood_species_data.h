// include/wood_species_data.h
// Species-specific coefficients for the resistance-MC model
//   M = 10^(A + B * log10(R_kOhms))
//
// Mixed provenance (all expressed in the single-log firmware form above):
//   - Douglas-Fir, Ash, Walnut: A/B fitted by log-log regression of
//     FPL-GTR-6 (James 1988) Table 1 over 7..25 % MC. Table 1 transcribed from
//     the scan (42 Master thesis/9823.pdf, p.6), cross-checked cell by cell;
//     reproducible via scripts/fit_species_coefficients.py (local-only).
//     Single-log fit R2 0.989-0.997, max residual <= 2 % MC; this form fits
//     James's data slightly better than the Samuelsson double-log (issue #11).
//   - Oak (European), Beech (European): converted from Forsen & Tarvainen
//     (VTT Publications 420, 2000) Table 5 Central-Europe curves, which are in
//     the Samuelsson form log10(log10(R_MOhm)+1) = a*u + b (verified against
//     VTT's own Table 6 worked example). Re-fitted to the single-log firmware
//     form over 8..24 % MC by scripts/fit_vtt_european_coefficients.py
//     (local-only): R2 >= 0.993, max residual ~1.3 % MC. These are European-
//     stock curves, preferred over the American FPL seeds for the project's samples.
//     See docs/ai/2026-06-15-001-research-state-of-art-electrodes-models.md.
//
// CAVEATS (literature SEEDS / published curves, NOT this project's own M3 fit):
//   - FPL Table 1 resistance is at 80 F (26.7 C); the temperature-correction
//     grid (wood_temp_correction_data.h) is referenced to 70 F (21.1 C). Pairing
//     the two leaves a small (~0.5 % MC) systematic offset until the M3
//     regression replaces these with own-data coefficients.
//   - Douglas-Fir, Ash, Walnut are North American James-1988 species; VTT has
//     no curve for them. They remain FPL seeds; European stock will differ.
//   - VTT's specimens were conditioned at 8-18 % MC; 18-24 % is mild
//     extrapolation. Below ~8 % MC resistive sensing is unreliable for all
//     species (the dry-end "silent zone", an inherent method floor).

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
static const char SPECIES_NAME_1[] PROGMEM = "Oak (European)";
static const char SPECIES_NAME_2[] PROGMEM = "Ash, Black";
static const char SPECIES_NAME_3[] PROGMEM = "Walnut, Black";
static const char SPECIES_NAME_4[] PROGMEM = "Beech (European)";

// --- SPECIES DATA TABLE ---
// The 5 thesis species (Douglas-fir, Oak, Ash, Walnut, Beech). R must be in kOhms.
const WoodSpecies species_data[] PROGMEM = {
    // Name,           A,        B
    {SPECIES_NAME_0, 1.7003f, -0.12007f},   // Douglas-Fir (Coast)  FPL fit  R2 0.996
    {SPECIES_NAME_1, 1.6852f, -0.11368f},   // Oak (European)       VTT CE   R2 0.994 (a=-0.047 b=1.079)
    {SPECIES_NAME_2, 1.6213f, -0.11454f},   // Ash, Black           FPL fit  R2 0.989
    {SPECIES_NAME_3, 1.6676f, -0.11069f},   // Walnut, Black        FPL fit  R2 0.995
    {SPECIES_NAME_4, 1.6688f, -0.10258f}    // Beech (European)     VTT CE   R2 0.994 (a=-0.046 b=1.119)
};

const int NUM_WOOD_SPECIES = sizeof(species_data) / sizeof(WoodSpecies);

#endif // WOOD_SPECIES_DATA_H
