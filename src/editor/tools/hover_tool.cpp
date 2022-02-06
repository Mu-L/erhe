#include "hover_tool.hpp"
#include "editor_tools.hpp"
#include "log.hpp"
#include "rendering.hpp"

#include "tools/hover_tool.hpp"
#include "tools/pointer_context.hpp"
#include "tools/trs_tool.hpp"

#include "renderers/line_renderer.hpp"
#include "renderers/render_context.hpp"
#include "renderers/text_renderer.hpp"

#include "scene/scene_root.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/core.h>
#include <fmt/format.h>
#include <imgui.h>

#include <string>

namespace editor
{

using glm::vec3;

void Hover_tool_hover_command::on_inactive(Command_context& context)
{
    static_cast<void>(context);

    m_hover_tool.on_inactive();
}

auto Hover_tool_hover_command::try_call(Command_context& context) -> bool
{
    if (context.viewport_window() == nullptr)
    {
        set_inactive(context);
        return false;
    }

    return m_hover_tool.try_call();
}

Hover_tool::Hover_tool()
    : erhe::components::Component{c_name}
{
}

Hover_tool::~Hover_tool() = default;

auto Hover_tool::description() -> const char*
{
    return c_description.data();
}

void Hover_tool::connect()
{
    m_line_renderer_set = get    <Line_renderer_set>();
    m_pointer_context   = get    <Pointer_context  >();
    m_scene_root        = require<Scene_root       >();
    m_text_renderer     = get    <Text_renderer    >();
    require<Editor_tools        >();
}

void Hover_tool::initialize_component()
{
    m_hover_material = m_scene_root->make_material("hover");
    m_hover_material_index = m_hover_material->index;
    get<Editor_tools>()->register_background_tool(this);
}

void Hover_tool::on_inactive()
{
    m_hover_content = false;
    m_hover_tool    = false;
    m_hover_position_world.reset();
    m_hover_normal.reset();
    deselect();
}

auto Hover_tool::try_call() -> bool
{
    m_hover_content        = m_pointer_context->hovering_over_content();
    m_hover_tool           = m_pointer_context->hovering_over_tool();
    m_hover_position_world = m_hover_content ? m_pointer_context->position_in_world() : nonstd::optional<vec3>{};
    m_hover_normal         = m_hover_content ? m_pointer_context->hover_normal()      : nonstd::optional<vec3>{};
    if (
        (m_pointer_context->hover_mesh()      != m_hover_mesh) ||
        (m_pointer_context->hover_primitive() != m_hover_primitive_index)
    )
    {
        deselect();
        select();
    }

    return false;
}

void Hover_tool::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    auto& line_renderer = m_line_renderer_set->hidden;

    constexpr uint32_t red   = 0xff0000ffu;
    constexpr uint32_t green = 0xff00ff00u;
    constexpr uint32_t blue  = 0xffff0000u;
    constexpr uint32_t white = 0xffffffffu;

    const uint32_t text_color =
        m_hover_content
            ? white
            : m_hover_tool
                ? blue
                : red;

    if (m_hover_mesh != nullptr)
    {
        if (m_enable_color_highlight)
        {
            // float t = 0.5f + std::cos(render_context.time * glm::pi<float>()) * 0.5f;
            const float t = 1.0f;
            m_hover_material->emissive = m_original_primitive_material->emissive + t * m_hover_emissive;
        }

        if (
            (context.window != nullptr) &&
            m_hover_position_world.has_value()
        )
        {
            const auto position_in_viewport = context.window->project_to_viewport(m_hover_position_world.value());
            const vec3 position_at_fixed_depth{
                position_in_viewport.x + 50.0f,
                position_in_viewport.y,
                -0.5f
            };
            const std::string text = fmt::format(
                "{}",
                m_hover_mesh->name()
            );
            m_text_renderer->print(
                position_at_fixed_depth,
                text_color,
                text
            );
        }

        if (
            m_hover_position_world.has_value() &&
            m_hover_normal.has_value()
        )
        {
            const vec3 p0 = m_hover_position_world.value() + 0.0f * m_hover_normal.value();
            const vec3 p1 = m_hover_position_world.value() + 1.0f * m_hover_normal.value();
            line_renderer.set_line_color(0xff0000ffu);
            line_renderer.add_lines(
                {
                    {
                        p0,
                        p1
                    }
                },
                10.0f
            );
        }
#if 1
        if (m_pointer_context->raytrace_hit_position().has_value())
        {
            const vec3 p0 = m_pointer_context->raytrace_hit_position().value();
            const vec3 neg_x = p0 - vec3{1.0f, 0.0f, 0.0f};
            const vec3 pos_x = p0 + vec3{1.0f, 0.0f, 0.0f};
            const vec3 neg_y = p0 - vec3{0.0f, 1.0f, 0.0f};
            const vec3 pos_y = p0 + vec3{0.0f, 1.0f, 0.0f};
            const vec3 neg_z = p0 - vec3{0.0f, 0.0f, 1.0f};
            const vec3 pos_z = p0 + vec3{0.0f, 0.0f, 1.0f};
            m_line_renderer_set->hidden.set_line_color(red);
            m_line_renderer_set->hidden.add_lines(
                {
                    {
                        neg_x,
                        pos_x
                    }
                },
                10.0f
            );
            m_line_renderer_set->hidden.set_line_color(green);
            m_line_renderer_set->hidden.add_lines(
                {
                    {
                        neg_y,
                        pos_y
                    }
                },
                10.0f
            );
            line_renderer.set_line_color(blue);
            line_renderer.add_lines(
                {
                    {
                        neg_z,
                        pos_z
                    }
                },
                10.0f
            );
        }
#endif
    }
}

void Hover_tool::deselect()
{
    ERHE_PROFILE_FUNCTION

    if (m_hover_mesh == nullptr)
    {
        return;
    }

    if (m_enable_color_highlight)
    {
        // Restore original primitive material
        m_hover_mesh->data.primitives[m_hover_primitive_index].material = m_original_primitive_material;
        m_original_primitive_material.reset();
        m_hover_mesh = nullptr;
    }
}

void Hover_tool::select()
{
    ERHE_PROFILE_FUNCTION

    if (m_pointer_context->hover_mesh() == nullptr)
    {
        return;
    }

    // Update hover information
    m_hover_mesh            = m_pointer_context->hover_mesh();
    m_hover_primitive_index = m_pointer_context->hover_primitive();

    if (m_enable_color_highlight)
    {
        // Store original primitive material
        m_original_primitive_material = m_hover_mesh->data.primitives[m_hover_primitive_index].material;

        // Copy material and mutate it with increased material emissive
        *m_hover_material.get()    = *m_original_primitive_material.get();
        m_hover_material->name     = "hover";
        m_hover_material->emissive = m_original_primitive_material->emissive + m_hover_emissive;
        m_hover_material->index    = m_hover_material_index;

        // Replace primitive material with modified copy
        m_hover_mesh->data.primitives[m_hover_primitive_index].material = m_hover_material;
        log_materials.info(
            "hover mesh {} primitive {} set to {} with index {}\n",
            m_hover_mesh->name(),
            m_hover_primitive_index,
            m_hover_material->name,
            m_hover_material->index
        );
    }
}

}
