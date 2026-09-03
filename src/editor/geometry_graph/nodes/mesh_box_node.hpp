#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>

namespace editor {

class Mesh_box_node : public Geometry_graph_node
{
public:
    Mesh_box_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Parameter properties bridged over the members below (D18 / D27);
    // Mesh_torus_node is the recipe.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    static const erhe::property::Property<glm::vec3>  size_property;
    static const erhe::property::Property<glm::ivec3> subdivisions_property;
    static const erhe::property::Property<float>      power_property;

private:
    glm::vec3  m_size        {1.0f, 1.0f, 1.0f};
    // Interior subdivision planes per axis; 0 = vertices only at the
    // min/max corners of that axis.
    glm::ivec3 m_subdivisions{1, 1, 1};
    float      m_power       {1.0f};
};

}
