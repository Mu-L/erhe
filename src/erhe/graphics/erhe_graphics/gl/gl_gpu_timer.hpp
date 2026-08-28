#pragma once

#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_profile/profile.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace erhe::graphics {

class Command_buffer;
class Device;
class Render_pass;

// OpenGL GPU timer backend. Each timer owns a small ring buffer of
// GL_TIME_ELAPSED queries. write_begin_timestamp issues glBeginQuery into the
// current ring slot; write_end_timestamp issues glEndQuery. After the GPU
// completes the work the result is polled in Gpu_timer_impl::end_frame()
// (driven from Gl_device::end_frame) and stored as m_last_result.
//
// Constraint: GL allows only one GL_TIME_ELAPSED query active at a time, so
// only one Gpu_timer per Render_pass scope is supported.
//
// Main-thread-only by design: query objects are per-context (unshared), the
// per-timer state is a ring of queries no per-context slot can represent,
// and nothing wants GPU timing off the drawing thread. Creation and the
// begin/end timestamps assert the draw-capable role.
class Gpu_timer_impl
{
public:
    Gpu_timer_impl(Render_pass& render_pass, const char* label);
    ~Gpu_timer_impl() noexcept;

    Gpu_timer_impl(const Gpu_timer_impl&) = delete;
    auto operator=(const Gpu_timer_impl&) = delete;
    Gpu_timer_impl(Gpu_timer_impl&&)      = delete;
    auto operator=(Gpu_timer_impl&&)      = delete;

    [[nodiscard]] auto last_result() -> uint64_t;
    [[nodiscard]] auto label      () const -> const char*;

    void write_begin_timestamp(Command_buffer& command_buffer);
    void write_end_timestamp  (Command_buffer& command_buffer);

    // Walk every live Gpu_timer_impl and poll for available query results.
    // Called from Gl_device::end_frame; the registry exists for this walk.
    static void end_frame();

private:
    void create();
    void poll  ();

    class Query
    {
    public:
        std::optional<Gl_query> query_object{};
        bool                    pending     {false};
    };

    static constexpr std::size_t                      s_count = 4;
    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Gpu_timer_impl*>               s_all_gpu_timers;
    static std::size_t                                s_index;

    Render_pass*               m_render_pass{nullptr};
    Device*                    m_device     {nullptr};
    std::array<Query, s_count> m_queries;
    uint64_t                   m_last_result{0};
    const char*                m_label      {nullptr};
};

} // namespace erhe::graphics
