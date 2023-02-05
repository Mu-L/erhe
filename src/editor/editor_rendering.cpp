#include "editor_rendering.hpp"

#include "editor_log.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "rendergraph/post_processing.hpp"
#include "rendertarget_imgui_viewport.hpp"
#include "rendertarget_mesh.hpp"
#include "scene/material_library.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "windows/debug_view_window.hpp"
#include "windows/viewport_config.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/application_view.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/gpu_timer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

using erhe::graphics::Color_blend_state;


#pragma region Commands
Capture_frame_command::Capture_frame_command()
    : Command{"editor.capture_frame"}
{
}

auto Capture_frame_command::try_call() -> bool
{
    g_editor_rendering->trigger_capture();
    return true;
}
#pragma endregion Commands

Editor_rendering* g_editor_rendering{nullptr};

Editor_rendering::Editor_rendering()
    : erhe::components::Component{c_type_name}
{
}

Editor_rendering::~Editor_rendering() noexcept
{
    ERHE_VERIFY(g_editor_rendering == nullptr);
}

void Editor_rendering::deinitialize_component()
{
    ERHE_VERIFY(g_editor_rendering == this);
    m_rp_polygon_fill_standard.reset();
    m_rp_tool1_hidden_stencil.reset();
    m_rp_tool2_visible_stencil.reset();
    m_rp_tool3_depth_clear.reset();
    m_rp_tool4_depth.reset();
    m_rp_tool5_visible_color.reset();
    m_rp_tool6_hidden_color.reset();
    m_rp_line_hidden_blend.reset();
    m_rp_brush_back.reset();
    m_rp_brush_front.reset();
    m_rp_edge_lines.reset();
    m_rp_corner_points.reset();
    m_rp_polygon_centroids.reset();
    m_rp_rendertarget_meshes.reset();

    m_content_timer.reset();
    m_selection_timer.reset();
    m_gui_timer.reset();
    m_brush_timer.reset();
    m_tools_timer.reset();

    g_editor_rendering = nullptr;
}

void Editor_rendering::declare_required_components()
{
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Commands           >();
    require<erhe::application::Configuration      >();
    require<Programs   >();
    require<Mesh_memory>();
}

void Editor_rendering::initialize_component()
{
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(g_editor_rendering == nullptr);

    erhe::application::g_commands->register_command(&m_capture_frame_command);
    erhe::application::g_commands->bind_command_to_key(&m_capture_frame_command, erhe::toolkit::Key_f10);
    g_editor_rendering = this;
}

void Editor_rendering::post_initialize()
{
    ERHE_PROFILE_FUNCTION

    setup_renderpasses();

    // Init here (in main thread) so no scoped GL context needed
    m_content_timer = std::make_unique<erhe::graphics::Gpu_timer>("Content");
}

