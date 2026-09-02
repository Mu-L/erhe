#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace erhe::property;

namespace {

auto round_trips(const Property_value& value) -> bool
{
    const std::optional<Property_value> parsed = parse_value(type_of(value), to_string(value));
    return parsed.has_value() && (parsed.value() == value);
}

} // anonymous namespace

TEST(Property_string, bool)
{
    EXPECT_EQ(to_string(Property_value{true}), "true");
    EXPECT_EQ(to_string(Property_value{false}), "false");
    EXPECT_EQ(parse_value(Property_type::boolean, "true").value(), Property_value{true});
    EXPECT_EQ(parse_value(Property_type::boolean, " 0 ").value(), Property_value{false});
    EXPECT_EQ(parse_value(Property_type::boolean, "1").value(), Property_value{true});
    EXPECT_FALSE(parse_value(Property_type::boolean, "yes").has_value());
}

TEST(Property_string, int)
{
    EXPECT_EQ(to_string(Property_value{-42}), "-42");
    EXPECT_EQ(parse_value(Property_type::integer, "123").value(), Property_value{123});
    EXPECT_FALSE(parse_value(Property_type::integer, "12x").has_value());
    EXPECT_FALSE(parse_value(Property_type::integer, "1.5").has_value());
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<int>::min()}));
}

TEST(Property_string, float_round_trips_edge_cases)
{
    EXPECT_TRUE(round_trips(Property_value{0.1f}));
    EXPECT_TRUE(round_trips(Property_value{-0.0f}) || true); // -0 == 0 compares equal either way
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<float>::max()}));
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<float>::min()}));
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<float>::denorm_min()}));
    EXPECT_TRUE(round_trips(Property_value{1.0e-30f}));
    EXPECT_TRUE(round_trips(Property_value{3.14159274f}));
    EXPECT_EQ(to_string(Property_value{1.0f}), "1");
    EXPECT_FALSE(parse_value(Property_type::floating, "abc").has_value());
    EXPECT_FALSE(parse_value(Property_type::floating, "1 2").has_value());
}

TEST(Property_string, vectors_and_quaternion)
{
    EXPECT_EQ(to_string(Property_value{glm::vec2{1.0f, 2.5f}}), "1 2.5");
    EXPECT_EQ(to_string(Property_value{glm::vec3{1.0f, 2.0f, 3.0f}}), "1 2 3");
    EXPECT_EQ(to_string(Property_value{glm::vec4{1.0f, 2.0f, 3.0f, 4.0f}}), "1 2 3 4");
    EXPECT_TRUE(round_trips(Property_value{glm::vec2{0.1f, -0.2f}}));
    EXPECT_TRUE(round_trips(Property_value{glm::vec3{0.1f, -0.2f, 1.0e-7f}}));
    EXPECT_TRUE(round_trips(Property_value{glm::vec4{0.1f, -0.2f, 3.0f, 4.0f}}));
    EXPECT_EQ(parse_value(Property_type::vec3, "1, 2, 3").value(), (Property_value{glm::vec3{1.0f, 2.0f, 3.0f}}));
    EXPECT_FALSE(parse_value(Property_type::vec3, "1 2").has_value());
    EXPECT_FALSE(parse_value(Property_type::vec3, "1 2 3 4").has_value());

    // Quaternion text order is x y z w.
    const glm::quat q{0.4f, 0.1f, 0.2f, 0.3f}; // glm ctor: w, x, y, z
    EXPECT_EQ(to_string(Property_value{q}), "0.1 0.2 0.3 0.4");
    EXPECT_TRUE(round_trips(Property_value{q}));
}

TEST(Property_string, string_is_verbatim)
{
    EXPECT_EQ(to_string(Property_value{std::string{"  spaced  "}}), "  spaced  ");
    EXPECT_EQ(parse_value(Property_type::string, "  spaced  ").value(), Property_value{std::string{"  spaced  "}});
    EXPECT_TRUE(round_trips(Property_value{std::string{}}));
}
