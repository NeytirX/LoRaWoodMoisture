// test/test_wood_mc_math/test_main.cpp
// Off-target invariant tests for the pure MC math in include/wood_mc_math.h.
// Run on the host: pio test -e native
//
// Scope (deliberate): invariants that hold for ANY sane calibration data (so
// the suite survives the M3 recalibration of wood_species_data.h and
// wood_temp_correction_data.h, issues.md #9/#11), plus goldens pinning the
// current tables - update those when the tables change. The physics-direction test
// runs (enabled 2026-06-05, issues.md #9 resolved), golden cells pin the
// digitized temperature table, and test_mc_real_species_golden pins the
// Douglas-Fir/Oak/Beech coefficients; only Ash and Walnut remain unpinned
// until the M3 own-data regression (issues.md #11).

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

static void test_mc_real_species_golden(void) {
    // Golden values pin the shipped coefficients (wood_species_data.h).
    // Index 0 = Douglas-Fir (Coast), FPL-GTR-6 fit, A=1.7003 B=-0.12007.
    // Round-trips the table: 1 MOhm -> ~22 %, 10 MOhm -> ~16.6 % MC.
    WoodSpecies douglas;
    memcpy_P(&douglas, &species_data[0], sizeof(WoodSpecies));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 21.88f, wood_mc::calculate_indicated_mc(1.0e6f, douglas));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 16.60f, wood_mc::calculate_indicated_mc(1.0e7f, douglas));

    // Index 1 = Oak (European), VTT 2000 Table 5 CE curve re-fit, A=1.6852 B=-0.11368.
    WoodSpecies oak_eu;
    memcpy_P(&oak_eu, &species_data[1], sizeof(WoodSpecies));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 22.08f, wood_mc::calculate_indicated_mc(1.0e6f, oak_eu));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 17.00f, wood_mc::calculate_indicated_mc(1.0e7f, oak_eu));

    // Index 4 = Beech (European), VTT 2000 Table 5 CE curve re-fit, A=1.6688 B=-0.10258.
    WoodSpecies beech_eu;
    memcpy_P(&beech_eu, &species_data[4], sizeof(WoodSpecies));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 22.97f, wood_mc::calculate_indicated_mc(1.0e6f, beech_eu));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 18.13f, wood_mc::calculate_indicated_mc(1.0e7f, beech_eu));
    // Every shipped species must have a physically sane slope (B in -0.13..-0.10).
    for (int i = 0; i < NUM_WOOD_SPECIES; i++) {
        WoodSpecies s;
        memcpy_P(&s, &species_data[i], sizeof(WoodSpecies));
        TEST_ASSERT_TRUE_MESSAGE(s.B < -0.10f && s.B > -0.13f, "species B out of sane range");
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

static void test_correction_near_zero_at_reference_21c(void) {
    // 70 F = 21.1 C is the meter calibration reference; the correction must
    // be ~zero there for any indicated MC (interpolated between the 20 C and
    // 25 C rows, so not exactly zero).
    const float t_ref_c = (70.0f - 32.0f) * 5.0f / 9.0f;
    for (float mc = 6.0f; mc <= 25.0f; mc += 1.0f)
        TEST_ASSERT_FLOAT_WITHIN(0.25f, 0.0f,
                                 wood_mc::get_temperature_correction(mc, t_ref_c));
}

static void test_correction_clamps_below_table_min_temp(void) {
    // below the first table row (-20 C) the correction is held constant
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,
        wood_mc::get_temperature_correction(15.0f, -20.0f),
        wood_mc::get_temperature_correction(15.0f, -40.0f));
}

static void test_correction_clamps_above_table_max_temp(void) {
    // above the last table row (50 C) the correction is held constant
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,
        wood_mc::get_temperature_correction(15.0f, 50.0f),
        wood_mc::get_temperature_correction(15.0f, 80.0f));
}

static void test_correction_golden_cells(void) {
    // Golden values against the current digitized table (FPL-GTR-6 Figure 5,
    // 2026-06-05). At exact grid nodes bilinear interpolation returns the
    // cell itself; update these only when the table is re-derived.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  6.2f, wood_mc::get_temperature_correction(15.0f, -10.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -4.4f, wood_mc::get_temperature_correction(20.0f,  50.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  1.9f, wood_mc::get_temperature_correction( 6.0f,   0.0f));
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
    // Enabled 2026-06-05 with the table re-digitized from FPL-GTR-6 Figure 5
    // (closes issues.md #9): wood resistance falls as temperature rises, so a
    // meter calibrated at 21 C reads low on cold wood and high on warm wood.
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
    RUN_TEST(test_mc_real_species_golden);
    RUN_TEST(test_mc_matches_closed_form);
    RUN_TEST(test_bilinear_reproduces_plane_at_nodes);
    RUN_TEST(test_bilinear_is_exact_on_plane_interior);
    RUN_TEST(test_bilinear_degenerate_cell_returns_corner);
    RUN_TEST(test_correction_near_zero_at_reference_21c);
    RUN_TEST(test_correction_clamps_below_table_min_temp);
    RUN_TEST(test_correction_clamps_above_table_max_temp);
    RUN_TEST(test_correction_clamps_mc_outside_columns);
    RUN_TEST(test_correction_golden_cells);
    RUN_TEST(test_correction_direction_matches_physics);
    return UNITY_END();
}
