// include/wood_mc_math.h
// Off-target-testable copy of the pure moisture-content math (FPL GTR-6).
//
// Why this header exists
// ----------------------
// The authoritative copies of calculate_indicated_mc(), get_temperature_correction()
// and bilinear_interpolation() live in src/main.cpp. That file is the single
// firmware translation unit and depends on Arduino, RadioLib, the PMIC, NVS, etc.,
// none of which build for the `native` test platform. To unit-test the math
// without hardware, the *pure* portion is mirrored here as free functions in the
// `wood_mc` namespace.
//
// The functions read the same PROGMEM tables in include/wood_species_data.h and
// include/wood_temp_correction_data.h (the single source of calibration truth),
// so the tables themselves are never duplicated - only the arithmetic is. The
// arithmetic here is a line-for-line transcription of main.cpp; if you change one,
// change both. A future cleanup can collapse this duplication by having main.cpp
// include this header and call wood_mc::* (it cannot today without editing
// main.cpp). The namespace keeps these names from colliding with main.cpp's
// global-scope copies, so this header is safe to include from any TU.
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
// Transcribed from src/main.cpp::calculate_indicated_mc.
inline float calculate_indicated_mc(float R_wood_ohms, const WoodSpecies& species) {
    if (R_wood_ohms <= 0) return -1.0f;
    float R_kOhms = R_wood_ohms / 1000.0f;
    if (R_kOhms <= 1e-3f) return 250.0f;
    if (R_kOhms >= 1e9f)  return 5.0f;
    float log10_R_kOhms = log10(R_kOhms);
    return pow(10, species.A + (species.B * log10_R_kOhms));
}

// Bilinear interpolation over the temperature-correction grid.
// Transcribed from src/main.cpp::bilinear_interpolation.
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
// Transcribed from src/main.cpp::get_temperature_correction.
inline float get_temperature_correction(float indicated_mc, float wood_temp_celsius) {
    if (!ENABLE_TEMPERATURE_COMPENSATION) return 0.0f;

    float wood_temp_f = (wood_temp_celsius * 9.0f / 5.0f) + 32.0f;

    float first_temp = pgm_read_float(&temp_points_f[0]);
    float last_temp  = pgm_read_float(&temp_points_f[TEMP_POINTS_COUNT - 1]);
    float first_mc   = pgm_read_float(&mc_points_indicated[0]);
    float last_mc    = pgm_read_float(&mc_points_indicated[MC_POINTS_COUNT - 1]);

    wood_temp_f  = constrain(wood_temp_f, first_temp, last_temp);
    indicated_mc = constrain(indicated_mc, first_mc, last_mc);

    return bilinear_interpolation(wood_temp_f, indicated_mc,
                                  temp_points_f, TEMP_POINTS_COUNT,
                                  mc_points_indicated, MC_POINTS_COUNT,
                                  correction_table);
}

} // namespace wood_mc

#endif // WOOD_MC_MATH_H
