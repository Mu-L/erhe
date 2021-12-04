#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/physics/idebug_draw.hpp"

#include <memory>

namespace editor
{

class Selection_tool;
class Scene_root;

class Physics_window
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Physics_window"};
    static constexpr std::string_view c_title{"Physics"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Physics_window ();
    ~Physics_window() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    void render     (const Render_context& render_context) override;
    auto state      () const -> State                      override;
    auto description() -> const char*                      override;

    // Implements Window
    void imgui(Pointer_context& pointer_context) override;

    class Debug_draw_parameters
    {
    public:
        bool enable           {false};
        bool wireframe        {false};
        bool aabb             {true};
        bool contact_points   {true};
        bool no_deactivation  {false}; // forcibly disables deactivation when enabled
        bool constraints      {true};
        bool constraint_limits{true};
        bool normals          {false};
        bool frames           {true};

        erhe::physics::IDebug_draw::Colors colors;
    };

    auto get_debug_draw_parameters() -> Debug_draw_parameters;

private:
    Selection_tool*       m_selection_tool{nullptr};
    Scene_root*           m_scene_root    {nullptr};
    Debug_draw_parameters m_debug_draw;
};

} // namespace editor