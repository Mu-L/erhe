#pragma once

#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

// Explicit per-thread GL role, not an "is this the main thread" test: a
// future non-executor thread that makes a context current without going
// through the worker API gets `none` and trips the first guard rather than
// silently passing an is_main_thread() negation.
enum class Gl_thread_role : int {
    none = 0, // no GL context current on this thread
    main,     // the drawing thread's context is current
    worker    // a worker share context is current
};

[[nodiscard]] auto get_gl_thread_role() -> Gl_thread_role;
void set_gl_thread_role(Gl_thread_role role);

// Dense per-context identity for per-context container-object slots
// (VAOs, framebuffers): main context = 0, worker pool contexts
// 1..gl_worker_context_pool_size. Thread ids are the wrong key - the
// executor reuses threads across contexts - so the index is stored
// thread-locally beside the role. -1 = no context current.
//
// The slot count is the CONFIGURED pool size plus the main context, not the
// number of contexts actually created: context creation may fail entirely
// (headless / null window), and the main context's per-object slots are
// constructed before any pool context exists. Fixed by design - a dynamic
// pool size would invalidate every per-object slot array.
constexpr int gl_worker_context_pool_size = 4;
constexpr int gl_context_slot_count       = 1 + gl_worker_context_pool_size;

[[nodiscard]] auto get_gl_context_index() -> int;
void set_gl_context_index(int index);

} // namespace erhe::graphics

// A thread with any GL context current - main or worker - may pass.
#define ERHE_VERIFY_GL_THREAD_HAS_CONTEXT() \
    ERHE_VERIFY(::erhe::graphics::get_gl_thread_role() != ::erhe::graphics::Gl_thread_role::none)

// Only the drawing thread may pass: shared software caches
// (Gl_binding_state, OpenGL_state_tracker) and draw-related state.
#define ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE() \
    ERHE_VERIFY(::erhe::graphics::get_gl_thread_role() == ::erhe::graphics::Gl_thread_role::main)
