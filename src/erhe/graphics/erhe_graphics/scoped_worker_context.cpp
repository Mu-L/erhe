#include "erhe_graphics/scoped_worker_context.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/device.hpp"
# include "erhe_graphics/gl/gl_device.hpp"
# include "erhe_graphics/gl/gl_context_index.hpp"
#endif

namespace erhe::graphics {

#if defined(ERHE_GRAPHICS_API_OPENGL)

namespace {

// Nested-scope refcount for one thread. Scopes are stack objects, so
// destruction order is LIFO: the outermost scope (the one holding the pool
// slot) is destroyed last, when the depth returns to zero.
thread_local int t_worker_context_depth = 0;

// Construction site of the outermost live scope on this thread; valid only
// while that scope holds a pool slot.
thread_local std::source_location t_acquire_site{};

} // anonymous namespace

Scoped_worker_context::Scoped_worker_context(Device& device, const std::source_location location)
    : m_device{device}
{
    // No-op on the drawing thread: it already holds the main context and
    // may do everything a worker context would allow.
    if (gl_thread_is_main_context()) {
        return;
    }
    m_counted = true;
    if (t_worker_context_depth++ > 0) {
        // Nested on a worker already holding a pool context: keep it.
        return;
    }
    t_acquire_site = location;
    m_slot = device.get_impl().acquire_worker_context_slot();
}

Scoped_worker_context::~Scoped_worker_context() noexcept
{
    if (!m_counted) {
        return;
    }
    --t_worker_context_depth;
    if (m_slot >= 0) {
        m_device.get_impl().release_worker_context_slot(m_slot);
    }
}

auto thread_holds_worker_context() -> bool
{
    return gl_thread_is_worker_context();
}

auto thread_worker_context_slot() -> int
{
    const int index = get_gl_context_index();
    return (index > 0) ? index : -1;
}

auto thread_worker_context_acquire_site() -> const std::source_location*
{
    return gl_thread_is_worker_context() ? &t_acquire_site : nullptr;
}

#else

Scoped_worker_context::Scoped_worker_context(Device& device, std::source_location)
    : m_device{device}
{
}

Scoped_worker_context::~Scoped_worker_context() noexcept = default;

auto thread_holds_worker_context() -> bool
{
    return false;
}

auto thread_worker_context_slot() -> int
{
    return -1;
}

auto thread_worker_context_acquire_site() -> const std::source_location*
{
    return nullptr;
}

#endif

} // namespace erhe::graphics
