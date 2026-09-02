#include "geometry_graph/nodes/value_nodes.hpp"
#include "graph_editor/graph_node_property_bridge.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

auto Float_value_node::property_owner_subtype() -> uint32_t
{
    static const uint32_t s_subtype = erhe::property::allocate_property_owner_subtype();
    return s_subtype;
}

auto Float_value_node::get_property_owner_subtype() const -> uint32_t
{
    return property_owner_subtype();
}

const Property<float> Float_value_node::value_property = Property<float>::register_property(
    "value", erhe::Item_type::graph_node, Float_value_node::property_owner_subtype(),
    Property_metadata{
        .flags  = erhe::property::Property_flags::none, // the graph JSON is the serializer
        .ui     = Property_ui{.step = 0.01f, .group = "Parameters", .label = "Value"},
        .bridge = make_node_member_bridge<Float_value_node>(&Float_value_node::m_value)
    }
);

auto Integer_value_node::property_owner_subtype() -> uint32_t
{
    static const uint32_t s_subtype = erhe::property::allocate_property_owner_subtype();
    return s_subtype;
}

auto Integer_value_node::get_property_owner_subtype() const -> uint32_t
{
    return property_owner_subtype();
}

const Property<int> Integer_value_node::value_property = Property<int>::register_property(
    "value", erhe::Item_type::graph_node, Integer_value_node::property_owner_subtype(),
    Property_metadata{
        .flags  = erhe::property::Property_flags::none,
        .ui     = Property_ui{.group = "Parameters", .label = "Value"},
        .bridge = make_node_member_bridge<Integer_value_node>(&Integer_value_node::m_value)
    }
);

auto Vector_value_node::property_owner_subtype() -> uint32_t
{
    static const uint32_t s_subtype = erhe::property::allocate_property_owner_subtype();
    return s_subtype;
}

auto Vector_value_node::get_property_owner_subtype() const -> uint32_t
{
    return property_owner_subtype();
}

const Property<glm::vec3> Vector_value_node::value_property = Property<glm::vec3>::register_property(
    "value", erhe::Item_type::graph_node, Vector_value_node::property_owner_subtype(),
    Property_metadata{
        .flags  = erhe::property::Property_flags::none,
        .ui     = Property_ui{.step = 0.01f, .group = "Parameters", .label = "Value"},
        .bridge = make_node_member_bridge<Vector_value_node>(&Vector_value_node::m_value)
    }
);

Float_value_node::Float_value_node()
    : Geometry_graph_node{"Float"}
{
    make_output_pin(Geometry_pin_key::float_value, "value");
}

void Float_value_node::evaluate(Geometry_graph&)
{
    set_output(0, Geometry_payload{.value = m_value});
}

void Float_value_node::imgui()
{
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##value", &m_value, 0.01f)) { mark_dirty(); }
}

void Float_value_node::write_parameters(nlohmann::json& out) const
{
    out["value"] = m_value;
}

void Float_value_node::read_parameters(const nlohmann::json& in)
{
    m_value = in.value("value", m_value);
    mark_dirty();
}

Integer_value_node::Integer_value_node()
    : Geometry_graph_node{"Integer"}
{
    make_output_pin(Geometry_pin_key::int_value, "value");
}

void Integer_value_node::evaluate(Geometry_graph&)
{
    set_output(0, Geometry_payload{.value = m_value});
}

void Integer_value_node::imgui()
{
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##value", &m_value, 0.1f)) { mark_dirty(); }
}

void Integer_value_node::write_parameters(nlohmann::json& out) const
{
    out["value"] = m_value;
}

void Integer_value_node::read_parameters(const nlohmann::json& in)
{
    m_value = in.value("value", m_value);
    mark_dirty();
}

Vector_value_node::Vector_value_node()
    : Geometry_graph_node{"Vector"}
{
    make_output_pin(Geometry_pin_key::vec3_value, "value");
}

void Vector_value_node::evaluate(Geometry_graph&)
{
    set_output(0, Geometry_payload{.value = m_value});
}

void Vector_value_node::imgui()
{
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat3("##value", &m_value.x, 0.01f)) { mark_dirty(); }
}

void Vector_value_node::write_parameters(nlohmann::json& out) const
{
    write_vec3(out, "value", m_value);
}

void Vector_value_node::read_parameters(const nlohmann::json& in)
{
    m_value = read_vec3(in, "value", m_value);
    mark_dirty();
}

}
