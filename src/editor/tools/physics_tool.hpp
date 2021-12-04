#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

namespace erhe::scene {
    class Mesh;
}

namespace erhe::physics {
    class IConstraint;
}

namespace editor
{

class Node_physics;
class Scene_root;

class Physics_tool
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Physics_tool"};
    static constexpr std::string_view c_title{"Physics Tool"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Physics_tool ();
    ~Physics_tool() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    auto update         (Pointer_context& pointer_context) -> bool override;
    void render         (const Render_context& render_context)     override;
    auto state          () const -> State                          override;
    void cancel_ready   ()                                         override;
    auto description    () -> const char*                          override;
    void tool_properties()                                         override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

private:
    void update_internal(Pointer_context& pointer_context);

    State                                       m_state{State::Passive};

    Scene_root*                                 m_scene_root{nullptr};

    std::shared_ptr<erhe::scene::Mesh>          m_drag_mesh;
    std::shared_ptr<Node_physics>               m_drag_node_physics;
    float                                       m_drag_depth{0.5f};
    glm::vec3                                   m_drag_position_in_mesh{0.0f, 0.0f, 0.0f};
    glm::vec3                                   m_drag_position_start  {0.0f, 0.0f, 0.0f};
    glm::vec3                                   m_drag_position_end    {0.0f, 0.0f, 0.0f};
    std::unique_ptr<erhe::physics::IConstraint> m_drag_constraint;

    float    m_tau                     { 0.001f};
    float    m_damping                 { 1.00f};
    float    m_impulse_clamp           { 1.00f};
    float    m_linear_damping          { 0.99f};
    float    m_angular_damping         { 0.99f};
    float    m_original_linear_damping { 0.00f};
    float    m_original_angular_damping{ 0.00f};
    uint64_t m_last_update_frame_number{0};
};

} // namespace editor