#pragma once

#include "erhe_property/property_value.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace erhe::property {

class Dependency_object;
class Dependency_property;

// Where the base value of a property on an object comes from.
enum class Value_source : uint8_t {
    default_value = 0, // metadata default
    inherited     = 1, // closest ancestor's effective value (inherits flag)
    local         = 2  // set_value on this object
};

[[nodiscard]] constexpr auto c_str(const Value_source source) -> const char*
{
    switch (source) {
        case Value_source::default_value: return "default";
        case Value_source::inherited:     return "inherited";
        case Value_source::local:         return "local";
    }
    return "?";
}

class Property_changed_args
{
public:
    const Dependency_property& property;
    const Property_value&      old_value;
    const Property_value&      new_value;
    Value_source               old_source;
    Value_source               new_source;
};

using Property_changed_callback = std::function<void(Dependency_object&, const Property_changed_args&)>;
using Coerce_callback           = std::function<Property_value(const Dependency_object&, const Property_value&)>;
using Validate_callback         = std::function<bool(const Property_value&)>;

// What a change of the property affects. Data only: the library never acts
// on these, the editor reads them (App_context::on_item_property_changed).
class Property_flags
{
public:
    static constexpr uint32_t none                         = 0u;
    static constexpr uint32_t affects_transform            = (1u << 0);
    static constexpr uint32_t affects_draw_list_partition  = (1u << 1);
    static constexpr uint32_t affects_shader_variant       = (1u << 2);
    static constexpr uint32_t serialize                    = (1u << 3);
};

// What the Properties window needs to draw the row. Data only.
class Property_ui
{
public:
    enum class Presentation : uint8_t {
        plain = 0,
        color,          // vec3 / vec4 color edit
        angle_degrees,  // float / vec3 stored in radians, shown in degrees
        slider          // float / int slider within min..max
    };

    // Row shown only while this returns true for the inspected object
    // (e.g. a material's alpha cutoff only in the alpha-test blending
    // mode); unset = always shown.
    using Visible_when = std::function<bool(const Dependency_object&)>;

    std::optional<float> min           {};
    std::optional<float> max           {};
    std::optional<float> step          {};
    Presentation         presentation  {Presentation::plain};
    bool                 logarithmic   {false}; // slider presentation: logarithmic scale (D20)
    std::string_view     group         {};
    std::string_view     tooltip       {};
    bool                 developer_only{false};
    std::string_view     label         {}; // row label; empty = the property name
    Visible_when         visible_when  {};
};

// Storage outside the object's entry store (doc/property-system-plan.md
// D18): when `get` is bound, the property's local value is whatever `get`
// returns and set_value / clear_value go through `set`. The object never
// gets an entry for the property, it always reports Value_source::local,
// it never inherits, and its coerce callback runs on every read. For state
// that already has an engineered representation the object must keep
// (Node's transform with its deferred matrix decomposition).
class Property_bridge
{
public:
    std::function<Property_value(const Dependency_object&)>        get{};
    std::function<void(Dependency_object&, const Property_value&)> set{};

    [[nodiscard]] auto is_bound() const -> bool { return static_cast<bool>(get); }
};

class Property_metadata
{
public:
    // nullopt: the zero value of the property type (zero_value()), or the
    // first table entry for an enumeration.
    std::optional<Property_value> default_value   {};
    Property_changed_callback     property_changed{};
    Coerce_callback               coerce          {};
    bool                          inherits        {false};
    uint32_t                      flags           {Property_flags::serialize};
    Property_ui                   ui              {};
    Property_bridge               bridge          {};
};

} // namespace erhe::property
