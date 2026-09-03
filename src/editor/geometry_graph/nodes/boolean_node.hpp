#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_property/dependency_property.hpp"

namespace editor {

// CSG boolean of two geometries (experimental Geogram backend).
class Boolean_node : public Geometry_graph_node
{
public:
    enum class Boolean_operation : int {
        union_operation = 0,
        intersection    = 1,
        difference      = 2
    };

    Boolean_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Parameter properties bridged over the members below (D18 / D27);
    // Mesh_torus_node is the recipe.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    static const erhe::property::Property<Boolean_operation> operation_property;

private:
    Boolean_operation m_operation{Boolean_operation::union_operation};
};

}