void Editor_rendering::setup_renderpasses()
{
    ERHE_PROFILE_FUNCTION

    const bool reverse_depth = erhe::application::g_configuration->graphics.reverse_depth;

    auto* vertex_input = g_mesh_memory->vertex_input.get();

    using erhe::graphics::Vertex_input_state;
    using erhe::graphics::Input_assembly_state;
    using erhe::graphics::Rasterization_state;
    using erhe::graphics::Depth_stencil_state;
    using erhe::graphics::Color_blend_state;

    m_rp_polygon_fill_standard.pipeline.data = {
        .name           = "Polygon Fill",
        .shader_stages  = g_programs->circular_brushed_metal.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_disabled
    };

    // Tool pass one: For hidden tool parts, set stencil to 1.
    // Only reads depth buffer, only writes stencil buffer.
    m_rp_tool1_hidden_stencil.pipeline.data = {
        .name                    = "Tool pass 1: Tag depth hidden with stencil = 1",
        .shader_stages           = g_programs->tool.get(),
        .vertex_input            = vertex_input,
        .input_assembly          = Input_assembly_state::triangles,
        .rasterization           = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = false,
            .depth_compare_op    = erhe::application::g_configuration->depth_function(gl::Depth_function::greater),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_hidden,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_hidden,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
        },
        .color_blend             = Color_blend_state::color_writes_disabled
    };

    // Tool pass two: For visible tool parts, set stencil to 2.
    // Only reads depth buffer, only writes stencil buffer.
    m_rp_tool2_visible_stencil.pipeline.data = {
        .name                    = "Tool pass 2: Tag visible tool parts with stencil = 2",
        .shader_stages           = g_programs->tool.get(),
        .vertex_input            = vertex_input,
        .input_assembly          = Input_assembly_state::triangles,
        .rasterization           = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = false,
            .depth_compare_op    = erhe::application::g_configuration->depth_function(gl::Depth_function::lequal),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
        },
        .color_blend             = Color_blend_state::color_writes_disabled
    };

    // Tool pass three: Set depth to fixed value (with depth range)
    // Only writes depth buffer, depth test always.
    m_rp_tool3_depth_clear.pipeline.data = {
        .name           = "Tool pass 3: Set depth to fixed value",
        .shader_stages  = g_programs->tool.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_always_stencil_test_disabled,
        .color_blend    = Color_blend_state::color_writes_disabled
    };
    m_rp_tool3_depth_clear.begin = [](){ gl::depth_range(0.0f, 0.0f); };
    m_rp_tool3_depth_clear.end   = [](){ gl::depth_range(0.0f, 1.0f); };

    // Tool pass four: Set depth to proper tool depth
    // Normal depth buffer update with depth test.
    m_rp_tool4_depth.pipeline.data = {
        .name           = "Tool pass 4: Set depth to proper tool depth",
        .shader_stages  = g_programs->tool.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_writes_disabled
    };

    // Tool pass five: Render visible tool parts
    // Normal depth test, stencil test require 2, color writes enabled, no blending
    m_rp_tool5_visible_color.pipeline.data = {
        .name                    = "Tool pass 5: Render visible tool parts",
        .shader_stages           = g_programs->tool.get(),
        .vertex_input            = vertex_input,
        .input_assembly          = Input_assembly_state::triangles,
        .rasterization           = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = true,
            .depth_compare_op    = erhe::application::g_configuration->depth_function(gl::Depth_function::lequal),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::keep,
                .function        = gl::Stencil_function::equal,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::keep,
                .function        = gl::Stencil_function::equal,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
        },
        .color_blend             = Color_blend_state::color_blend_disabled
    };

    // Tool pass six: Render hidden tool parts
    // Normal depth test, stencil test requires 1, color writes enabled, blending
    m_rp_tool6_hidden_color.pipeline.data = {
        .name                       = "Tool pass 6: Render hidden tool parts",
        .shader_stages              = g_programs->tool.get(),
        .vertex_input               = vertex_input,
        .input_assembly             = Input_assembly_state::triangles,
        .rasterization              = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil = {
            .depth_test_enable      = true,
            .depth_write_enable     = true,
            .depth_compare_op       = erhe::application::g_configuration->depth_function(gl::Depth_function::lequal),
            .stencil_test_enable    = true,
            .stencil_front = {
                .stencil_fail_op    = gl::Stencil_op::keep,
                .z_fail_op          = gl::Stencil_op::keep,
                .z_pass_op          = gl::Stencil_op::keep,
                .function           = gl::Stencil_function::equal,
                .reference          = s_stencil_tool_mesh_hidden,
                .test_mask          = 0xffu,
                .write_mask         = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op    = gl::Stencil_op::keep,
                .z_fail_op          = gl::Stencil_op::keep,
                .z_pass_op          = gl::Stencil_op::replace,
                .function           = gl::Stencil_function::always,
                .reference          = s_stencil_tool_mesh_hidden,
                .test_mask          = 0xffu,
                .write_mask         = 0xffu
            },
        },
        .color_blend = {
            .enabled                = true,
            .rgb = {
                .equation_mode      = gl::Blend_equation_mode::func_add,
                .source_factor      = gl::Blending_factor::constant_alpha,
                .destination_factor = gl::Blending_factor::one_minus_constant_alpha
            },
            .alpha = {
                .equation_mode      = gl::Blend_equation_mode::func_add,
                .source_factor      = gl::Blending_factor::constant_alpha,
                .destination_factor = gl::Blending_factor::one_minus_constant_alpha
            },
            .constant               = { 0.0f, 0.0f, 0.0f, 0.6f }
        }
    };

    m_rp_edge_lines.pipeline.data = {
        .name           = "Edge Lines",
        .shader_stages  = g_programs->wide_lines_draw_color.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::lines,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        //.depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = true,
            .depth_compare_op    = erhe::application::g_configuration->depth_function(gl::Depth_function::lequal),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::incr,
                .function        = gl::Stencil_function::equal,
                .reference       = 0,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::incr,
                .function        = gl::Stencil_function::equal,
                .reference       = 0,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            }
        },

        .color_blend    = Color_blend_state::color_blend_premultiplied
    };
    m_rp_edge_lines.primitive_mode = erhe::primitive::Primitive_mode::edge_lines;

    m_rp_corner_points.pipeline.data = {
        .name           = "Corner Points",
        .shader_stages  = g_programs->points.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::points,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_disabled
    };
    m_rp_corner_points.primitive_mode = erhe::primitive::Primitive_mode::corner_points;

    m_rp_polygon_centroids.pipeline.data = {
        .name           = "Polygon Centroids",
        .shader_stages  = g_programs->points.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::points,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_disabled
    };
    m_rp_polygon_centroids.primitive_mode = erhe::primitive::Primitive_mode::polygon_centroids;

    m_rp_line_hidden_blend.pipeline.data = {
        .name                       = "Hidden lines with blending",
        .shader_stages              = g_programs->wide_lines_draw_color.get(),
        .vertex_input               = vertex_input,
        .input_assembly             = Input_assembly_state::lines,
        .rasterization              = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = {
            .depth_test_enable      = true,
            .depth_write_enable     = false,
            .depth_compare_op       = erhe::application::g_configuration->depth_function(gl::Depth_function::greater),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::incr,
                .function        = gl::Stencil_function::equal,
                .reference       = 0,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::incr,
                .function        = gl::Stencil_function::equal,
                .reference       = 0,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },

        },
        .color_blend = {
            .enabled                = true,
            .rgb = {
                .equation_mode      = gl::Blend_equation_mode::func_add,
                .source_factor      = gl::Blending_factor::constant_alpha,
                .destination_factor = gl::Blending_factor::one_minus_constant_alpha
            },
            .alpha = {
                .equation_mode      = gl::Blend_equation_mode::func_add,
                .source_factor      = gl::Blending_factor::constant_alpha,
                .destination_factor = gl::Blending_factor::one_minus_constant_alpha
            },
            .constant = { 0.0f, 0.0f, 0.0f, 0.2f }
        }
    };
    m_rp_line_hidden_blend.primitive_mode = erhe::primitive::Primitive_mode::edge_lines;

    m_rp_brush_back.pipeline.data = {
        .name           = "Brush back faces",
        .shader_stages  = g_programs->brush.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_front_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    };

    m_rp_brush_front.pipeline.data = {
        .name           = "Brush front faces",
        .shader_stages  = g_programs->brush.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    };

    m_rp_rendertarget_meshes.pipeline.data = {
        .name           = "Rendertarget Meshes",
        .shader_stages  = g_programs->textured.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_none,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    };

    // const erhe::application::Scoped_gl_context gl_context{
    //     Component::get<erhe::application::Gl_context_provider>()
    // };
    //
    // m_content_timer   = std::make_unique<erhe::graphics::Gpu_timer>("Content");
    //m_selection_timer = std::make_unique<erhe::graphics::Gpu_timer>("Selection");
    //m_gui_timer       = std::make_unique<erhe::graphics::Gpu_timer>("Gui");
    //m_brush_timer     = std::make_unique<erhe::graphics::Gpu_timer>("Brush");
    //m_tools_timer     = std::make_unique<erhe::graphics::Gpu_timer>("Tools");
}

