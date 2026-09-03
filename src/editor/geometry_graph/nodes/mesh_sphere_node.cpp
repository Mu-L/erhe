#include "geometry_graph/nodes/mesh_sphere_node.hpp"
#include "graph_editor/graph_node_property_bridge.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/sphere.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

auto Mesh_sphere_node::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(Graph_editor_node::property_owner_type(), "Mesh_sphere_node");
    return s_id;
}

auto Mesh_sphere_node::get_property_owner_type() const -> erhe::property::Owner_type
{
    return property_owner_type();
}

const Property<float> Mesh_sphere_node::radius_property = Property<float>::register_member(
    "radius", Mesh_sphere_node::property_owner_type(), &Mesh_sphere_node::m_radius,
    Property_metadata{
        .default_value = 1.0f,
        .flags         = erhe::property::Property_flags::none, // the graph JSON is the serializer
        .ui            = Property_ui{.min = 0.01f, .max = 100.0f, .step = 0.01f, .group = "Parameters", .label = "Radius"}
    },
    mark_node_dirty
);

const Property<int> Mesh_sphere_node::slices_property = Property<int>::register_member(
    "slices", Mesh_sphere_node::property_owner_type(), &Mesh_sphere_node::m_slice_count,
    Property_metadata{
        .default_value = 32,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 3.0f, .max = 128.0f, .group = "Parameters", .label = "Slices"}
    },
    mark_node_dirty
);

const Property<int> Mesh_sphere_node::stacks_property = Property<int>::register_member(
    "stacks", Mesh_sphere_node::property_owner_type(), &Mesh_sphere_node::m_stack_division,
    Property_metadata{
        .default_value = 16,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 1.0f, .max = 128.0f, .group = "Parameters", .label = "Stacks"}
    },
    mark_node_dirty
);

Mesh_sphere_node::Mesh_sphere_node()
    : Geometry_graph_node{"Sphere"}
{
    make_input_pin(Geometry_pin_key::float_value, "radius");
    make_input_pin(Geometry_pin_key::int_value,   "slices");
    make_input_pin(Geometry_pin_key::int_value,   "stacks");
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Mesh_sphere_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float radius         = get_input(0).get_float(m_radius);
    const int   slice_count    = std::max(3, get_input(1).get_int(m_slice_count));
    const int   stack_division = std::max(1, get_input(2).get_int(m_stack_division));

    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("sphere");
    erhe::geometry::shapes::make_sphere(
        geometry->get_mesh(),
        radius,
        static_cast<unsigned int>(slice_count),
        static_cast<unsigned int>(stack_division)
    );
    process_for_graph(*geometry.get());
    set_output(0, Geometry_payload{.value = geometry});
}

void Mesh_sphere_node::imgui()
{
    ImGui::TextUnformatted("Radius");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##radius", &m_radius, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Slices");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##slices", &m_slice_count, 0.1f, 3, 128)) { mark_dirty(); }
    ImGui::TextUnformatted("Stacks");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##stacks", &m_stack_division, 0.1f, 1, 128)) { mark_dirty(); }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Mesh_sphere_node::write_parameters(nlohmann::json& out) const
{
    out["radius"] = m_radius;
    out["slices"] = m_slice_count;
    out["stacks"] = m_stack_division;
}

void Mesh_sphere_node::read_parameters(const nlohmann::json& in)
{
    m_radius         = in.value("radius", m_radius);
    m_slice_count    = in.value("slices", m_slice_count);
    m_stack_division = in.value("stacks", m_stack_division);
    mark_dirty();
}

}
