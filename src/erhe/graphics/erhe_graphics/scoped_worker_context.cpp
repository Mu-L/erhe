#include "erhe_graphics/scoped_worker_context.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/device.hpp"
# include "erhe_graphics/gl/gl_device.hpp"
# include "erhe_graphics/gl/gl_thread_role.hpp"
#endif

namespace erhe::graphics {

#if defined(ERHE_GRAPHICS_API_OPENGL)

namespace {

// Nested-scope refcount for one thread. Scopes are stack objects, so
// destruction order is LIFO: the outermost scope (the one holding the pool
// slot) is destroyed last, when the depth returns to zero.
thread_local int t_worker_context_depth = 0;

} // anonymous namespace

Scoped_worker_context::Scoped_worker_context(Device& device)
    : m_device{device}
{
    // No-op on the drawing thread: it already holds the main context and
    // may do everything a worker context would allow.
    if (get_gl_thread_role() == Gl_thread_role::main) {
        return;
    }
    m_counted = true;
    if (t_worker_context_depth++ > 0) {
        // Nested on a worker already holding a pool context: keep it.
        return;
    }
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

#else

Scoped_worker_context::Scoped_worker_context(Device& device)
    : m_device{device}
{
}

Scoped_worker_context::~Scoped_worker_context() noexcept = default;

#endif

} // namespace erhe::graphics
