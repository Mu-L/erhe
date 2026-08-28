#pragma once

#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/gl/gl_thread_role.hpp"
#include "erhe_gl/wrapper_enums.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::graphics {

class Render_pipeline_state;
class Scoped_debug_group;

// GL framebuffer objects are container objects: they are NOT shared between
// contexts. One logical Render_pass therefore holds one framebuffer name
// pair per context (indexed by get_gl_context_index()), each created lazily
// on first use on that context - no ownership, no migration, no hand-back.
class Render_pass_impl final
{
public:
    Render_pass_impl (Device& device, const Render_pass_descriptor& render_pass_descriptor);
    ~Render_pass_impl() noexcept;
    Render_pass_impl (const Render_pass_impl&) = delete;
    void operator=   (const Render_pass_impl&) = delete;
    Render_pass_impl (Render_pass_impl&&)      = delete;
    void operator=   (Render_pass_impl&&)      = delete;

    // Current context's framebuffer names; 0 when not created on this
    // context (or when the pass targets the default framebuffer).
    // Relaxed loads of the caller's own slot - lock-free.
    [[nodiscard]] auto gl_name                    () const -> unsigned int;
    [[nodiscard]] auto gl_multisample_resolve_name() const -> unsigned int;
    [[nodiscard]] auto get_sample_count           () const -> unsigned int;

    // Creates and attaches this pass's framebuffer(s) on the calling
    // thread's current context if they do not exist yet, and returns the
    // main framebuffer name (0 for a swapchain pass). First use on a
    // context takes m_adoption_mutex; afterwards this is the same relaxed
    // own-slot load as gl_name().
    auto ensure_created_on_current_context() const -> unsigned int;

    auto check_status() const -> bool;

    [[nodiscard]] auto get_render_target_width () const -> int;
    [[nodiscard]] auto get_render_target_height() const -> int;
    [[nodiscard]] auto get_swapchain           () const -> Swapchain*;
    [[nodiscard]] auto get_debug_label         () const -> erhe::utility::Debug_label;

private:
    friend class Render_pass;
    // The command_buffer / before / after arguments are ignored by the
    // OpenGL backend; the driver handles all cross-pass synchronization
    // implicitly and there is no native command buffer to record into.
    void start_render_pass(Command_buffer& command_buffer, Render_pass* render_pass_before, Render_pass* render_pass_after);
    void end_render_pass  (Command_buffer& command_buffer, Render_pass* render_pass_after);

private:
    // Both names for one context; 0 = not created. Written only by the
    // owning context under m_adoption_mutex; reading one's own slot is
    // relaxed and lock-free. Relaxed ordering is sufficient: the names are
    // the only data published and no context uses another context's
    // framebuffer.
    class Context_slot
    {
    public:
        std::atomic<unsigned int> framebuffer                    {0};
        std::atomic<unsigned int> multisample_resolve_framebuffer{0};
    };

    Device&                                                 m_device;
    Swapchain*                                              m_swapchain{nullptr};
    mutable std::array<Context_slot, gl_context_slot_count> m_context_slots{};
    mutable std::mutex                                      m_adoption_mutex;
    std::array<Render_pass_attachment_descriptor, 4>        m_color_attachments;
    // Computed once in the constructor from the swapchain / attachment
    // descriptors, which never change after construction; per-context
    // creation must not write shared members.
    std::vector<gl::Color_buffer>                           m_draw_buffers;
    Render_pass_attachment_descriptor                       m_depth_attachment;
    Render_pass_attachment_descriptor                       m_stencil_attachment;
    int                                                     m_render_target_width{0};
    int                                                     m_render_target_height{0};
    erhe::utility::Debug_label                              m_debug_label;
    bool                                                    m_uses_multisample_resolve{false};
    bool                                                    m_is_active{false};

    erhe::utility::Debug_label                   m_debug_group_name;
    erhe::utility::Debug_label                   m_begin_debug_group_name;
    erhe::utility::Debug_label                   m_end_debug_group_name;
    std::unique_ptr<Scoped_debug_group>          m_outer_debug_group;

    friend class Device_impl;
};

} // namespace erhe::graphics
