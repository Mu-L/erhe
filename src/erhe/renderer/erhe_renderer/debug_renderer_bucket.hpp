#pragma once

#include "erhe_graphics/pipeline.hpp"
#include "erhe_graphics/instance.hpp"
#include <vector>

namespace erhe::graphics {
    class Shader_stages;
}

namespace erhe::renderer {

class Debug_renderer;

class Debug_renderer_config
{
public:
    gl::Primitive_type primitive_type   {gl::Primitive_type{0}};
    unsigned int       stencil_reference{0};
    bool               draw_visible     {true};
    bool               draw_hidden      {false};
    bool               reverse_depth    {true};
};

auto operator==(const Debug_renderer_config& lhs, const Debug_renderer_config& rhs) -> bool;

class Debug_draw_entry
{
public:
    erhe::graphics::Buffer_range input_buffer_range;
    erhe::graphics::Buffer_range draw_buffer_range;
    std::size_t                  primitive_count;
};

class Debug_renderer_bucket
{
public:
    Debug_renderer_bucket(erhe::graphics::Instance& graphics_instance, Debug_renderer& debug_renderer, Debug_renderer_config config);

    void clear           ();
    auto match           (const Debug_renderer_config& config) const -> bool;
    void dispatch_compute();
    void render          (bool draw_hidden, bool draw_visible);
    void release_buffers ();
    auto make_draw       (std::size_t vertex_byte_count, std::size_t primitive_count) -> std::span<std::byte>;

private:
    [[nodiscard]] auto make_pipeline(bool visible) -> erhe::graphics::Pipeline;

    erhe::graphics::Instance&              m_graphics_instance;
    Debug_renderer&                        m_debug_renderer;
    erhe::graphics::GPU_ring_buffer_client m_vertex_ssbo_buffer;
    erhe::graphics::GPU_ring_buffer_client m_triangle_vertex_buffer;
    Debug_renderer_config                  m_config;
    erhe::graphics::Shader_stages*         m_compute_shader_stages{nullptr};
    erhe::graphics::Pipeline               m_compute;
    erhe::graphics::Pipeline               m_pipeline_visible;
    erhe::graphics::Pipeline               m_pipeline_hidden;
    std::vector<Debug_draw_entry>          m_draws;
};

} // namespace erhe::renderer

