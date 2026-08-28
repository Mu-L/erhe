#pragma once

namespace erhe::graphics {

class Device;

// Grants the calling worker thread a GL share context: create and operate on
// SHARED objects (buffers, textures, samplers) via DSA. Does NOT by itself
// grant access to any container object - take a Scoped_vertex_input_state or
// Scoped_framebuffer for that (scoped_container_access.hpp). No-op on the
// main thread and on every backend that needs no per-thread context (Vulkan,
// Metal, null).
//
// RE-ENTRANT: nested construction on one thread refcounts and keeps the same
// context current; only the outermost scope acquires and releases. Scopes
// are stack objects, so destruction is strictly LIFO per thread.
//
// Blocking: with all pool contexts in use, construction blocks until one is
// released. Callers must check Device::supports_worker_contexts() first on
// configurations where the pool may not exist (GL with no window) - see
// gl-worker-thread-contexts-plan.md section 8; constructing a scope on a
// worker thread with no pool is an error (asserts).
class Scoped_worker_context final
{
public:
    explicit Scoped_worker_context(Device& device);
    ~Scoped_worker_context() noexcept;
    Scoped_worker_context(const Scoped_worker_context&) = delete;
    void operator=       (const Scoped_worker_context&) = delete;
    Scoped_worker_context(Scoped_worker_context&&)      = delete;
    void operator=       (Scoped_worker_context&&)      = delete;

private:
    Device& m_device;
    // Pool context slot held by THIS scope: >= 1 only for the outermost
    // scope on a worker thread; -1 for the main-thread no-op, for nested
    // scopes, and on backends with no per-thread context.
    int     m_slot{-1};
    // True when this scope participated in the thread-local depth count
    // (worker-thread scopes only, outermost and nested alike).
    bool    m_counted{false};
};

} // namespace erhe::graphics