void Editor_rendering::update_once_per_frame(const erhe::components::Time_context&)
{
    if (erhe::application::g_line_renderer_set != nullptr) erhe::application::g_line_renderer_set->next_frame();
    if (erhe::application::g_text_renderer     != nullptr) erhe::application::g_text_renderer    ->next_frame();

    if (g_forward_renderer != nullptr) g_forward_renderer ->next_frame();
    if (g_id_renderer      != nullptr) g_id_renderer      ->next_frame();
    if (g_post_processing  != nullptr) g_post_processing  ->next_frame();
    if (g_shadow_renderer  != nullptr) g_shadow_renderer  ->next_frame();
}

void Editor_rendering::trigger_capture()
{
    m_trigger_capture = true;
}

auto Editor_rendering::width() const -> int
{
    return erhe::application::g_view->width();
}

auto Editor_rendering::height() const -> int
{
    return erhe::application::g_view->height();
}

void Editor_rendering::begin_frame()
{
    ERHE_PROFILE_FUNCTION

    if (m_trigger_capture)
    {
        erhe::application::g_window->begin_renderdoc_capture();
    }

    erhe::application::Window_imgui_viewport* imgui_viewport = erhe::application::g_imgui_windows->get_window_viewport().get();

    g_viewport_windows->update_hover(imgui_viewport);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (g_headset_view != nullptr)
    {
        g_headset_view->begin_frame();
    }
#endif
}

