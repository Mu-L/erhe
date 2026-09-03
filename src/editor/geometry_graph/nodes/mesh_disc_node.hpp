#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_property/dependency_property.hpp"

namespace editor {

class Mesh_disc_node : public Geometry_graph_node
{
public:
    Mesh_disc_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Parameter properties bridged over the members below (D18 / D27);
    // Mesh_torus_node is the recipe.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    static const erhe::property::Property<float> outer_radius_property;
    static const erhe::property::Property<float> inner_radius_property;
    static const erhe::property::Property<int>   slices_property;
    static const erhe::property::Property<int>   stacks_property;

private:
    float m_outer_radius{1.0f};
    float m_inner_radius{0.0f};
    int   m_slice_count {32};
    int   m_stack_count {1};
};

}
