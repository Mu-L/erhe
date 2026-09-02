#pragma once

#include "erhe_property/property_value.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace erhe::property {

class Dependency_property;
class Enum_info;

// Text form of every Property_type (R18). Booleans are "true" / "false",
// vectors are space-separated components, quaternions are "x y z w",
// enumerations are their Enum_info labels (the integer when no table is
// given), strings are verbatim. Floats use the shortest representation that
// round-trips.
[[nodiscard]] auto to_string(const Property_value& value, const Enum_info* enum_info = nullptr) -> std::string;
[[nodiscard]] auto to_string(const Dependency_property& property, const Property_value& value) -> std::string;

// Inverse of to_string. Booleans also accept "1" / "0"; enumerations also
// accept an integer that is in the table. nullopt when the text does not
// parse as the type.
[[nodiscard]] auto parse_value(Property_type type, std::string_view text, const Enum_info* enum_info = nullptr) -> std::optional<Property_value>;
[[nodiscard]] auto parse_value(const Dependency_property& property, std::string_view text) -> std::optional<Property_value>;

} // namespace erhe::property