void Editor_rendering::end_frame()
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (g_headset_view != nullptr)
    {
        g_headset_view->end_frame();
    }
#endif

    if (g_post_processing != nullptr)
    {
        g_post_processing->next_frame();
    }

    if (m_trigger_capture)
    {
        erhe::application::g_window->end_renderdoc_capture();
        m_trigger_capture = false;
    }
}

void Editor_rendering::render()
{
    ERHE_PROFILE_FUNCTION

    Expects(erhe::application::g_view != nullptr);

    begin_frame();
}

void Editor_rendering::render_viewport_main(
    const Render_context& context,
    const bool            has_pointer
)
{
    ERHE_PROFILE_FUNCTION
    static_cast<void>(has_pointer);

    gl::enable(gl::Enable_cap::scissor_test);
    gl::scissor(context.viewport.x, context.viewport.y, context.viewport.width, context.viewport.height);

    erhe::graphics::g_opengl_state_tracker->shader_stages.reset();
    erhe::graphics::g_opengl_state_tracker->color_blend.execute(Color_blend_state::color_blend_disabled);

    const auto& clear_color = context.viewport_config->clear_color;
    gl::clear_color(
        clear_color[0],
        clear_color[1],
        clear_color[2],
        clear_color[3]
    );
    gl::clear_stencil(0);
    gl::clear_depth_f(*erhe::application::g_configuration->depth_clear_value_pointer());
    gl::clear(
        gl::Clear_buffer_mask::color_buffer_bit |
        gl::Clear_buffer_mask::depth_buffer_bit |
        gl::Clear_buffer_mask::stencil_buffer_bit
    );

    if (g_forward_renderer)
    {
        static constexpr std::string_view c_id_main{"Main"};
        ERHE_PROFILE_GPU_SCOPE(c_id_main);
        erhe::graphics::Scoped_gpu_timer timer{*m_content_timer.get()};
        erhe::graphics::Scoped_debug_group pass_scope{c_id_main};

        render_content  (context, true);
        render_selection(context, true);
        render_brush    (context);

        erhe::graphics::g_opengl_state_tracker->depth_stencil.reset(); // workaround issue in stencil state tracking
    }

    if (erhe::application::g_line_renderer_set != nullptr)
    {
        erhe::application::g_line_renderer_set->begin();
        g_tools->render_tools(context);
        erhe::application::g_line_renderer_set->end();
        erhe::application::g_line_renderer_set->render(context.viewport, *context.camera);
    }

    if (g_forward_renderer)
    {
        render_content            (context, false);
        render_selection          (context, false);
        render_tool_meshes        (context);
        render_rendertarget_meshes(context);
    }

    if (erhe::application::g_text_renderer != nullptr)
    {
        erhe::application::g_text_renderer->render(context.viewport);
    }

    gl::disable(gl::Enable_cap::scissor_test);
}

void Editor_rendering::render_viewport_overlay(
    const Render_context& context,
    const bool            has_pointer
)
{
    ERHE_PROFILE_FUNCTION

    static_cast<void>(context);
    static_cast<void>(has_pointer);

    // TODO move text renderer here when correct framebuffer is bound
}

