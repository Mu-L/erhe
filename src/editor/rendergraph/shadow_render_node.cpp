#include "rendergraph/shadow_render_node.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"

#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/content_library.hpp"

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Shadow_render_node::Shadow_render_node(
    erhe::graphics::Device&         graphics_device,
    erhe::rendergraph::Rendergraph& rendergraph,
    Editor_context&                 editor_context,
    Scene_view&                     scene_view,
    const int                       resolution,
    const int                       light_count
)
    // TODO fmt::format("Shadow render {}", viewport_scene_view->name())
    : erhe::rendergraph::Rendergraph_node{rendergraph, "shadow_maps"} 
    , m_context   {editor_context}
    , m_scene_view{scene_view}
{
    register_output(
        erhe::rendergraph::Routing::Resource_provided_by_producer,
        "shadow_maps",
        erhe::rendergraph::Rendergraph_node_key::shadow_maps
    );

    reconfigure(graphics_device, resolution, light_count);
}

void Shadow_render_node::reconfigure(erhe::graphics::Device& graphics_device, const int resolution, const int light_count)
{
    log_render->trace("Reconfigure shadow resolution = {}, light count = {}", resolution, light_count);

    const bool reverse_depth = graphics_device.configuration.reverse_depth;
    {
        ERHE_PROFILE_SCOPE("allocating shadow map array texture");

        m_texture.reset();
        m_texture = std::make_shared<Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info {
                .device          = graphics_device,
                .target          = gl::Texture_target::texture_2d_array,
                .internal_format = gl::Internal_format::depth_component32f,
                //.sparse          = erhe::graphics::g_instance->info.use_sparse_texture,
                .width           = std::max(1, resolution),
                .height          = std::max(1, resolution),
                .depth           = std::max(1, light_count),
                .debug_label     = "Shadowmap"
            }
        );

        m_texture->set_debug_label("Shadowmaps");

        if (resolution <= 1) {
            float depth_clear_value = reverse_depth ? 0.0f : 1.0f;
            if (gl::is_command_supported(gl::Command::Command_glClearTexImage)) {
                gl::clear_tex_image(m_texture->gl_name(), 0, gl::Pixel_format::depth_component, gl::Pixel_type::float_, &depth_clear_value);
            } else {
                // TODO
            }
        }
    }

    m_framebuffers.clear();
    for (int i = 0; i < light_count; ++i) {
        ERHE_PROFILE_SCOPE("framebuffer creation");

        Framebuffer::Create_info create_info;
        create_info.attach(gl::Framebuffer_attachment::depth_attachment, m_texture.get(), 0, static_cast<unsigned int>(i));
        auto framebuffer = std::make_unique<Framebuffer>(graphics_device, create_info);
        framebuffer->set_debug_label(fmt::format("Shadow {}", i));
        m_framebuffers.emplace_back(std::move(framebuffer));
    }

    m_viewport = {
        .x             = 0,
        .y             = 0,
        .width         = resolution,
        .height        = resolution,
        .reverse_depth = reverse_depth
    };
}

void Shadow_render_node::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

    // Render shadow maps
    const auto& scene_root = m_scene_view.get_scene_root();
    const auto& camera     = m_scene_view.get_camera();
    if (!scene_root || !camera) {
        return;
    }

    const auto& layers = scene_root->layers();
    if (layers.content()->meshes.empty()) {
        return;
    }

    if (!m_texture) {
        return;
    }

    // TODO: Keep this vector in content library and update when needed.
    //       Make content library item host for content library nodes.
    const auto& material_library = scene_root->content_library()->materials;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials = material_library->get_all<erhe::primitive::Material>();

    scene_root->sort_lights();

    m_context.shadow_renderer->render(
        erhe::scene_renderer::Shadow_renderer::Render_parameters{
            .vertex_input_state    = &m_context.mesh_memory->vertex_input,
            .index_type            = m_context.mesh_memory->buffer_info.index_type,
            .index_buffer          = &m_context.mesh_memory->index_buffer,
            .vertex_buffer         = &m_context.mesh_memory->position_vertex_buffer,
            .view_camera           = camera.get(),
            .view_camera_viewport  = {},
            .light_camera_viewport = m_viewport,
            .texture               = m_texture,
            .framebuffers          = m_framebuffers,
            .mesh_spans            = { layers.content()->meshes },
            .lights                = layers.light()->lights,
            .skins                 = scene_root->get_scene().get_skins(),
            .materials             = materials,
            .light_projections     = m_light_projections
        }
    );
}

auto Shadow_render_node::get_producer_output_texture(erhe::rendergraph::Routing, int, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

auto Shadow_render_node::get_producer_output_viewport(erhe::rendergraph::Routing, const int key, int) const -> erhe::math::Viewport
{
    ERHE_VERIFY(key == erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    return m_viewport;
}

auto Shadow_render_node::get_scene_view() -> Scene_view&
{
    return m_scene_view;
}

auto Shadow_render_node::get_scene_view() const -> const Scene_view&
{
    return m_scene_view;
}

auto Shadow_render_node::get_light_projections() -> erhe::scene_renderer::Light_projections&
{
    return m_light_projections;
}

auto Shadow_render_node::get_texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

auto Shadow_render_node::get_viewport() const -> erhe::math::Viewport
{
    return m_viewport;
}

auto Shadow_render_node::inputs_allowed() const -> bool
{
    return false;
}

} // namespace editor
