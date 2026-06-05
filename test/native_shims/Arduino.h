// test/native_shims/Arduino.h
// Minimal off-target stand-in for the Arduino core, used ONLY by the [env:native]
// unit tests (its directory is on that env's include path; the embedded build
// never sees it). It supplies just enough of the Arduino surface for the
// calibration data headers (wood_species_data.h, wood_temp_correction_data.h)
// and wood_mc_math.h to compile and read their PROGMEM tables as plain RAM.
//
// Keep this tiny. It is not an Arduino emulator - if a test ever needs more of
// the core, prefer testing pure functions that do not depend on it.

#ifndef NATIVE_SHIM_ARDUINO_H
#define NATIVE_SHIM_ARDUINO_H

#include <cstdint>
#include <cmath>

// PROGMEM is a no-op off-target; the tables become ordinary const arrays.
#ifndef PROGMEM
#define PROGMEM
#endif

// pgm_read_float just dereferences; the data lives in normal memory natively.
#ifndef pgm_read_float
#define pgm_read_float(addr) (*(reinterpret_cast<const float *>(addr)))
#endif

// memcpy_P is a plain memcpy off-target (PROGMEM is ordinary RAM here).
#include <cstring>
#ifndef memcpy_P
#define memcpy_P(dest, src, n) memcpy((dest), (src), (n))
#endif

// The math uses log10/pow; pull them into the global namespace like the core does.
using std::log10;
using std::pow;

#endif // NATIVE_SHIM_ARDUINO_H
