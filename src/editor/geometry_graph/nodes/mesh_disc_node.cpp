#include "geometry_graph/nodes/mesh_disc_node.hpp"
#include "graph_editor/graph_node_property_bridge.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/disc.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

auto Mesh_disc_node::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(Graph_editor_node::property_owner_type(), "Mesh_disc_node");
    return s_id;
}

auto Mesh_disc_node::get_property_owner_type() const -> erhe::property::Owner_type
{
    return property_owner_type();
}

const Property<float> Mesh_disc_node::outer_radius_property = Property<float>::register_member(
    "outer_radius", Mesh_disc_node::property_owner_type(), &Mesh_disc_node::m_outer_radius,
    Property_metadata{
        .default_value = 1.0f,
        .flags         = erhe::property::Property_flags::none, // the graph JSON is the serializer
        .ui            = Property_ui{.min = 0.01f, .max = 100.0f, .step = 0.01f, .group = "Parameters", .label = "Outer radius"}
    },
    mark_node_dirty
);

const Property<float> Mesh_disc_node::inner_radius_property = Property<float>::register_member(
    "inner_radius", Mesh_disc_node::property_owner_type(), &Mesh_disc_node::m_inner_radius,
    Property_metadata{
        .default_value = 0.0f,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 0.0f, .max = 100.0f, .step = 0.01f, .group = "Parameters", .label = "Inner radius"}
    },
    mark_node_dirty
);

const Property<int> Mesh_disc_node::slices_property = Property<int>::register_member(
    "slices", Mesh_disc_node::property_owner_type(), &Mesh_disc_node::m_slice_count,
    Property_metadata{
        .default_value = 32,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 3.0f, .max = 128.0f, .group = "Parameters", .label = "Slices"}
    },
    mark_node_dirty
);

const Property<int> Mesh_disc_node::stacks_property = Property<int>::register_member(
    "stacks", Mesh_disc_node::property_owner_type(), &Mesh_disc_node::m_stack_count,
    Property_metadata{
        .default_value = 1,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 1.0f, .max = 128.0f, .group = "Parameters", .label = "Stacks"}
    },
    mark_node_dirty
);

Mesh_disc_node::Mesh_disc_node()
    : Geometry_graph_node{"Disc"}
{
    make_input_pin(Geometry_pin_key::float_value, "outer r");
    make_input_pin(Geometry_pin_key::float_value, "inner r");
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Mesh_disc_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float outer_radius = get_input(0).get_float(m_outer_radius);
    const float inner_radius = get_input(1).get_float(m_inner_radius);
    const int   slice_count  = std::max(3, m_slice_count);
    const int   stack_count  = std::max(1, m_stack_count);

    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("disc");
    erhe::geometry::shapes::make_disc(geometry->get_mesh(), outer_radius, inner_radius, slice_count, stack_count);
    process_for_graph(*geometry.get());
    set_output(0, Geometry_payload{.value = geometry});
}

void Mesh_disc_node::imgui()
{
    ImGui::TextUnformatted("Outer radius");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##outer_radius", &m_outer_radius, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Inner radius");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##inner_radius", &m_inner_radius, 0.01f, 0.0f, 100.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Slices");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##slices", &m_slice_count, 0.1f, 3, 128)) { mark_dirty(); }
    ImGui::TextUnformatted("Stacks");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##stacks", &m_stack_count, 0.1f, 1, 128)) { mark_dirty(); }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Mesh_disc_node::write_parameters(nlohmann::json& out) const
{
    out["outer_radius"] = m_outer_radius;
    out["inner_radius"] = m_inner_radius;
    out["slices"]       = m_slice_count;
    out["stacks"]       = m_stack_count;
}

void Mesh_disc_node::read_parameters(const nlohmann::json& in)
{
    m_outer_radius = in.value("outer_radius", m_outer_radius);
    m_inner_radius = in.value("inner_radius", m_inner_radius);
    m_slice_count  = in.value("slices",       m_slice_count);
    m_stack_count  = in.value("stacks",       m_stack_count);
    mark_dirty();
}

}
