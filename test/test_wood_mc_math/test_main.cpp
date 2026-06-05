// test/test_wood_mc_math/test_main.cpp
// Off-target invariant tests for the pure MC math in include/wood_mc_math.h.
// Run on the host: pio test -e native
//
// Scope (deliberate): only invariants that hold for ANY sane calibration data,
// so the suite survives the M3 recalibration of wood_species_data.h and
// wood_temp_correction_data.h (issues.md #9/#11). Golden-value tests pinning
// the calibrated coefficients and the re-transcribed temperature table are
// added at M3 (next_steps.md item 4). The one physics-direction test is
// TEST_IGNOREd until the table re-transcription lands (issues.md #9).

#include <unity.h>
#include "wood_mc_math.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// calculate_indicated_mc - synthetic species, real functional form
// ---------------------------------------------------------------------------

// Synthetic coefficients with realistic magnitude (|B| ~ 0.1, issues.md #11).
// Every test below must hold for any A and any B < 0.
static const WoodSpecies kSynthetic = {"synthetic", 1.6f, -0.1f};

static void test_mc_rejects_nonpositive_resistance(void) {
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, wood_mc::calculate_indicated_mc(0.0f, kSynthetic));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, wood_mc::calculate_indicated_mc(-100.0f, kSynthetic));
}

static void test_mc_clamps_short_circuit_wet(void) {
    // sensor.h returns 1e-3 ohms on a near-zero ADC read (short circuit);
    // 1 ohm is the documented clamp boundary (R_kOhms <= 1e-3)
    TEST_ASSERT_EQUAL_FLOAT(250.0f, wood_mc::calculate_indicated_mc(1.0e-3f, kSynthetic));
    TEST_ASSERT_EQUAL_FLOAT(250.0f, wood_mc::calculate_indicated_mc(1.0f, kSynthetic));
}

static void test_mc_clamps_open_circuit_dry(void) {
    // sensor.h returns 1e12 ohms on a saturated ADC read (open circuit)
    TEST_ASSERT_EQUAL_FLOAT(5.0f, wood_mc::calculate_indicated_mc(1.0e12f, kSynthetic));
}

static void test_mc_monotonically_decreasing_in_resistance(void) {
    float prev = wood_mc::calculate_indicated_mc(1.0e4f, kSynthetic);
    for (float r = 1.0e5f; r <= 1.0e9f; r *= 10.0f) {
        float mc = wood_mc::calculate_indicated_mc(r, kSynthetic);
        TEST_ASSERT_TRUE_MESSAGE(mc < prev, "MC must fall as resistance rises (B < 0)");
        prev = mc;
    }
}

static void test_mc_matches_closed_form(void) {
    // M = 10^(A + B*log10(R_kOhms)); at R = 1 kOhm the log term vanishes: M = 10^A
    TEST_ASSERT_FLOAT_WITHIN(0.01f, pow(10.0f, 1.6f),
                             wood_mc::calculate_indicated_mc(1000.0f, kSynthetic));
}

// ---------------------------------------------------------------------------
// bilinear_interpolation - synthetic grids (the function signature fixes the
// column count at MC_POINTS_COUNT, so the synthetic grids reuse it)
// ---------------------------------------------------------------------------

static float syn_x[3];
static float syn_y[MC_POINTS_COUNT];
static float syn_plane[3][MC_POINTS_COUNT];

static void build_plane(void) {
    for (int i = 0; i < 3; i++) syn_x[i] = 10.0f * i;              // 0, 10, 20
    for (int j = 0; j < MC_POINTS_COUNT; j++) syn_y[j] = (float)j; // 0..19
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < MC_POINTS_COUNT; j++)
            syn_plane[i][j] = syn_x[i] + syn_y[j];                 // f(x,y) = x + y
}

static void test_bilinear_reproduces_plane_at_nodes(void) {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < MC_POINTS_COUNT; j++)
            TEST_ASSERT_FLOAT_WITHIN(1e-4f, syn_x[i] + syn_y[j],
                wood_mc::bilinear_interpolation(syn_x[i], syn_y[j],
                    syn_x, 3, syn_y, MC_POINTS_COUNT, syn_plane));
}

