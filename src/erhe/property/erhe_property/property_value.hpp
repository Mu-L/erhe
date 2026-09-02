#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace erhe::property {

// Stored form of a C++ enumeration property value. Kept distinct from int so
// generic code (Properties window, MCP, serialization) can tell an
// enumeration (combo, labels) from a plain integer (drag int).
struct Enum_value
{
    int32_t value{0};
    [[nodiscard]] auto operator==(const Enum_value&) const -> bool = default;
};

using Property_value = std::variant<
    bool,
    int,
    float,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    glm::quat,
    std::string,
    Enum_value
>;

// Enumerators are the Property_value variant indices.
enum class Property_type : uint8_t {
    boolean     = 0,
    integer     = 1,
    floating    = 2,
    vec2        = 3,
    vec3        = 4,
    vec4        = 5,
    quat        = 6,
    string      = 7,
    enumeration = 8
};

[[nodiscard]] constexpr auto c_str(const Property_type type) -> const char*
{
    switch (type) {
        case Property_type::boolean:     return "bool";
        case Property_type::integer:     return "int";
        case Property_type::floating:    return "float";
        case Property_type::vec2:        return "vec2";
        case Property_type::vec3:        return "vec3";
        case Property_type::vec4:        return "vec4";
        case Property_type::quat:        return "quat";
        case Property_type::string:      return "string";
        case Property_type::enumeration: return "enum";
    }
    return "?";
}

[[nodiscard]] inline auto type_of(const Property_value& value) -> Property_type
{
    return static_cast<Property_type>(value.index());
}

template <typename T>
concept Property_value_type =
    std::is_same_v<T, bool>      ||
    std::is_same_v<T, int>       ||
    std::is_same_v<T, float>     ||
    std::is_same_v<T, glm::vec2> ||
    std::is_same_v<T, glm::vec3> ||
    std::is_same_v<T, glm::vec4> ||
    std::is_same_v<T, glm::quat> ||
    std::is_same_v<T, std::string> ||
    std::is_same_v<T, Enum_value>;

template <typename T>
concept Property_enum_type = std::is_enum_v<T>;

// Types a Property<T> can be declared with: the variant alternatives and any
// C++ enumeration (stored as Enum_value).
template <typename T>
concept Property_storable = Property_value_type<T> || Property_enum_type<T>;

template <Property_storable T>
struct Stored
{
    using type = std::conditional_t<Property_enum_type<T>, Enum_value, T>;
};

template <Property_storable T>
using Stored_t = typename Stored<T>::type;

template <Property_storable T>
[[nodiscard]] constexpr auto property_type_of() -> Property_type
{
    using S = Stored_t<T>;
    if constexpr (std::is_same_v<S, bool>)        { return Property_type::boolean;     }
    if constexpr (std::is_same_v<S, int>)         { return Property_type::integer;     }
    if constexpr (std::is_same_v<S, float>)       { return Property_type::floating;    }
    if constexpr (std::is_same_v<S, glm::vec2>)   { return Property_type::vec2;        }
    if constexpr (std::is_same_v<S, glm::vec3>)   { return Property_type::vec3;        }
    if constexpr (std::is_same_v<S, glm::vec4>)   { return Property_type::vec4;        }
    if constexpr (std::is_same_v<S, glm::quat>)   { return Property_type::quat;        }
    if constexpr (std::is_same_v<S, std::string>) { return Property_type::string;      }
    if constexpr (std::is_same_v<S, Enum_value>)  { return Property_type::enumeration; }
}

// C++ value -> stored variant
template <Property_storable T>
[[nodiscard]] auto make_value(const T& value) -> Property_value
{
    if constexpr (Property_enum_type<T>) {
        return Enum_value{static_cast<int32_t>(value)};
    } else {
        return Property_value{value};
    }
}

// Stored variant -> C++ value. The caller guarantees the alternative matches
// (Dependency_object does, by construction of the registry).
template <Property_storable T>
[[nodiscard]] auto get_as(const Property_value& value) -> T
{
    if constexpr (Property_enum_type<T>) {
        return static_cast<T>(std::get<Enum_value>(value).value);
    } else {
        return std::get<T>(value);
    }
}

template <Property_storable T>
[[nodiscard]] auto holds(const Property_value& value) -> bool
{
    return std::holds_alternative<Stored_t<T>>(value);
}

// The value a property of the given type has when its metadata names none:
// false, 0, 0.0f, zero vectors, identity quaternion, "", enumeration 0
// (Property<E>::register_property replaces that with the table's first
// entry).
[[nodiscard]] inline auto zero_value(const Property_type type) -> Property_value
{
    switch (type) {
        case Property_type::boolean:     return false;
        case Property_type::integer:     return 0;
        case Property_type::floating:    return 0.0f;
        case Property_type::vec2:        return glm::vec2{0.0f};
        case Property_type::vec3:        return glm::vec3{0.0f};
        case Property_type::vec4:        return glm::vec4{0.0f};
        case Property_type::quat:        return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
        case Property_type::string:      return std::string{};
        case Property_type::enumeration: return Enum_value{0};
    }
    return false;
}

} // namespace erhe::property
