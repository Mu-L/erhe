#include "erhe_graphics/null/null_render_pass.hpp"
#include "erhe_graphics/null/null_device.hpp"
#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

Render_pass_impl* Device_impl::s_active_render_pass{nullptr};

Render_pass_impl::Render_pass_impl(Device& device, const Render_pass_descriptor& render_pass_descriptor)
    : m_device              {device}
    , m_swapchain           {render_pass_descriptor.swapchain}
    , m_render_target_width {render_pass_descriptor.render_target_width}
    , m_render_target_height{render_pass_descriptor.render_target_height}
    , m_debug_label         {render_pass_descriptor.debug_label}
{
}

Render_pass_impl::~Render_pass_impl() noexcept = default;

auto Render_pass_impl::gl_name() const -> unsigned int
{
    return 0;
}

auto Render_pass_impl::gl_multisample_resolve_name() const -> unsigned int
{
    return 0;
}

auto Render_pass_impl::get_sample_count() const -> unsigned int
{
    return 1;
}

auto Render_pass_impl::check_status() const -> bool
{
    return true;
}

auto Render_pass_impl::get_render_target_width() const -> int
{
    return m_render_target_width;
}

auto Render_pass_impl::get_render_target_height() const -> int
{
    return m_render_target_height;
}

auto Render_pass_impl::get_swapchain() const -> Swapchain*
{
    return m_swapchain;
}

auto Render_pass_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

void Render_pass_impl::start_render_pass(Command_buffer& command_buffer, Render_pass* const render_pass_before, Render_pass* const render_pass_after)
{
    // No-op in null backend
    static_cast<void>(command_buffer);
    static_cast<void>(render_pass_before);
    static_cast<void>(render_pass_after);
}

void Render_pass_impl::end_render_pass(Command_buffer& command_buffer, Render_pass* const render_pass_after)
{
    // No-op in null backend
    static_cast<void>(command_buffer);
    static_cast<void>(render_pass_after);
}

} // namespace erhe::graphics