static void test_bilinear_is_exact_on_plane_interior(void) {
    // bilinear interpolation reproduces a plane exactly, between nodes too
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 7.3f + 4.6f,
        wood_mc::bilinear_interpolation(7.3f, 4.6f,
            syn_x, 3, syn_y, MC_POINTS_COUNT, syn_plane));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 15.0f + 18.5f,
        wood_mc::bilinear_interpolation(15.0f, 18.5f,
            syn_x, 3, syn_y, MC_POINTS_COUNT, syn_plane));
}

static void test_bilinear_degenerate_cell_returns_corner(void) {
    static float deg_x[2] = {5.0f, 5.0f}; // x1 == x2 -> guard returns q11
    TEST_ASSERT_EQUAL_FLOAT(syn_plane[0][0],
        wood_mc::bilinear_interpolation(5.0f, 0.5f,
            deg_x, 2, syn_y, MC_POINTS_COUNT, syn_plane));
}

// ---------------------------------------------------------------------------
// get_temperature_correction - real table, sign-agnostic invariants only
// ---------------------------------------------------------------------------

static void test_correction_zero_at_reference_70f(void) {
    // 70 F = 21.1 C is the meter calibration reference; the correction must
    // vanish there for any indicated MC, whatever the table's sign convention.
    const float t_ref_c = (70.0f - 32.0f) * 5.0f / 9.0f;
    for (float mc = 6.0f; mc <= 25.0f; mc += 1.0f)
        TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f,
                                 wood_mc::get_temperature_correction(mc, t_ref_c));
}

static void test_correction_clamps_below_table_min_temp(void) {
    // below the first table row the correction is held constant, not extrapolated
    const float t_min_c = (0.0f - 32.0f) * 5.0f / 9.0f;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,
        wood_mc::get_temperature_correction(15.0f, t_min_c),
        wood_mc::get_temperature_correction(15.0f, -40.0f));
}

static void test_correction_clamps_above_table_max_temp(void) {
    const float t_max_c = (120.0f - 32.0f) * 5.0f / 9.0f;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,
        wood_mc::get_temperature_correction(15.0f, t_max_c),
        wood_mc::get_temperature_correction(15.0f, 80.0f));
}

static void test_correction_clamps_mc_outside_columns(void) {
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,
        wood_mc::get_temperature_correction(6.0f, 5.0f),
        wood_mc::get_temperature_correction(2.0f, 5.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,
        wood_mc::get_temperature_correction(25.0f, 5.0f),
        wood_mc::get_temperature_correction(40.0f, 5.0f));
}

static void test_correction_direction_matches_physics(void) {
    // issues.md #9: the shipped table's signs are suspected inverted.
    // Enable in the same commit as the FPL-GTR-6 re-transcription.
    TEST_IGNORE_MESSAGE("blocked on issues.md #9 - enable after FPL-GTR-6 re-transcription");
    TEST_ASSERT_TRUE(wood_mc::get_temperature_correction(15.0f, 0.0f) > 0.0f);  // cold reads low -> add
    TEST_ASSERT_TRUE(wood_mc::get_temperature_correction(15.0f, 45.0f) < 0.0f); // warm reads high -> subtract
}

int main(int, char**) {
    build_plane();
    UNITY_BEGIN();
    RUN_TEST(test_mc_rejects_nonpositive_resistance);
    RUN_TEST(test_mc_clamps_short_circuit_wet);
    RUN_TEST(test_mc_clamps_open_circuit_dry);
    RUN_TEST(test_mc_monotonically_decreasing_in_resistance);
    RUN_TEST(test_mc_matches_closed_form);
    RUN_TEST(test_bilinear_reproduces_plane_at_nodes);
    RUN_TEST(test_bilinear_is_exact_on_plane_interior);
    RUN_TEST(test_bilinear_degenerate_cell_returns_corner);
    RUN_TEST(test_correction_zero_at_reference_70f);
    RUN_TEST(test_correction_clamps_below_table_min_temp);
    RUN_TEST(test_correction_clamps_above_table_max_temp);
    RUN_TEST(test_correction_clamps_mc_outside_columns);
    RUN_TEST(test_correction_direction_matches_physics);
    return UNITY_END();
}
