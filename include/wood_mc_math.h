// include/wood_mc_math.h
// Single home of the pure moisture-content math (FPL GTR-6), used by main.cpp
// and the native tests.
//
// Why this header exists
// ----------------------
// This header is the single home of the pure moisture-content math —
// calculate_indicated_mc(), get_temperature_correction() and
// bilinear_interpolation() — as free functions in the `wood_mc` namespace.
// src/main.cpp includes this header and calls wood_mc::* rather than carrying its
// own copies, so the deployed firmware runs exactly the code the `native` test
// suite exercises. (Until 2026-07-02 the math was duplicated in main.cpp and this
// header was a hand-kept mirror of it; that duplication has been removed.)
//
// The functions read the PROGMEM tables in include/wood_species_data.h and
// include/wood_temp_correction_data.h (the single source of calibration truth).
// Being namespaced, stateless free functions, they are safe to include from any
// translation unit — both main.cpp (Arduino/RadioLib/PMIC) and the native tests,
// none of which the pure math depends on.
//
// Native build shims
// ------------------
// On-target, Arduino.h supplies PROGMEM / pgm_read_float / constrain. Off-target
// there is no such header, so we provide no-op shims guarded by NATIVE_BUILD
// (set by the [env:native] build_flags). On the ESP32 these are never defined
// here because Arduino.h has already defined them.

#ifndef WOOD_MC_MATH_H
#define WOOD_MC_MATH_H

#ifdef NATIVE_BUILD
// --- off-target shims: make the PROGMEM tables plain RAM arrays ---
// The data headers (wood_species_data.h / wood_temp_correction_data.h) and
// config.h all `#include <Arduino.h>`; the native test env supplies a stub
// Arduino.h (test/native_shims/) that defines PROGMEM / pgm_read_float and
// pulls in <cmath>. constrain() is not in that stub, so define it here.
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
#endif

#include "wood_species_data.h"
#include "wood_temp_correction_data.h"

// On-target this is defined in config.h (which main.cpp includes before any of
// this code runs). We deliberately do NOT include config.h here: config.h pulls
// in <Arduino.h>, the LoRaWAN region defines, and the gitignored lorawan_keys.h,
// none of which the math needs and the gitignored keys would make the native
// test non-portable. Mirror the config.h default so the header is self-contained.
#ifndef ENABLE_TEMPERATURE_COMPENSATION
#define ENABLE_TEMPERATURE_COMPENSATION true
#endif

namespace wood_mc {

// Indicated MC from wood resistance, per species coefficients:
//   M = 10^(A + B * log10(R_kOhms))
inline float calculate_indicated_mc(float R_wood_ohms, const WoodSpecies& species) {
    if (R_wood_ohms <= 0) return -1.0f;
    float R_kOhms = R_wood_ohms / 1000.0f;
    if (R_kOhms <= 1e-3f) return 250.0f;
    if (R_kOhms >= 1e9f)  return 5.0f;
    float log10_R_kOhms = log10(R_kOhms);
    return pow(10, species.A + (species.B * log10_R_kOhms));
}

// Bilinear interpolation over the temperature-correction grid.
inline float bilinear_interpolation(float x, float y,
                                    const float x_points[], int x_count,
                                    const float y_points[], int y_count,
                                    const float table[][MC_POINTS_COUNT]) {
    int x_idx = 0;
    while (x_idx < x_count - 2 && x > pgm_read_float(&x_points[x_idx + 1])) x_idx++;
    int y_idx = 0;
    while (y_idx < y_count - 2 && y > pgm_read_float(&y_points[y_idx + 1])) y_idx++;

    float x1  = pgm_read_float(&x_points[x_idx]);
    float x2  = pgm_read_float(&x_points[x_idx + 1]);
    float y1  = pgm_read_float(&y_points[y_idx]);
    float y2  = pgm_read_float(&y_points[y_idx + 1]);
    float q11 = pgm_read_float(&table[x_idx][y_idx]);
    float q12 = pgm_read_float(&table[x_idx][y_idx + 1]);
    float q21 = pgm_read_float(&table[x_idx + 1][y_idx]);
    float q22 = pgm_read_float(&table[x_idx + 1][y_idx + 1]);

    if ((x2 - x1) == 0 || (y2 - y1) == 0) return q11;

    float r1 = ((x2 - x) / (x2 - x1)) * q11 + ((x - x1) / (x2 - x1)) * q21;
    float r2 = ((x2 - x) / (x2 - x1)) * q12 + ((x - x1) / (x2 - x1)) * q22;
    return ((y2 - y) / (y2 - y1)) * r1 + ((y - y1) / (y2 - y1)) * r2;
}

// Temperature correction C_t (% MC) to add to the indicated MC.
inline float get_temperature_correction(float indicated_mc, float wood_temp_celsius) {
    if (!ENABLE_TEMPERATURE_COMPENSATION) return 0.0f;

    // The correction grid is in Celsius (matches the DS18B20), no conversion.
    float first_temp = pgm_read_float(&temp_points_c[0]);
    float last_temp  = pgm_read_float(&temp_points_c[TEMP_POINTS_COUNT - 1]);
    float first_mc   = pgm_read_float(&mc_points_indicated[0]);
    float last_mc    = pgm_read_float(&mc_points_indicated[MC_POINTS_COUNT - 1]);

    float wood_temp = constrain(wood_temp_celsius, first_temp, last_temp);
    indicated_mc    = constrain(indicated_mc, first_mc, last_mc);

    return bilinear_interpolation(wood_temp, indicated_mc,
                                  temp_points_c, TEMP_POINTS_COUNT,
                                  mc_points_indicated, MC_POINTS_COUNT,
                                  correction_table);
}

} // namespace wood_mc

#endif // WOOD_MC_MATH_H
