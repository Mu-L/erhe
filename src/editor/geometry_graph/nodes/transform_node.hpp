#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>

namespace editor {

class Transform_node : public Geometry_graph_node
{
public:
    enum class Rotation_mode : int {
        euler_degrees = 0,
        quaternion    = 1
    };

    Transform_node();

    [[nodiscard]] auto get_rotation_mode() const -> Rotation_mode { return m_rotation_mode; }

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Parameter properties bridged over the members below (D18 / D27);
    // Mesh_torus_node is the recipe. The rotation rows show per mode
    // (Property_ui::visible_when). rotation_degrees is stored in degrees,
    // so it stays a plain vec3 row (angle_degrees presentation would
    // convert from radians).
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    static const erhe::property::Property<glm::vec3>     translation_property;
    static const erhe::property::Property<Rotation_mode> rotation_mode_property;
    static const erhe::property::Property<glm::vec3>     rotation_degrees_property;
    static const erhe::property::Property<glm::vec4>     rotation_quaternion_property;
    static const erhe::property::Property<glm::vec3>     scale_property;

private:
    glm::vec3     m_translation        {0.0f, 0.0f, 0.0f};
    Rotation_mode m_rotation_mode      {Rotation_mode::euler_degrees};
    glm::vec3     m_rotation_degrees   {0.0f, 0.0f, 0.0f};
    glm::vec4     m_rotation_quaternion{0.0f, 0.0f, 0.0f, 1.0f}; // [x, y, z, w]
    glm::vec3     m_scale              {1.0f, 1.0f, 1.0f};
};

}
