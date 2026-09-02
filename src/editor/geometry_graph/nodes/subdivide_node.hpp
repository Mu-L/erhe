#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_property/dependency_property.hpp"

namespace editor {

class Subdivide_node : public Geometry_graph_node
{
public:
    enum class Mode : int {
        catmull_clark = 0,
        sqrt3         = 1
    };

    Subdivide_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Parameter properties bridged over the members below (D18 / D27);
    // Mesh_torus_node is the recipe.
    [[nodiscard]] static auto property_owner_subtype() -> uint32_t;
    [[nodiscard]] auto get_property_owner_subtype() const -> uint32_t override;
    static const erhe::property::Property<Mode> mode_property;
    static const erhe::property::Property<int>  iterations_property;

private:
    Mode m_mode      {Mode::catmull_clark};
    int  m_iterations{1};
};

}
