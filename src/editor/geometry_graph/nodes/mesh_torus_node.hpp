#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_property/dependency_property.hpp"

namespace editor {

class Mesh_torus_node : public Geometry_graph_node
{
public:
    Mesh_torus_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Registered parameter properties (property-system D27), bridged over
    // the members below (D18): the members stay the storage, JSON stays
    // the serializer, and the generic rows / MCP / expressions write
    // through the bridge, which ends in mark_dirty().
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    static const erhe::property::Property<float> major_radius_property;
    static const erhe::property::Property<float> minor_radius_property;
    static const erhe::property::Property<int>   major_steps_property;
    static const erhe::property::Property<int>   minor_steps_property;

private:
    float m_major_radius{1.0f};
    float m_minor_radius{0.25f};
    int   m_major_steps {32};
    int   m_minor_steps {16};
};

}
