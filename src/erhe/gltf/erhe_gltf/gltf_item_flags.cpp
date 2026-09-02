#include "erhe_gltf/gltf_item_flags.hpp"
#include "erhe_gltf/gltf_log.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"

#include <cstdio>

namespace erhe::gltf {

namespace {

class Serialized_item_flag
{
public:
    uint64_t         bit;
    std::string_view name;
};

constexpr Serialized_item_flag c_persistent_item_flags[] = {
    { erhe::Item_flags::no_message,                "no_message"                },
    { erhe::Item_flags::no_transform_update,       "no_transform_update"       },
    { erhe::Item_flags::transform_world_normative, "transform_world_normative" },
    { erhe::Item_flags::show_in_ui,                "show_in_ui"                },
    { erhe::Item_flags::show_debug_visualizations, "show_debug_visualizations" },
    { erhe::Item_flags::shadow_cast,               "shadow_cast"               },
    { erhe::Item_flags::lock_viewport_selection,   "lock_viewport_selection"   },
    { erhe::Item_flags::lock_viewport_transform,   "lock_viewport_transform"   },
    { erhe::Item_flags::visible,                   "visible"                   },
    { erhe::Item_flags::invisible_parent,          "invisible_parent"          },
    { erhe::Item_flags::render_wireframe,          "render_wireframe"          },
    { erhe::Item_flags::render_bounding_volume,    "render_bounding_volume"    },
    { erhe::Item_flags::content,                   "content"                   },
    { erhe::Item_flags::id,                        "id"                        },
    { erhe::Item_flags::tool,                      "tool"                      },
    { erhe::Item_flags::brush,                     "brush"                     },
    { erhe::Item_flags::controller,                "controller"                },
    { erhe::Item_flags::rendertarget,              "rendertarget"              },
    { erhe::Item_flags::expand,                    "expand"                    },
    { erhe::Item_flags::lock_edit,                 "lock_edit"                 },
    { erhe::Item_flags::show_in_developer_ui,      "show_in_developer_ui"      },
    { erhe::Item_flags::exclude_from_prefab,       "exclude_from_prefab"       },
    { erhe::Item_flags::lightmapped,               "lightmapped"               },
    { erhe::Item_flags::ik_lock,                   "ik_lock"                   }
};

} // anonymous namespace

auto persistent_item_flags_to_json(const uint64_t flag_bits) -> std::string
{
    std::string out{"["};
    const char* separator = "";
    for (const Serialized_item_flag& flag : c_persistent_item_flags) {
        if ((flag.bit & erhe::Item_flags::derived) != 0u) {
            continue; // properties (item_local_properties_to_json)
        }
        if ((flag_bits & flag.bit) != 0) {
            out += separator;
            out += '"';
            out += flag.name;
            out += '"';
            separator = ",";
        }
    }
    out += "]";
    return out;
}

auto persistent_item_flag_from_name(const std::string_view name) -> uint64_t
{
    for (const Serialized_item_flag& flag : c_persistent_item_flags) {
        if (name == flag.name) {
            return flag.bit;
        }
    }
    return 0;
}

void apply_persistent_item_flags(erhe::Item_base& item, const uint64_t listed_bits)
{
    for (const Serialized_item_flag& flag : c_persistent_item_flags) {
        if ((flag.bit & erhe::Item_flags::derived) != 0u) {
            continue;
        }
        if ((listed_bits & flag.bit) != 0) {
            item.enable_flag_bits(flag.bit);
        } else {
            item.disable_flag_bits(flag.bit);
        }
    }
}

namespace {

void append_json_string(std::string& out, const std::string_view text)
{
    out += '"';
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default: {
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out += c;
                }
                break;
            }
        }
    }
    out += '"';
}

} // anonymous namespace

auto item_local_properties_to_json(const erhe::Item_base& item) -> std::string
{
    std::string out{"{"};
    const char* separator = "";
    const uint64_t owner_type = item.get_property_owner_type();
    item.for_each_local_value(
        [&](const erhe::property::Dependency_property& property, const erhe::property::Property_value& value) {
            const erhe::property::Property_metadata& metadata = property.get_metadata(owner_type);
            if ((metadata.flags & erhe::property::Property_flags::serialize) == 0u) {
                return;
            }
            if (metadata.bridge.is_bound()) {
                return; // native glTF field
            }
            if (item.get_expression(property).has_value()) {
                return; // formulas are session state (D14)
            }
            out += separator;
            append_json_string(out, property.get_name());
            out += ':';
            append_json_string(out, erhe::property::to_string(property, value));
            separator = ",";
        }
    );
    out += "}";
    return out;
}

auto apply_item_local_property(erhe::Item_base& item, const std::string_view name, const std::string_view value) -> bool
{
    const erhe::property::Dependency_property* property = erhe::property::Property_registry::get().find_for_type(item.get_property_owner_type(), name);
    if (property == nullptr) {
        log_gltf->warn("'{}': no property '{}' on {}", item.get_name(), name, item.get_type_name());
        return false;
    }
    const std::optional<erhe::property::Property_value> parsed = erhe::property::parse_value(*property, value);
    if (!parsed.has_value()) {
        log_gltf->warn("'{}': property '{}' value '{}' does not parse", item.get_name(), name, value);
        return false;
    }
    item.set_value(*property, parsed.value());
    return true;
}

void apply_legacy_derived_item_flags(erhe::Item_base& item, const uint64_t listed_bits)
{
    if ((listed_bits & erhe::Item_flags::visible) == 0u) {
        item.set_value(erhe::Item_base::visible_property, false);
    }
    if ((listed_bits & erhe::Item_flags::shadow_cast) != 0u) {
        item.set_value(erhe::Item_base::shadow_cast_property, true);
    }
    if ((listed_bits & erhe::Item_flags::lightmapped) != 0u) {
        item.set_value(erhe::Item_base::lightmapped_property, true);
    }
}

}
