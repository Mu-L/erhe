#include "geometry_graph/nodes/mesh_torus_node.hpp"
#include "graph_editor/graph_node_property_bridge.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/torus.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

auto Mesh_torus_node::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(Graph_editor_node::property_owner_type(), "Mesh_torus_node");
    return s_id;
}

auto Mesh_torus_node::get_property_owner_type() const -> erhe::property::Owner_type
{
    return property_owner_type();
}

const Property<float> Mesh_torus_node::major_radius_property = Property<float>::register_member(
    "major_radius", Mesh_torus_node::property_owner_type(), &Mesh_torus_node::m_major_radius,
    Property_metadata{
        .default_value = 1.0f,
        .flags         = erhe::property::Property_flags::none, // the graph JSON is the serializer
        .ui            = Property_ui{.min = 0.01f, .max = 100.0f, .step = 0.01f, .group = "Parameters", .label = "Major radius"}
    },
    mark_node_dirty
);

const Property<float> Mesh_torus_node::minor_radius_property = Property<float>::register_member(
    "minor_radius", Mesh_torus_node::property_owner_type(), &Mesh_torus_node::m_minor_radius,
    Property_metadata{
        .default_value = 0.25f,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 0.01f, .max = 100.0f, .step = 0.01f, .group = "Parameters", .label = "Minor radius"}
    },
    mark_node_dirty
);

const Property<int> Mesh_torus_node::major_steps_property = Property<int>::register_member(
    "major_steps", Mesh_torus_node::property_owner_type(), &Mesh_torus_node::m_major_steps,
    Property_metadata{
        .default_value = 32,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 3.0f, .max = 128.0f, .group = "Parameters", .label = "Major steps"}
    },
    mark_node_dirty
);

const Property<int> Mesh_torus_node::minor_steps_property = Property<int>::register_member(
    "minor_steps", Mesh_torus_node::property_owner_type(), &Mesh_torus_node::m_minor_steps,
    Property_metadata{
        .default_value = 16,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 3.0f, .max = 128.0f, .group = "Parameters", .label = "Minor steps"}
    },
    mark_node_dirty
);

Mesh_torus_node::Mesh_torus_node()
    : Geometry_graph_node{"Torus"}
{
    make_input_pin(Geometry_pin_key::float_value, "major r");
    make_input_pin(Geometry_pin_key::float_value, "minor r");
    make_input_pin(Geometry_pin_key::int_value,   "major steps");
    make_input_pin(Geometry_pin_key::int_value,   "minor steps");
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Mesh_torus_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float major_radius = get_input(0).get_float(m_major_radius);
    const float minor_radius = get_input(1).get_float(m_minor_radius);
    const int   major_steps  = std::max(3, get_input(2).get_int(m_major_steps));
    const int   minor_steps  = std::max(3, get_input(3).get_int(m_minor_steps));

    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("torus");
    erhe::geometry::shapes::make_torus(geometry->get_mesh(), major_radius, minor_radius, major_steps, minor_steps);
    process_for_graph(*geometry.get());
    set_output(0, Geometry_payload{.value = geometry});
}

void Mesh_torus_node::imgui()
{
    ImGui::TextUnformatted("Major / minor radius");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##major_radius", &m_major_radius, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##minor_radius", &m_minor_radius, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Major / minor steps");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##major_steps", &m_major_steps, 0.1f, 3, 128)) { mark_dirty(); }
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##minor_steps", &m_minor_steps, 0.1f, 3, 128)) { mark_dirty(); }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Mesh_torus_node::write_parameters(nlohmann::json& out) const
{
    out["major_radius"] = m_major_radius;
    out["minor_radius"] = m_minor_radius;
    out["major_steps"]  = m_major_steps;
    out["minor_steps"]  = m_minor_steps;
}

void Mesh_torus_node::read_parameters(const nlohmann::json& in)
{
    m_major_radius = in.value("major_radius", m_major_radius);
    m_minor_radius = in.value("minor_radius", m_minor_radius);
    m_major_steps  = in.value("major_steps",  m_major_steps);
    m_minor_steps  = in.value("minor_steps",  m_minor_steps);
    mark_dirty();
}

}
