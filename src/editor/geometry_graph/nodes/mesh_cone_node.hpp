#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_property/dependency_property.hpp"

namespace editor {

class Mesh_cone_node : public Geometry_graph_node
{
public:
    Mesh_cone_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Parameter properties bridged over the members below (D18 / D27);
    // Mesh_torus_node is the recipe.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    static const erhe::property::Property<float> height_property;
    static const erhe::property::Property<float> radius_property;
    static const erhe::property::Property<bool>  use_bottom_property;
    static const erhe::property::Property<int>   slices_property;
    static const erhe::property::Property<int>   stacks_property;

private:
    float m_height        {1.0f};
    float m_bottom_radius {0.5f};
    bool  m_use_bottom    {true};
    int   m_slice_count   {32};
    int   m_stack_division{1};
};

}
