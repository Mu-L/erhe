#pragma once

#include "erhe_graphics/render_pass.hpp"
#include "erhe_utility/debug_label.hpp"

namespace erhe::graphics {

class Render_pipeline_state;

class Render_pass_impl final
{
public:
    Render_pass_impl (Device& device, const Render_pass_descriptor& render_pass_descriptor);
    ~Render_pass_impl() noexcept;
    Render_pass_impl (const Render_pass_impl&) = delete;
    void operator=   (const Render_pass_impl&) = delete;
    Render_pass_impl (Render_pass_impl&&)      = delete;
    void operator=   (Render_pass_impl&&)      = delete;

    [[nodiscard]] auto gl_name                    () const -> unsigned int;
    [[nodiscard]] auto gl_multisample_resolve_name() const -> unsigned int;
    [[nodiscard]] auto get_sample_count           () const -> unsigned int;

    auto check_status() const -> bool;

    [[nodiscard]] auto get_render_target_width () const -> int;
    [[nodiscard]] auto get_render_target_height() const -> int;
    [[nodiscard]] auto get_swapchain           () const -> Swapchain*;
    [[nodiscard]] auto get_debug_label         () const -> erhe::utility::Debug_label;

private:
    friend class Render_pass;
    void start_render_pass(Command_buffer& command_buffer, Render_pass* render_pass_before, Render_pass* render_pass_after);
    void end_render_pass  (Command_buffer& command_buffer, Render_pass* render_pass_after);

private:
    Device&                    m_device;
    Swapchain*                 m_swapchain{nullptr};
    int                        m_render_target_width{0};
    int                        m_render_target_height{0};
    erhe::utility::Debug_label m_debug_label;

    friend class Device_impl;
};

} // namespace erhe::graphics
