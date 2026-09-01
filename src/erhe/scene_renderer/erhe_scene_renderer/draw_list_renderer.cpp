#include "erhe_scene_renderer/draw_list_renderer.hpp"

#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::scene_renderer {

namespace {

// The light layer partition the shading variant is selected with: the
// resolved light set's partition carried by the Light_projections (which
// lights got a UBO slot and a shadow layer, see Light_set), so the shader's
// shadow-mapped / non-shadow loop bounds match the UBO slot layout
// Light_buffer::update() writes. Without light projections no light is shaded
// (Light_buffer::update() writes no light data then either).
[[nodiscard]] auto get_light_layer_partition(const Base_render_parameters& base) -> Light_layer_partition
{
    if (base.light_projections != nullptr) {
        return base.light_projections->light_partition;
    }
    return Light_layer_partition{};
}

}

Draw_list_renderer::Draw_list_renderer(
    erhe::graphics::Device& graphics_device,
    Program_interface&      program_interface,
    Scene_pass_resources&   pass_resources
)
    : m_graphics_device     {graphics_device}
    , m_pass_resources      {pass_resources}
    , m_draw_indirect_buffer{graphics_device, program_interface.config.max_draw_count}
    , m_primitive_buffer    {graphics_device, program_interface.primitive_interface}
{
}

Draw_list_renderer::~Draw_list_renderer() noexcept = default;

auto Draw_list_renderer::render(const Render_parameters& parameters) -> Draw_statistics
{
    ERHE_PROFILE_FUNCTION();

    const Base_render_parameters& base = parameters.base;

    // Early out before any upload / bind, mirroring the bucket path's empty
    // mesh-span check: nothing passes the filter for these layers.
    if (!parameters.draw_list_scene.has_drawable_entries(Draw_purpose::color, parameters.layers, parameters.blending, parameters.filter)) {
        return Draw_statistics{};
    }

    erhe::graphics::Render_command_encoder& render_encoder = base.render_encoder;
    Scene_pass_resources::Pass_state pass_state = m_pass_resources.begin_pass(base, parameters.debug_joint_indices, parameters.debug_joint_colors, parameters.debug_target_joint);

    // Environment (R18): recomputed per pass, compared inside draw_color.
    Color_environment environment{};
    environment.light_partition   = get_light_layer_partition(base);
    environment.shadow_filter     = parameters.shadow_filter;
    environment.shadow_bias       = parameters.shadow_bias;
    environment.shadow_technique  = parameters.shadow_technique;
    environment.shadow_depth_bits = parameters.shadow_depth_bits;
    environment.ddgi_enabled      = m_pass_resources.get_ddgi().is_valid();
    // Same convention as the bucket path: 0 for single view, N for multiview.
    const uint16_t multiview_count = (base.views.size() >= 2) ? static_cast<uint16_t>(base.views.size()) : uint16_t{0};

    Draw_statistics statistics{};
    for (erhe::graphics::Base_render_pipeline* base_render_pipeline : parameters.base_render_pipelines) {
        erhe::graphics::Scoped_debug_group pipeline_scope{
            render_encoder.get_command_buffer(),
            base_render_pipeline->data.debug_label
        };
        const Draw_statistics pass_statistics = parameters.draw_list_scene.draw_color(
            Draw_color_parameters{
                .render_encoder       = render_encoder,
                .render_pass          = base.render_pass,
                .base_render_pipeline = *base_render_pipeline,
                .primitive_buffer     = m_primitive_buffer,
                .draw_indirect_buffer = m_draw_indirect_buffer,
                .primitive_settings   = parameters.primitive_settings,
                .filter               = parameters.filter,
                .layers               = parameters.layers,
                .blending             = parameters.blending,
                .multiview_count      = multiview_count,
                .environment          = environment,
                .color_blend_override = parameters.color_blend_override,
                .debug_label          = base.debug_label
            }
        );
        statistics.draw_list_count += pass_statistics.draw_list_count;
        statistics.entry_count     += pass_statistics.entry_count;
        statistics.draw_call_count += pass_statistics.draw_call_count;
    }

    m_pass_resources.end_pass(pass_state, render_encoder);
    return statistics;
}

} // namespace erhe::scene_renderer
