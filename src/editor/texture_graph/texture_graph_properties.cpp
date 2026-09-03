#include "texture_graph/texture_graph_properties.hpp"
#include "texture_graph/nodes/texture_descriptor_node.hpp"
#include "texture_graph/nodes/texture_node_descriptors.hpp"

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include <glm/glm.hpp>

#include <deque>
#include <unordered_map>
#include <vector>

namespace editor {

namespace {

// Registration-side stand-in for a descriptor enum parameter: the values
// are indices into the parameter's enum_values table, there is no C++
// enumeration behind them.
enum class Texgen_enum_parameter : int {};

// Pointer-stable homes for the Enum_entry tables and Enum_info records the
// registry references for the life of the process.
struct Registration_state
{
    std::unordered_map<const erhe::texgen::Node_descriptor*, erhe::property::Owner_type> owner_types;
    std::deque<std::vector<erhe::property::Enum_entry>>                enum_entries;
    std::deque<erhe::property::Enum_info>                              enum_infos;
    bool                                                               registered{false};
};

auto state() -> Registration_state&
{
    static Registration_state s_state;
    return s_state;
}

[[nodiscard]] auto make_bridge(const std::size_t parameter_index, const erhe::texgen::Parameter_kind kind) -> erhe::property::Property_bridge
{
    return erhe::property::Property_bridge{
        .get = [parameter_index, kind](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
            const erhe::texgen::Parameter_value& value = static_cast<const Texture_descriptor_node&>(object).parameter_value(parameter_index);
            switch (kind) {
                case erhe::texgen::Parameter_kind::float_parameter: return value.float_value;
                case erhe::texgen::Parameter_kind::color_parameter: return glm::vec4{value.color_value[0], value.color_value[1], value.color_value[2], value.color_value[3]};
                case erhe::texgen::Parameter_kind::enum_parameter:  return erhe::property::Enum_value{static_cast<int32_t>(value.enum_index)};
                case erhe::texgen::Parameter_kind::bool_parameter:  return value.bool_value;
                case erhe::texgen::Parameter_kind::size_parameter:  return value.size_exponent;
                default:                                            return false; // unreachable: gradient / curve are not registered
            }
        },
        .set = [parameter_index, kind](erhe::property::Dependency_object& object, const erhe::property::Property_value& incoming) {
            Texture_descriptor_node&       node  = static_cast<Texture_descriptor_node&>(object);
            erhe::texgen::Parameter_value& value = node.parameter_value(parameter_index);
            switch (kind) {
                case erhe::texgen::Parameter_kind::float_parameter: value.float_value = std::get<float>(incoming); break;
                case erhe::texgen::Parameter_kind::color_parameter: {
                    const glm::vec4 color = std::get<glm::vec4>(incoming);
                    value.color_value = {color.x, color.y, color.z, color.w};
                    break;
                }
                case erhe::texgen::Parameter_kind::enum_parameter: value.enum_index    = static_cast<std::size_t>(std::get<erhe::property::Enum_value>(incoming).value); break;
                case erhe::texgen::Parameter_kind::bool_parameter: value.bool_value    = std::get<bool>(incoming); break;
                case erhe::texgen::Parameter_kind::size_parameter: value.size_exponent = std::get<int>(incoming); break;
                default: return;
            }
            node.mark_dirty();
        }
    };
}

void register_descriptor_parameter(const erhe::texgen::Parameter_descriptor& parameter, const std::size_t parameter_index, const erhe::property::Owner_type owner)
{
    using erhe::property::Property;
    using erhe::property::Property_metadata;
    using erhe::property::Property_ui;
    const std::string_view label = parameter.label.empty() ? std::string_view{parameter.name} : std::string_view{parameter.label};
    switch (parameter.kind) {
        case erhe::texgen::Parameter_kind::float_parameter: {
            static_cast<void>(Property<float>::register_property(
                parameter.name, owner,
                Property_metadata{
                    .default_value = parameter.default_float,
                    .flags         = erhe::property::Property_flags::none, // the graph JSON is the serializer
                    .ui            = Property_ui{.min = parameter.min_value, .max = parameter.max_value, .step = parameter.step, .group = "Parameters", .label = label},
                    .bridge        = make_bridge(parameter_index, parameter.kind)
                }
            ));
            break;
        }
        case erhe::texgen::Parameter_kind::color_parameter: {
            static_cast<void>(Property<glm::vec4>::register_property(
                parameter.name, owner,
                Property_metadata{
                    .default_value = glm::vec4{parameter.default_color[0], parameter.default_color[1], parameter.default_color[2], parameter.default_color[3]},
                    .flags         = erhe::property::Property_flags::none,
                    .ui            = Property_ui{.presentation = Property_ui::Presentation::color, .group = "Parameters", .label = label},
                    .bridge        = make_bridge(parameter_index, parameter.kind)
                }
            ));
            break;
        }
        case erhe::texgen::Parameter_kind::enum_parameter: {
            std::vector<erhe::property::Enum_entry> entries;
            entries.reserve(parameter.enum_values.size());
            for (std::size_t i = 0, end = parameter.enum_values.size(); i < end; ++i) {
                // Labels are string_views into the descriptor's std::strings,
                // stable for the life of the process.
                entries.push_back(erhe::property::Enum_entry{parameter.enum_values[i].label, static_cast<int32_t>(i)});
            }
            state().enum_entries.push_back(std::move(entries));
            state().enum_infos.emplace_back(std::string_view{parameter.name}, std::span<const erhe::property::Enum_entry>{state().enum_entries.back()});
            static_cast<void>(Property<Texgen_enum_parameter>::register_property(
                parameter.name, owner,
                state().enum_infos.back(),
                Property_metadata{
                    .default_value = erhe::property::Enum_value{static_cast<int32_t>(parameter.default_enum_index)},
                    .flags         = erhe::property::Property_flags::none,
                    .ui            = Property_ui{.group = "Parameters", .label = label},
                    .bridge        = make_bridge(parameter_index, parameter.kind)
                }
            ));
            break;
        }
        case erhe::texgen::Parameter_kind::bool_parameter: {
            static_cast<void>(Property<bool>::register_property(
                parameter.name, owner,
                Property_metadata{
                    .default_value = parameter.default_bool,
                    .flags         = erhe::property::Property_flags::none,
                    .ui            = Property_ui{.group = "Parameters", .label = label},
                    .bridge        = make_bridge(parameter_index, parameter.kind)
                }
            ));
            break;
        }
        case erhe::texgen::Parameter_kind::size_parameter: {
            static_cast<void>(Property<int>::register_property(
                parameter.name, owner,
                Property_metadata{
                    .default_value = parameter.default_size_exponent,
                    .flags         = erhe::property::Property_flags::none,
                    .ui            = Property_ui{
                        .min     = static_cast<float>(parameter.min_size_exponent),
                        .max     = static_cast<float>(parameter.max_size_exponent),
                        .group   = "Parameters",
                        .tooltip = "Power-of-two exponent; the resolution is 2^n",
                        .label   = label
                    },
                    .bridge = make_bridge(parameter_index, parameter.kind)
                }
            ));
            break;
        }
        // Gradient and curve parameters have no Property_value form; they
        // stay in the node's imgui().
        case erhe::texgen::Parameter_kind::gradient_parameter:
        case erhe::texgen::Parameter_kind::curve_parameter:
        default: {
            break;
        }
    }
}

} // anonymous namespace

void register_texture_graph_properties()
{
    Registration_state& registration_state = state();
    if (registration_state.registered) {
        return;
    }
    registration_state.registered = true;
    for (const erhe::texgen::Node_descriptor* descriptor : all_texture_node_descriptors()) {
        const erhe::property::Owner_type owner = erhe::property::allocate_owner_type(Texture_descriptor_node::property_owner_type(), descriptor->name);
        registration_state.owner_types.emplace(descriptor, owner);
        for (std::size_t i = 0, end = descriptor->parameters.size(); i < end; ++i) {
            register_descriptor_parameter(descriptor->parameters[i], i, owner);
        }
        if (descriptor->uses_seed()) {
            static_cast<void>(erhe::property::Property<float>::register_property(
                "seed", owner,
                erhe::property::Property_metadata{
                    .flags  = erhe::property::Property_flags::none,
                    .ui     = erhe::property::Property_ui{.step = 1.0f, .group = "Parameters", .label = "Seed"},
                    .bridge = erhe::property::Property_bridge{
                        .get = [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
                            return static_cast<const Texture_descriptor_node&>(object).get_seed();
                        },
                        .set = [](erhe::property::Dependency_object& object, const erhe::property::Property_value& value) {
                            Texture_descriptor_node& node = static_cast<Texture_descriptor_node&>(object);
                            node.set_seed(std::get<float>(value));
                            node.mark_dirty();
                        }
                    }
                }
            ));
        }
    }
}

auto texture_descriptor_owner_type(const erhe::texgen::Node_descriptor* const descriptor) -> erhe::property::Owner_type
{
    const Registration_state& registration_state = state();
    const auto i = registration_state.owner_types.find(descriptor);
    return (i != registration_state.owner_types.end()) ? i->second : Texture_descriptor_node::property_owner_type();
}

} // namespace editor
