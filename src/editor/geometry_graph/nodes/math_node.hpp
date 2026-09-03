#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_property/dependency_property.hpp"

namespace editor {

class Math_node : public Geometry_graph_node
{
public:
    enum class Math_operation : int {
        add      = 0,
        subtract = 1,
        multiply = 2,
        divide   = 3,
        power    = 4,
        minimum  = 5,
        maximum  = 6,
        absolute = 7,
        square_root = 8,
        sine     = 9,
        cosine   = 10
    };

    Math_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Parameter properties bridged over the members below (D18 / D27);
    // Mesh_torus_node is the recipe.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    static const erhe::property::Property<Math_operation> operation_property;
    static const erhe::property::Property<float>          a_property;
    static const erhe::property::Property<float>          b_property;

private:
    Math_operation m_operation{Math_operation::add};
    float          m_a{0.0f};
    float          m_b{0.0f};
};

}
