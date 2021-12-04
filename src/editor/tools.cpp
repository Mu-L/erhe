﻿#include "tools.hpp"
#include "application.hpp"
#include "log.hpp"
#include "time.hpp"
#include "view.hpp"
#include "window.hpp"
#include "operations/compound_operation.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/grid_tool.hpp"
#include "tools/hover_tool.hpp"
#include "tools/physics_tool.hpp"
#include "tools/trs_tool.hpp"
#include "windows/brushes.hpp"

#include "erhe/imgui/imgui_impl_erhe.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

#include <backends/imgui_impl_glfw.h>

namespace editor {

using namespace std;

Editor_tools::Editor_tools()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Editor_tools::~Editor_tools() = default;

void Editor_tools::connect()
{
    require<Application>();
    require<erhe::graphics::OpenGL_state_tracker>();
    require<Window>();
    m_brushes        = get<Brushes       >();
    m_editor_view    = get<Editor_view   >();
    m_editor_time    = get<Editor_time   >();
    m_physics_tool   = get<Physics_tool  >();
    m_selection_tool = get<Selection_tool>();
    m_trs_tool       = get<Trs_tool      >();
}

void Editor_tools::initialize_component()
{
    IMGUI_CHECKVERSION();
    m_imgui_context = ImGui::CreateContext();
    ImGuiIO& io     = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.Fonts->AddFontFromFileTTF("res/fonts/ProximaNova-Regular.otf", 16.0f);

    ImFontGlyphRangesBuilder builder;

    //ImGui::Text(u8"Hiragana: 要らない.txt");
    //ImGui::Text(u8"Greek kosme κόσμε");

    // %u 8981      4E00 — 9FFF  	CJK Unified Ideographs
    // %u 3089      3040 — 309F  	Hiragana
    // %u 306A      3040 — 309F  	Hiragana
    // %u 3044.txt  3040 — 309F  	Hiragana
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());                // Basic Latin, Extended Latin
    //builder.AddRanges(io.Fonts->GetGlyphRangesKorean());                 // Default + Korean characters
    //builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());               // Default + Hiragana, Katakana, Half-Width, Selection of 2999 Ideographs
    //builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());            // Default + Half-Width + Japanese Hiragana/Katakana + full set of about 21000 CJK Unified Ideographs
    //builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());// Default + Half-Width + Japanese Hiragana/Katakana + set of 2500 CJK Unified Ideographs for common simplified Chinese
    //builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());               // Default + about 400 Cyrillic characters
    //builder.AddRanges(io.Fonts->GetGlyphRangesThai());                   // Default + Thai characters
    //builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());             // Default + Vietnamese characters

    builder.BuildRanges(&m_glyph_ranges);
    //io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f, nullptr, m_glyph_ranges.Data);

    ImGui::StyleColorsDark();
    auto* const glfw_window = reinterpret_cast<GLFWwindow*>(get<Window>()->get_context_window()->get_glfw_window());
    ImGui_ImplGlfw_InitForOther(glfw_window, true);
    ImGui_ImplErhe_Init(get<erhe::graphics::OpenGL_state_tracker>());

    get<Editor_tools>()->register_imgui_window(this);
}

void Editor_tools::menu()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo"      )) {}
            if (ImGui::MenuItem("Redo"      )) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Select All")) {}
            if (ImGui::MenuItem("Deselect"  )) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Translate" )) {}
            if (ImGui::MenuItem("Rotate"    )) {}
            if (ImGui::MenuItem("Scale"     )) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::MenuItem("Cube"  )) {}
            if (ImGui::MenuItem("Box"   )) {}
            if (ImGui::MenuItem("Sphere")) {}
            if (ImGui::MenuItem("Torus" )) {}
            if (ImGui::MenuItem("Cone"  )) {}
            if (ImGui::BeginMenu("Brush"))
            {
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Modify"))
        {
            if (ImGui::MenuItem("Ambo"    )) {}
            if (ImGui::MenuItem("Dual"    )) {}
            if (ImGui::MenuItem("Truncate")) {}
            ImGui::EndMenu();
        }
        window_menu();
        ImGui::EndMainMenuBar();
    }

    //ImGui::End();
}

void Editor_tools::window_menu()
{
    if (ImGui::BeginMenu("Window"))
    {
        for (auto window : m_imgui_windows)
        {
            bool enabled = window->is_visibile();
            if (ImGui::MenuItem(window->title().data(), "", &enabled))
            {
                if (enabled) {
                    window->show();
                }
                else
                {
                    window->hide();
                }
            }
        }
        ImGui::MenuItem("Tool Properties", "", &m_show_tool_properties);
        ImGui::Separator();
        if (ImGui::MenuItem("Close All"))
        {
            for (auto window : m_imgui_windows)
            {
                window->hide();
            }
            m_show_tool_properties = false;
        }
        if (ImGui::MenuItem("Open All"))
        {
            for (auto window : m_imgui_windows)
            {
                window->show();
            }
            m_show_tool_properties = true;
        }
        ImGui::EndMenu();
    }
}


