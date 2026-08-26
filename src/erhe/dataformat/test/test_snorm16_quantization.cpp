#include "erhe_dataformat/dataformat.hpp"

#include <meshoptimizer.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>

// The AABB vertex position encoding routes its snorm16 quantization through
// meshopt_quantizeSnorm() (primitive.cpp build_buffer_mesh_from_triangle_soup()
// and primitive_builder.cpp Build_context::write_position()) instead of
// erhe::dataformat::float_to_snorm16(). Both encode sites clamp the encoded
// position to [-1, 1] first, and on that domain the two must agree bit for bit:
// same 32767 scale, same round-half-away-from-zero, same truncating cast.
//
// They are NOT interchangeable outside [-1, 1] - float_to_snorm16() clamps after
// scaling while meshopt_quantizeSnorm() clamps before - which is exactly why the
// encode sites clamp, and why that is asserted here too.

namespace {

[[nodiscard]] auto meshopt_snorm16(const float v) -> int16_t
{
    return static_cast<int16_t>(meshopt_quantizeSnorm(v, 16));
}

} // anonymous namespace

TEST(Snorm16_quantization, matches_meshopt_on_the_encoded_domain)
{
    // Dense sweep of [-1, 1]. The step is deliberately not a power of two so the
    // sweep lands on values that are not exactly representable.
    constexpr int steps = 200001;
    for (int i = 0; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps - 1);
        const float v = -1.0f + 2.0f * t;
        ASSERT_EQ(erhe::dataformat::float_to_snorm16(v), meshopt_snorm16(v)) << "v = " << v;
    }
}

TEST(Snorm16_quantization, matches_meshopt_at_the_rounding_boundaries)
{
    // Around every half-step, where round-half-away-from-zero is decided. The
    // division below rounds, so v lands NEAR the tie rather than on it - bracket
    // it with the two adjacent representable floats instead, which straddles the
    // decision point whichever side the division fell on.
    for (int q = -32767; q <= 32766; ++q) {
        const float v = (static_cast<float>(q) + 0.5f) / 32767.0f;
        const float candidates[3] = { std::nextafter(v, -2.0f), v, std::nextafter(v, 2.0f) };
        for (const float candidate : candidates) {
            ASSERT_EQ(erhe::dataformat::float_to_snorm16(candidate), meshopt_snorm16(candidate)) << "q = " << q;
        }
    }
    EXPECT_EQ(erhe::dataformat::float_to_snorm16(-1.0f), meshopt_snorm16(-1.0f));
    EXPECT_EQ(erhe::dataformat::float_to_snorm16( 1.0f), meshopt_snorm16( 1.0f));
    EXPECT_EQ(erhe::dataformat::float_to_snorm16( 0.0f), meshopt_snorm16( 0.0f));
    EXPECT_EQ(erhe::dataformat::float_to_snorm16(-0.0f), meshopt_snorm16(-0.0f));
}

TEST(Snorm16_quantization, spans_the_full_snorm16_range)
{
    // A regression guard on the scale: both ends must reach +-32767, so the
    // encoded AABB corners survive the round trip.
    EXPECT_EQ(meshopt_snorm16( 1.0f),  32767);
    EXPECT_EQ(meshopt_snorm16(-1.0f), -32767);
    EXPECT_EQ(erhe::dataformat::float_to_snorm16( 1.0f),  32767);
    EXPECT_EQ(erhe::dataformat::float_to_snorm16(-1.0f), -32767);
}

TEST(Snorm16_quantization, the_two_differ_outside_the_encoded_domain)
{
    // Documents why both encode sites clamp before quantizing: without the clamp
    // the switch to meshopt_quantizeSnorm() would not be a no-op. float_to_snorm16()
    // scales first and saturates at -32768; meshopt_quantizeSnorm() clamps the
    // input and so saturates at -32767.
    EXPECT_EQ(erhe::dataformat::float_to_snorm16(-2.0f), -32768);
    EXPECT_EQ(meshopt_snorm16(-2.0f), -32767);
}

TEST(Snorm16_quantization, meshopt_maps_nan_into_range)
{
    // NaN passes through both clamps, and meshopt_quantizeSnorm() then maps it to
    // the AABB's minimum corner rather than failing - where the old convert() path
    // aborted on its range check. This is why both encode sites verify that the
    // position is finite before quantizing; the test pins the behavior that makes
    // those verifies necessary.
    const float nan_value = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(meshopt_snorm16(nan_value), -32767);
}
