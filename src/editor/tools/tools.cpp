#include "tools/tools.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "tools/tool.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

Tools::Tools(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    App_settings&                /*app_settings*/
)
    : m_context{app_context}
{
    ERHE_PROFILE_FUNCTION();

    const auto tools_content_library = std::make_shared<Content_library>();

    // No Draw_list_scene: tool passes use override_with_base_render_pipeline
    // and stay on the Forward_renderer fallback (constructed before the
    // renderer dependencies exist anyway).
    m_scene_root = std::make_shared<Scene_root>(
        nullptr, // Do not process editor messages
        tools_content_library,
        "Tool scene",
        false,
        nullptr
    );

    m_scene_root->get_scene().disable_flag_bits(erhe::Item_flags::show_in_ui);

    for (const auto& tool : m_tools) {
        const auto priority = tool->get_priority();
        tool->handle_priority_update(priority + 1, priority);
    }

    if (app_context.developer_mode) {
        m_content_library_tree_window = std::make_shared<Item_tree_window>(
            imgui_renderer,
            imgui_windows,
            app_context,
            "Tools Library",
            "tools_content_library"
        );
        m_content_library_tree_window->set_root(tools_content_library->root);
        m_content_library_tree_window->set_item_filter(
            erhe::Item_filter{
                .require_all_bits_set           = 0,
                .require_at_least_one_bit_set   = 0,
                .require_all_bits_clear         = 0,
                .require_at_least_one_bit_clear = 0
            }
        );
        m_content_library_tree_window->set_developer();
    }
}

auto Tools::get_tool_scene_root() -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

void Tools::register_tool(Tool* tool)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const auto flags = tool->get_flags();
    if (erhe::utility::test_bit_set(flags, Tool_flags::background)) {
        m_background_tools.emplace_back(tool);
    } else {
        m_tools.emplace_back(tool);
    }
}

void Tools::update_transforms()
{
    ERHE_PROFILE_FUNCTION();

    erhe::scene::Scene& scene = m_scene_root->get_scene();
    scene.update_node_transforms();
}

void Tools::render_viewport_tools(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(context.command_buffer != nullptr);
    erhe::graphics::Scoped_debug_group debug_group{*context.command_buffer, "Tools"};

    for (const auto& tool : m_background_tools) {
        tool->tool_render(context);
    }
    for (const auto& tool : m_tools) {
        tool->tool_render(context);
    }
}

void Tools::set_priority_tool(Tool* priority_tool)
{
    if (m_priority_tool == priority_tool) {
        return;
    }

    if (m_priority_tool != nullptr) {
        log_tools->trace("de-prioritizing tool {}", m_priority_tool->get_description());
        m_priority_tool->set_priority_boost(0);
    }

    m_priority_tool = priority_tool;

    if (m_priority_tool != nullptr) {
        log_tools->trace("prioritizing tool {}", m_priority_tool->get_description());
        constexpr int c_priority_tool_boost = 100;
        m_priority_tool->set_priority_boost(c_priority_tool_boost);
    } else {
        log_tools->trace("active tool reset");
    }

    {
        using namespace erhe::utility;
        const bool allow_secondary =
            (m_priority_tool != nullptr) &&
            test_bit_set(m_priority_tool->get_flags(), Tool_flags::allow_secondary);
        log_tools->trace("Update tools: allow_secondary = {}", allow_secondary);
        for (auto* tool : m_tools) {
            const auto flags = tool->get_flags();
            if (test_bit_set(flags, Tool_flags::toolbox)) {
                const bool is_priority_tool = (tool == m_priority_tool);
                const bool is_secondary     = test_bit_set(flags, Tool_flags::secondary);
                const bool enable           = is_priority_tool || (allow_secondary && is_secondary);
                tool->set_enabled(enable);
                log_tools->trace(
                    "{} {}{}{}", tool->get_description(),
                    is_priority_tool ? "priority " : "",
                    is_secondary     ? "secondary " : "",
                    enable           ? "-> enabled" : "-> disabled"
                );
            }
        }
    }

    m_context.commands->sort_bindings();
    m_context.app_message_bus->tool_select.send_message(
        Tool_select_message{}
    );
}

auto Tools::get_priority_tool() const -> Tool*
{
    return m_priority_tool;
}

auto Tools::get_tools() const -> const std::vector<Tool*>&
{
    return m_tools;
}

}  // namespace editor