void Editor_tools::gui_begin_frame()
{
    ERHE_PROFILE_FUNCTION

    ImGui_ImplErhe_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    menu();    
}

void Editor_tools::imgui_render()
{
    ImGui_ImplErhe_RenderDrawData(ImGui::GetDrawData());
}

void Editor_tools::cancel_ready_tools(Tool* keep)
{
    for (auto tool : m_tools)
    {
        if (tool == keep)
        {
            continue;
        }
        if (tool->state() == Tool::State::Ready)
        {
            log_input_events.trace("Canceling ready tool {}\n", tool->description());
            tool->cancel_ready();
            return;
        }
    }
}

auto Editor_tools::get_action_tool(Action action) const -> Tool*
{
    // clang-format off
    switch (action)
    {
        case Action::select:    return m_selection_tool;
        case Action::add:       return m_brushes;
        case Action::remove:    return m_brushes;
        case Action::translate: return m_trs_tool;
        case Action::rotate:    return m_trs_tool;
        case Action::drag:      return m_physics_tool;
        default:                return nullptr;
    }
    // clang-format on
}

auto Editor_tools::get_priority_action() const -> Action
{
    return m_priority_action;
}

void Editor_tools::set_priority_action(Action action)
{
    log_tools.trace("set_priority_action(action = {})\n", c_action_strings[static_cast<int>(action)]);
    m_priority_action = action;
    auto* tool = get_action_tool(action);

    if (m_trs_tool != nullptr)
    {
        switch (action)
        {
            case Action::translate:
            {
                m_trs_tool->set_rotate(false);
                m_trs_tool->set_translate(true);
                break;
            }
            case Action::rotate:
            {
                m_trs_tool->set_rotate(true);
                m_trs_tool->set_translate(false);
                break;
            }
            default:
            {
                m_trs_tool->set_rotate(false);
                m_trs_tool->set_translate(false);
                break;
            }
        }
    }

    if (tool == nullptr)
    {
        log_tools.trace("tool not found\n");
        return;
    }
    auto i = find(m_tools.begin(), m_tools.end(), tool);
    log_tools.trace("tools:");
    for (auto t : m_tools)
    {
        log_tools.trace(" {}", t->description());
    }
    log_tools.trace("\n");
    if (i == m_tools.begin())
    {
        log_tools.trace("tool {} already first\n", tool->description());
        return;
    }
    if (i != m_tools.end())
    {
        swap(*i, *m_tools.begin());
        log_tools.trace("moved tool {} first\n", tool->description());
        log_tools.trace("tools:");
        for (auto t : m_tools)
        {
            log_tools.trace(" {}", t->description());
        }
        log_tools.trace("\n");
        return;
    }
    log_tools.trace("tool {} is not registered\n", tool->description());
}

void Editor_tools::register_tool(Tool* tool)
{
    lock_guard<mutex> lock(m_mutex);
    m_tools.emplace_back(tool);
    auto* imgui_window = dynamic_cast<Imgui_window*>(tool);
    if (imgui_window != nullptr)
    {
        m_imgui_windows.emplace_back(imgui_window);
    }
}

void Editor_tools::register_background_tool(Tool* tool)
{
    lock_guard<mutex> lock(m_mutex);
    m_background_tools.emplace_back(tool);
    auto* imgui_window = dynamic_cast<Imgui_window*>(tool);
    if (imgui_window != nullptr)
    {
        m_imgui_windows.emplace_back(imgui_window);
    }
}

void Editor_tools::register_imgui_window(Imgui_window* window)
{
    lock_guard<mutex> lock(m_mutex);
    m_imgui_windows.emplace_back(window);
}

void Editor_tools::imgui()
{
    const auto initial_priority_action = get_priority_action();
    if (m_editor_view == nullptr)
    {
        return;
    }

    auto& pointer_context = m_editor_view->pointer_context;
    for (auto imgui_window : m_imgui_windows)
    {
        if (imgui_window->is_visibile())
        {
            imgui_window->imgui(pointer_context);
        }
    }
    if (pointer_context.priority_action != initial_priority_action)
    {
        set_priority_action(pointer_context.priority_action);
    }
    auto priority_action_tool = get_action_tool(pointer_context.priority_action);
    if (m_show_tool_properties && (priority_action_tool != nullptr))
    {
        ImGui::Begin("Tool Properties");
        priority_action_tool->tool_properties();
        ImGui::End();
    }
    // if (m_forward_renderer)
    // {
    //     m_forward_renderer->debug_properties_window();
    // }
}

