#include "geometry_graph/nodes/mesh_box_node.hpp"
#include "graph_editor/graph_node_property_bridge.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

auto Mesh_box_node::property_owner_subtype() -> uint32_t
{
    static const uint32_t s_subtype = erhe::property::allocate_property_owner_subtype();
    return s_subtype;
}

auto Mesh_box_node::get_property_owner_subtype() const -> uint32_t
{
    return property_owner_subtype();
}

const Property<glm::vec3> Mesh_box_node::size_property = Property<glm::vec3>::register_property(
    "size", erhe::Item_type::graph_node, Mesh_box_node::property_owner_subtype(),
    Property_metadata{
        .default_value = glm::vec3{1.0f, 1.0f, 1.0f},
        .flags         = erhe::property::Property_flags::none, // the graph JSON is the serializer
        .ui            = Property_ui{.min = 0.01f, .max = 100.0f, .step = 0.01f, .group = "Parameters", .label = "Size"},
        .bridge        = make_node_member_bridge<Mesh_box_node>(&Mesh_box_node::m_size)
    }
);

const Property<glm::ivec3> Mesh_box_node::subdivisions_property = Property<glm::ivec3>::register_property(
    "subdivisions", erhe::Item_type::graph_node, Mesh_box_node::property_owner_subtype(),
    Property_metadata{
        .default_value = glm::ivec3{1, 1, 1},
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{
            .min     = 0.0f,
            .max     = 16.0f,
            .group   = "Parameters",
            .tooltip = "Interior subdivision planes per axis; 0 = vertices only at the min/max corners of that axis",
            .label   = "Subdivisions"
        },
        .bridge        = make_node_member_bridge<Mesh_box_node>(&Mesh_box_node::m_subdivisions)
    }
);

const Property<float> Mesh_box_node::power_property = Property<float>::register_property(
    "power", erhe::Item_type::graph_node, Mesh_box_node::property_owner_subtype(),
    Property_metadata{
        .default_value = 1.0f,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 0.1f, .max = 10.0f, .step = 0.01f, .group = "Parameters", .label = "Power"},
        .bridge        = make_node_member_bridge<Mesh_box_node>(&Mesh_box_node::m_power)
    }
);

Mesh_box_node::Mesh_box_node()
    : Geometry_graph_node{"Box"}
{
    make_input_pin(Geometry_pin_key::float_value, "x size");
    make_input_pin(Geometry_pin_key::float_value, "y size");
    make_input_pin(Geometry_pin_key::float_value, "z size");
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Mesh_box_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float x_size = get_input(0).get_float(m_size.x);
    const float y_size = get_input(1).get_float(m_size.y);
    const float z_size = get_input(2).get_float(m_size.z);

    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("box");
    erhe::geometry::shapes::make_box(
        geometry->get_mesh(),
        GEO::vec3f{x_size, y_size, z_size},
        erhe::geometry::to_geo_vec3i(m_subdivisions),
        m_power
    );
    process_for_graph(*geometry.get());
    set_output(0, Geometry_payload{.value = geometry});
}

void Mesh_box_node::imgui()
{
    ImGui::TextUnformatted("Size");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat3("##size", &m_size.x, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Subdivisions");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt3("##subdivisions", &m_subdivisions.x, 0.1f, 0, 16)) { mark_dirty(); }
    ImGui::TextUnformatted("Power");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##power", &m_power, 0.01f, 0.1f, 10.0f)) { mark_dirty(); }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Mesh_box_node::write_parameters(nlohmann::json& out) const
{
    write_vec3 (out, "size",         m_size);
    write_ivec3(out, "subdivisions", m_subdivisions);
    out["power"] = m_power;
}

void Mesh_box_node::read_parameters(const nlohmann::json& in)
{
    m_size         = read_vec3 (in, "size",         m_size);
    m_subdivisions = read_ivec3(in, "subdivisions", m_subdivisions);
    m_power        = in.value("power", m_power);
    mark_dirty();
}

}
