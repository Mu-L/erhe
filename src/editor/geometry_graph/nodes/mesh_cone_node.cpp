#include "geometry_graph/nodes/mesh_cone_node.hpp"
#include "graph_editor/graph_node_property_bridge.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/cone.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

auto Mesh_cone_node::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(Graph_editor_node::property_owner_type(), "Mesh_cone_node");
    return s_id;
}

auto Mesh_cone_node::get_property_owner_type() const -> erhe::property::Owner_type
{
    return property_owner_type();
}

const Property<float> Mesh_cone_node::height_property = Property<float>::register_member(
    "height", Mesh_cone_node::property_owner_type(), &Mesh_cone_node::m_height,
    Property_metadata{
        .default_value = 1.0f,
        .flags         = erhe::property::Property_flags::none, // the graph JSON is the serializer
        .ui            = Property_ui{.min = 0.01f, .max = 100.0f, .step = 0.01f, .group = "Parameters", .label = "Height"}
    },
    mark_node_dirty
);

const Property<float> Mesh_cone_node::radius_property = Property<float>::register_member(
    "radius", Mesh_cone_node::property_owner_type(), &Mesh_cone_node::m_bottom_radius,
    Property_metadata{
        .default_value = 0.5f,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 0.01f, .max = 100.0f, .step = 0.01f, .group = "Parameters", .label = "Radius"}
    },
    mark_node_dirty
);

const Property<bool> Mesh_cone_node::use_bottom_property = Property<bool>::register_member(
    "use_bottom", Mesh_cone_node::property_owner_type(), &Mesh_cone_node::m_use_bottom,
    Property_metadata{
        .default_value = true,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.group = "Parameters", .label = "Bottom"}
    },
    mark_node_dirty
);

const Property<int> Mesh_cone_node::slices_property = Property<int>::register_member(
    "slices", Mesh_cone_node::property_owner_type(), &Mesh_cone_node::m_slice_count,
    Property_metadata{
        .default_value = 32,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 3.0f, .max = 128.0f, .group = "Parameters", .label = "Slices"}
    },
    mark_node_dirty
);

const Property<int> Mesh_cone_node::stacks_property = Property<int>::register_member(
    "stacks", Mesh_cone_node::property_owner_type(), &Mesh_cone_node::m_stack_division,
    Property_metadata{
        .default_value = 1,
        .flags         = erhe::property::Property_flags::none,
        .ui            = Property_ui{.min = 1.0f, .max = 128.0f, .group = "Parameters", .label = "Stacks"}
    },
    mark_node_dirty
);

Mesh_cone_node::Mesh_cone_node()
    : Geometry_graph_node{"Cone"}
{
    make_input_pin(Geometry_pin_key::float_value, "height");
    make_input_pin(Geometry_pin_key::float_value, "radius");
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Mesh_cone_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float height        = get_input(0).get_float(m_height);
    const float bottom_radius = get_input(1).get_float(m_bottom_radius);
    const int   slice_count   = std::max(3, m_slice_count);
    const int   stack_division = std::max(1, m_stack_division);

    // make_cone() extends along the x axis from min_x to max_x.
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("cone");
    erhe::geometry::shapes::make_cone(geometry->get_mesh(), 0.0f, height, bottom_radius, m_use_bottom, slice_count, stack_division);
    process_for_graph(*geometry.get());
    set_output(0, Geometry_payload{.value = geometry});
}

void Mesh_cone_node::imgui()
{
    ImGui::TextUnformatted("Height");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##height", &m_height, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Radius");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##radius", &m_bottom_radius, 0.01f, 0.01f, 100.0f)) { mark_dirty(); }
    if (ImGui::Checkbox("Bottom", &m_use_bottom)) { mark_dirty(); }
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

void Mesh_cone_node::write_parameters(nlohmann::json& out) const
{
    out["height"]     = m_height;
    out["radius"]     = m_bottom_radius;
    out["use_bottom"] = m_use_bottom;
    out["slices"]     = m_slice_count;
    out["stacks"]     = m_stack_division;
}

void Mesh_cone_node::read_parameters(const nlohmann::json& in)
{
    m_height         = in.value("height",     m_height);
    m_bottom_radius  = in.value("radius",     m_bottom_radius);
    m_use_bottom     = in.value("use_bottom", m_use_bottom);
    m_slice_count    = in.value("slices",     m_slice_count);
    m_stack_division = in.value("stacks",     m_stack_division);
    mark_dirty();
}

}