void Editor_tools::update_and_render_tools(const Render_context& render_context)
{
    ERHE_PROFILE_FUNCTION

    auto& pointer_context = m_editor_view->pointer_context;
    for (auto tool : m_background_tools)
    {
        tool->update(pointer_context);
        tool->render(render_context);
    }
    for (auto tool : m_tools)
    {
        tool->render(render_context);
    }
}

void Editor_tools::render_update_tools(const Render_context& render_context)
{
    ERHE_PROFILE_FUNCTION

    for (auto tool : m_background_tools)
    {
        tool->render_update(render_context);
    }
    for (auto tool : m_tools)
    {
        tool->render_update(render_context);
    }
}

void Editor_tools::update_once_per_frame(const erhe::components::Time_context&)
{
    auto& pointer_context = m_editor_view->pointer_context;

    for (auto tool : m_tools)
    {
        if ((tool->state() == Tool::State::Active) && tool->update(pointer_context))
        {
            return;
        }
    }
}

void Editor_tools::on_pointer()
{
    auto& pointer_context = m_editor_view->pointer_context;

    // Pass 1: Active tools
    for (auto tool : m_tools)
    {
        if ((tool->state() == Tool::State::Active) && tool->update(pointer_context))
        {
            log_input_events.trace("Active tool {} consumed pointer event\n", tool->description());
            cancel_ready_tools(nullptr);
            return;
        }
    }

    log_input_events.trace("No tools are active, looking for ready tools\n");

    // Pass 2: Ready tools
    for (auto tool : m_tools)
    {
        if ((tool->state() == Tool::State::Ready) && tool->update(pointer_context))
        {
            log_input_events.trace("Ready tool {} consumed pointer event\n", tool->description());
            cancel_ready_tools(nullptr);
            return;
        }
    }

    log_input_events.trace("No tools are ready, looking for passive tools\n");

    // Oass 3: Passive tools
    for (auto tool : m_tools)
    {
        if ((tool->state() == Tool::State::Passive) && tool->update(pointer_context))
        {
            log_input_events.trace("Passive tool {} consumed pointer event\n", tool->description());
            cancel_ready_tools(tool);
            return;
        }
    }
}

void Editor_tools::imgui(Pointer_context&)
{
    ImGui::Begin("Editor Tools");

    ImGui::Text("Priority action: %s", c_action_strings[static_cast<int>(m_priority_action)].data());

    const ImGuiTreeNodeFlags leaf_flags{ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Leaf};

    if (ImGui::TreeNodeEx("Active", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto tool : m_tools)
        {
            if (tool->state() == Tool::State::Active)
            {
                ImGui::TreeNodeEx(tool->description(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Ready", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto tool : m_tools)
        {
            if (tool->state() == Tool::State::Ready)
            {
                ImGui::TreeNodeEx(tool->description(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Passive", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto tool : m_tools)
        {
            if (tool->state() == Tool::State::Passive)
            {
                ImGui::TreeNodeEx(tool->description(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Disabled", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto tool : m_tools)
        {
            if (tool->state() == Tool::State::Disabled)
            {
                ImGui::TreeNodeEx(tool->description(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }
    
    ImGui::End();
}

void Editor_tools::delete_selected_meshes()
{
    if (!m_selection_tool)
    {
        return;
    }

    auto scene_root = get<Scene_root>();
    auto& selection = m_selection_tool->selection();
    if (selection.empty())
    {
        return;
    }

    Compound_operation::Context compound_context;
    for (auto node : selection)
    {
        const auto mesh = as_mesh(node);
        if (!mesh)
        {
            continue;
        }

        auto node_physics = get_physics_node(mesh.get());
        auto* parent = mesh->parent();

        Mesh_insert_remove_operation::Context context{
            m_selection_tool,
            scene_root->scene(),
            scene_root->content_layer(),
            scene_root->physics_world(),
            mesh,
            node_physics,
            (parent != nullptr)
                ? parent->shared_from_this()
                : std::shared_ptr<erhe::scene::Node>{},
            Scene_item_operation::Mode::remove
        };
        auto op = make_shared<Mesh_insert_remove_operation>(context);
        compound_context.operations.push_back(op);
    }
    if (compound_context.operations.empty())
    {
        return;
    }

    auto op = make_shared<Compound_operation>(compound_context);
    get<Operation_stack>()->push(op);
}

}  // namespace editor