void Editor_rendering::render_id(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    if (
        (g_id_renderer      == nullptr) ||
        (context.scene_view == nullptr) ||
        (context.camera     == nullptr)
    )
    {
        return;
    }

    const auto scene_root = context.scene_view->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    const auto position_opt = context.viewport_window->get_position_in_viewport();
    if (!position_opt.has_value())
    {
        return;
    }
    const auto position = position_opt.value();

    const auto& layers          = scene_root->layers();
    const auto& tool_scene_root = g_tools->get_tool_scene_root();
    if (!tool_scene_root)
    {
        return;
    }

    const auto& tool_layers = tool_scene_root->layers();

    // TODO listen to viewport changes in msg bus?
    g_id_renderer->render(
        Id_renderer::Render_parameters{
            .viewport           = context.viewport,
            .camera             = context.camera,
            .content_mesh_spans = { layers.content()->meshes, layers.rendertarget()->meshes },
            .tool_mesh_spans    = { tool_layers.tool()->meshes },
            .x                  = static_cast<int>(position.x),
            .y                  = static_cast<int>(position.y)
        }
    );
}

void Editor_rendering::render_content(const Render_context& context, bool polygon_fill)
{
    ERHE_PROFILE_FUNCTION

    if (
        (g_forward_renderer      == nullptr) ||
        (context.scene_view      == nullptr) ||
        (context.camera          == nullptr) ||
        (context.viewport_config == nullptr)
    )
    {
        log_render->error("Missing forward renderer / scene viewport / camera / viewport config - cannot render");
        return;
    }

    const auto scene_root = context.scene_view->get_scene_root();
    if (!scene_root)
    {
        log_render->error("Missing scene root - cannot render");
        return;
    }

    erhe::graphics::Scoped_debug_group outer_debug_scope{"Viewport content"};

    auto& render_style = context.viewport_config->render_style_not_selected;

    const auto& layers           = scene_root->layers();
    const auto& material_library = scene_root->content_library()->materials;
    const auto& materials        = material_library.entries();

    using Item_filter = erhe::scene::Item_filter;
    using Item_flags  = erhe::scene::Item_flags;
    constexpr Item_filter content_not_selected_filter{
        .require_all_bits_set         = Item_flags::visible,
        .require_at_least_one_bit_set = Item_flags::content | Item_flags::controller,
        .require_all_bits_clear       = Item_flags::selected
    };

    if (polygon_fill && render_style.polygon_fill)
    {
        //if (render_style.polygon_offset_enable)
        //{
        //    gl::enable(gl::Enable_cap::polygon_offset_fill);
        //    gl::polygon_offset_clamp(render_style.polygon_offset_factor,
        //                             render_style.polygon_offset_units,
        //                             render_style.polygon_offset_clamp);
        //}
        Renderpass renderpass = m_rp_polygon_fill_standard;
        if (context.override_shader_stages != nullptr)
        {
            renderpass.pipeline.data.shader_stages = context.override_shader_stages;
        }
        g_forward_renderer->render(
            {
                .ambient_light     = layers.light()->ambient_light,
                .camera            = context.camera,
                .light_projections = context.scene_view->get_light_projections(),
                .lights            = layers.light()->lights,
                .materials         = materials,
                .mesh_spans        = { layers.content()->meshes, layers.controller()->meshes },
                .passes            = { &renderpass },
                .shadow_texture    = context.scene_view->get_shadow_texture(),
                .viewport          = context.viewport,
                .filter            = content_not_selected_filter,
            }
        );
        //gl::disable(gl::Enable_cap::polygon_offset_line);
        return;
    }

    auto& primitive_settings = g_forward_renderer->primitive_settings();

    if (render_style.edge_lines)
    {
        gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
        primitive_settings.color_source   = render_style.edge_lines_color_source;// Base_renderer::Primitive_color_source::constant_color;
        primitive_settings.constant_color = render_style.line_color;
        primitive_settings.size_source    = Primitive_size_source::constant_size;
        primitive_settings.constant_size  = render_style.line_width;
        g_forward_renderer->render(
            {
                .camera     = context.camera,
                .materials  = materials,
                .mesh_spans = { layers.content()->meshes },
                .passes     = { &m_rp_edge_lines },
                .viewport   = context.viewport,
                .filter     = content_not_selected_filter
            }
        );
        gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    }

    if (render_style.polygon_centroids)
    {
        primitive_settings.color_source   = render_style.polygon_centroids_color_source; // Base_renderer::Primitive_color_source::constant_color;
        primitive_settings.constant_color = render_style.centroid_color;
        primitive_settings.size_source    = Primitive_size_source::constant_size;
        primitive_settings.constant_size  = render_style.point_size;
        g_forward_renderer->render(
            {
                .camera     = context.camera,
                .materials  = materials,
                .mesh_spans = { layers.content()->meshes },
                .passes     = { &m_rp_polygon_centroids },
                .viewport   = context.viewport,
                .filter     = content_not_selected_filter
            }
        );
    }

    if (render_style.corner_points)
    {
        primitive_settings.color_source   = render_style.corner_points_color_source; // Base_renderer::Primitive_color_source::constant_color;
        primitive_settings.constant_color = render_style.corner_color;
        primitive_settings.size_source    = Primitive_size_source::constant_size;
        primitive_settings.constant_size  = render_style.point_size;
        g_forward_renderer->render(
            {
                .camera     = context.camera,
                .materials  = materials,
                .mesh_spans = { layers.content()->meshes },
                .passes     = { &m_rp_corner_points },
                .viewport   = context.viewport,
                .filter     = content_not_selected_filter
            }
        );
    }
}

