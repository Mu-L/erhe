#include "test_object.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

enum class Color_mode : int {
    rgb  = 0,
    hsv  = 1,
    gray = 5
};

constexpr Enum_entry c_color_mode_entries[] = {
    {"RGB",  static_cast<int32_t>(Color_mode::rgb)},
    {"HSV",  static_cast<int32_t>(Color_mode::hsv)},
    {"Gray", static_cast<int32_t>(Color_mode::gray)},
};
const Enum_info c_color_mode_info{"Color_mode", c_color_mode_entries};

const Property<Color_mode> enum_mode = Property<Color_mode>::register_property("enum_mode", type_a(), c_color_mode_info);
const Property<Color_mode> enum_mode_default = Property<Color_mode>::register_property(
    "enum_mode_default", type_a(), c_color_mode_info, Property_metadata{.default_value = make_value(Color_mode::gray)}
);
const Property<Color_mode> enum_mode_attached = Property<Color_mode>::register_attached(
    "enum_mode_attached", type_c(), c_color_mode_info, Property_metadata{.default_value = make_value(Color_mode::hsv)}
);

} // anonymous namespace

TEST(Enum_property, registration_and_table)
{
    EXPECT_EQ(enum_mode.get().get_type(), Property_type::enumeration);
    ASSERT_NE(enum_mode.get().get_enum_info(), nullptr);
    EXPECT_EQ(enum_mode.get().get_enum_info()->get_type_name(), "Color_mode");
    EXPECT_EQ(c_color_mode_info.label_for(5), "Gray");
    EXPECT_EQ(c_color_mode_info.label_for(2), "");
    EXPECT_EQ(c_color_mode_info.value_for("HSV").value(), 1);
    EXPECT_FALSE(c_color_mode_info.value_for("nope").has_value());
    EXPECT_TRUE(c_color_mode_info.contains(5));
    EXPECT_FALSE(c_color_mode_info.contains(3));
    EXPECT_EQ(c_color_mode_info.index_of(5).value(), std::size_t{2});
}

TEST(Enum_property, defaults)
{
    Test_object o;
    EXPECT_EQ(o.get_value(enum_mode), Color_mode::rgb);          // first table entry
    EXPECT_EQ(o.get_value(enum_mode_default), Color_mode::gray); // explicit
}

TEST(Enum_property, typed_round_trip_and_rejection)
{
    Test_object o;
    o.set_value(enum_mode, Color_mode::gray);
    EXPECT_EQ(o.get_value(enum_mode), Color_mode::gray);
    EXPECT_EQ(std::get<Enum_value>(o.get_value(enum_mode.get())).value, 5);
    o.set_value(enum_mode.get(), Property_value{Enum_value{3}}); // not in table: dropped
    EXPECT_EQ(o.get_value(enum_mode), Color_mode::gray);
    o.set_value(enum_mode.get(), Property_value{1});             // int instead of Enum_value: dropped
    EXPECT_EQ(o.get_value(enum_mode), Color_mode::gray);
}

TEST(Enum_property, string_conversion_uses_labels)
{
    EXPECT_EQ(to_string(enum_mode.get(), make_value(Color_mode::hsv)), "HSV");
    EXPECT_EQ(to_string(make_value(Color_mode::hsv)), "1"); // no table given
    EXPECT_EQ(parse_value(enum_mode.get(), "Gray").value(), make_value(Color_mode::gray));
    EXPECT_EQ(parse_value(enum_mode.get(), "5").value(), make_value(Color_mode::gray));
    EXPECT_FALSE(parse_value(enum_mode.get(), "3").has_value());
    EXPECT_FALSE(parse_value(enum_mode.get(), "Purple").has_value());
}

TEST(Enum_property, attached_enum_registration)
{
    EXPECT_TRUE(enum_mode_attached.get().is_attached());
    EXPECT_EQ(enum_mode_attached.get().get_type(), Property_type::enumeration);
    ASSERT_NE(enum_mode_attached.get().get_enum_info(), nullptr);
    Test_object b{type_b()};
    EXPECT_EQ(b.get_value(enum_mode_attached), Color_mode::hsv);
    b.set_value(enum_mode_attached, Color_mode::gray);
    EXPECT_EQ(b.get_value(enum_mode_attached), Color_mode::gray);
    EXPECT_EQ(Property_registry::get().find_for_object(type_b(), "type_c.enum_mode_attached"), enum_mode_attached.get_ptr());
}
