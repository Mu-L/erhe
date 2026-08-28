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

} // namespace erhe::graphics

// A thread with any GL context current - main or worker - may pass.
#define ERHE_VERIFY_GL_THREAD_HAS_CONTEXT() \
    ERHE_VERIFY(::erhe::graphics::get_gl_thread_role() != ::erhe::graphics::Gl_thread_role::none)

// Only the drawing thread may pass: shared software caches
// (Gl_binding_state, OpenGL_state_tracker) and draw-related state.
#define ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE() \
    ERHE_VERIFY(::erhe::graphics::get_gl_thread_role() == ::erhe::graphics::Gl_thread_role::main)