void Editor_rendering::render_rendertarget_meshes(
    const Render_context& context
)
{
    ERHE_PROFILE_FUNCTION

    if (
        (g_forward_renderer      == nullptr) ||
        (context.scene_view      == nullptr) ||
        (context.camera          == nullptr) ||
        (context.viewport_config == nullptr)
    )
    {
        return;
    }

    const auto scene_root = context.scene_view->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    erhe::graphics::Scoped_debug_group outer_debug_scope{"Viewport rendertarget meshes"};

    const auto& layers           = scene_root->layers();
    const auto& material_library = scene_root->content_library()->materials;
    const auto& materials        = material_library.entries();

    g_forward_renderer->render(
        Forward_renderer::Render_parameters{
            .camera     = context.camera,
            .materials  = { materials },
            .mesh_spans = { layers.rendertarget()->meshes },
            .passes     = { &m_rp_rendertarget_meshes },
            .viewport   = context.viewport,
            .filter =
            {
                .require_all_bits_set = (
                    erhe::scene::Item_flags::visible |
                    erhe::scene::Item_flags::rendertarget
                )
            }
        }
    );
}

void Editor_rendering::render_selection(const Render_context& context, bool polygon_fill)
{
    if (
        (context.camera          == nullptr) ||
        (context.viewport_config == nullptr) ||
        (context.scene_view      == nullptr)
    )
    {
        return;
    }

    const auto scene_root = context.scene_view->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    const auto& render_style     = context.viewport_config->render_style_selected;
    const auto& layers           = scene_root->layers();
    const auto& material_library = scene_root->content_library()->materials;
    const auto& materials        = material_library.entries();

    constexpr erhe::scene::Item_filter content_selected_filter{
        .require_all_bits_set =
            erhe::scene::Item_flags::visible |
            erhe::scene::Item_flags::content |
            erhe::scene::Item_flags::selected
    };

    if (polygon_fill && render_style.polygon_fill)
    {
        //if (render_style.polygon_offset_enable)
        //{
        //    gl::enable(gl::Enable_cap::polygon_offset_fill);
        //    gl::polygon_offset_clamp(render_style.polygon_offset_factor,
        //                             render_style.polygon_offset_units,
        //                             render_style.polygon_offset_clamp);
        //}
        //m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        //m_forward_renderer->primitive_constant_color = render_style.line_color;

        Renderpass renderpass = m_rp_polygon_fill_standard;
        if (context.override_shader_stages != nullptr)
        {
            renderpass.pipeline.data.shader_stages = context.override_shader_stages;
        }

        g_forward_renderer->render(
            {
                .ambient_light     = layers.light()->ambient_light,
                .camera            = context.camera,
                .light_projections = context.scene_view->get_light_projections(),
                .lights            = layers.light()->lights,
                .materials         = materials,
                .mesh_spans        = { layers.content()->meshes },
                .passes            = { &renderpass },
                .shadow_texture    = context.scene_view->get_shadow_texture(),
                .viewport          = context.viewport,
                .filter            = content_selected_filter
            }
        );
        //gl::disable(gl::Enable_cap::polygon_offset_line);
        return;
    }

    auto& primitive_settings = g_forward_renderer->primitive_settings();

    if (render_style.edge_lines)
    {
        ERHE_PROFILE_SCOPE("selection edge lines");

        gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
        primitive_settings.color_source   = render_style.edge_lines_color_source;;
        primitive_settings.constant_color = render_style.line_color;
        primitive_settings.size_source    = Primitive_size_source::constant_size;
        primitive_settings.constant_size  = render_style.line_width;
        g_forward_renderer->render(
            {
                .camera     = context.camera,
                .materials  = materials,
                .mesh_spans = { layers.content()->meshes },
                .passes     = { &m_rp_edge_lines, &m_rp_line_hidden_blend },
                .viewport   = context.viewport,
                .filter     = content_selected_filter
            }
        );
        gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    }

    gl::enable(gl::Enable_cap::program_point_size);
    if (render_style.polygon_centroids)
    {
        ERHE_PROFILE_SCOPE("selection polygon centroids");

        primitive_settings.color_source   = render_style.polygon_centroids_color_source;
        primitive_settings.size_source    = Primitive_size_source::constant_size;
        primitive_settings.constant_color = render_style.centroid_color;
        primitive_settings.constant_size  = render_style.point_size;
        g_forward_renderer->render(
            {
                .camera     = context.camera,
                .materials  = materials,
                .mesh_spans = { layers.content()->meshes },
                .passes     = { &m_rp_polygon_centroids },
                .viewport   = context.viewport,
                .filter     = content_selected_filter
            }
        );
    }
    if (render_style.corner_points)
    {
        ERHE_PROFILE_SCOPE("selection corner points");

        primitive_settings.color_source   = render_style.corner_points_color_source;
        primitive_settings.size_source    = Primitive_size_source::constant_size;
        primitive_settings.constant_color = render_style.corner_color;
        primitive_settings.constant_size  = render_style.point_size;
        g_forward_renderer->render(
            {
                .camera     = context.camera,
                .materials  = materials,
                .mesh_spans = { layers.content()->meshes },
                .passes     = { &m_rp_corner_points },
                .viewport   = context.viewport,
                .filter     = content_selected_filter
            }
        );
    }
    gl::disable(gl::Enable_cap::program_point_size);
}

