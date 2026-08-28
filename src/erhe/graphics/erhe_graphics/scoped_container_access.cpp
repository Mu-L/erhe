#include "erhe_graphics/scoped_container_access.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_render_pass.hpp"
# include "erhe_graphics/gl/gl_vertex_input_state.hpp"
# include "erhe_graphics/render_pass.hpp"
# include "erhe_graphics/state/vertex_input_state.hpp"
#endif

namespace erhe::graphics {

#if defined(ERHE_GRAPHICS_API_OPENGL)

Scoped_vertex_input_state::Scoped_vertex_input_state(Device& device, const Vertex_input_state& vertex_input_state)
{
    static_cast<void>(device);
    m_gl_name = vertex_input_state.get_impl().ensure_created_on_current_context();
}

Scoped_framebuffer::Scoped_framebuffer(Device& device, const Render_pass& render_pass)
{
    static_cast<void>(device);
    const Render_pass_impl& render_pass_impl = render_pass.get_impl();
    m_gl_name = render_pass_impl.ensure_created_on_current_context();
    m_gl_multisample_resolve_name = render_pass_impl.gl_multisample_resolve_name();
}

#else

Scoped_vertex_input_state::Scoped_vertex_input_state(Device& device, const Vertex_input_state& vertex_input_state)
{
    static_cast<void>(device);
    static_cast<void>(vertex_input_state);
}

Scoped_framebuffer::Scoped_framebuffer(Device& device, const Render_pass& render_pass)
{
    static_cast<void>(device);
    static_cast<void>(render_pass);
}

#endif

Scoped_vertex_input_state::~Scoped_vertex_input_state() noexcept = default;

auto Scoped_vertex_input_state::gl_name() const -> unsigned int
{
    return m_gl_name;
}

Scoped_framebuffer::~Scoped_framebuffer() noexcept = default;

auto Scoped_framebuffer::gl_name() const -> unsigned int
{
    return m_gl_name;
}

auto Scoped_framebuffer::gl_multisample_resolve_name() const -> unsigned int
{
    return m_gl_multisample_resolve_name;
}

} // namespace erhe::graphics