void Editor_rendering::render_tool_meshes(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    if (
        (context.camera     == nullptr) ||
        (context.scene_view == nullptr)
    )
    {
        return;
    }

    const auto& scene_root = g_tools->get_tool_scene_root();
    if (!scene_root)
    {
        return;
    }

    const auto& layers           = scene_root->layers();
    const auto& material_library = scene_root->content_library()->materials;
    const auto& materials        = material_library.entries();

    if (layers.tool()->meshes.empty())
    {
        return;
    }

    g_forward_renderer->render(
        {
            .camera     = context.camera,
            .lights     = {},
            .materials  = materials,
            .mesh_spans = { layers.tool()->meshes },
            .passes     =
            {
                &m_rp_tool1_hidden_stencil,   // tag_depth_hidden_with_stencil
                &m_rp_tool2_visible_stencil,  // tag_depth_visible_with_stencil
                &m_rp_tool3_depth_clear,      // clear_depth
                &m_rp_tool4_depth,            // depth_only
                &m_rp_tool5_visible_color,    // require_stencil_tag_depth_visible
                &m_rp_tool6_hidden_color      // require_stencil_tag_depth_hidden_and_blend,
            },
            .viewport   = context.viewport,
            .filter     =
            {
                .require_all_bits_set =
                    erhe::scene::Item_flags::visible |
                    erhe::scene::Item_flags::tool
            }
        }
    );
}

void Editor_rendering::render_brush(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    if (
        (context.camera     == nullptr) ||
        (context.scene_view == nullptr)
    )
    {
        return;
    }

    const auto scene_root = context.scene_view->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    const auto& layers           = scene_root->layers();
    const auto& material_library = scene_root->content_library()->materials;
    const auto& materials        = material_library.entries();

    if (layers.brush()->meshes.empty())
    {
        return;
    }

    g_forward_renderer->render(
        {
            .ambient_light = layers.light()->ambient_light,
            .camera        = context.camera,
            .lights        = layers.light()->lights,
            .materials     = materials,
            .mesh_spans    = { layers.brush()->meshes },
            .passes        = { &m_rp_brush_back, &m_rp_brush_front },
            .viewport      = context.viewport,
            .filter        =
            {
                .require_all_bits_set =
                    erhe::scene::Item_flags::visible |
                    erhe::scene::Item_flags::brush
            }
        }
    );
}

}  // namespace editor
