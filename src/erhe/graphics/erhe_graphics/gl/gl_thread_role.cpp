#include "erhe_graphics/gl/gl_thread_role.hpp"

namespace erhe::graphics {

namespace {

thread_local Gl_thread_role s_gl_thread_role   {Gl_thread_role::none};
thread_local int            s_gl_context_index{-1};

} // anonymous namespace

auto get_gl_thread_role() -> Gl_thread_role
{
    return s_gl_thread_role;
}

void set_gl_thread_role(const Gl_thread_role role)
{
    s_gl_thread_role = role;
}

auto get_gl_context_index() -> int
{
    return s_gl_context_index;
}

void set_gl_context_index(const int index)
{
    s_gl_context_index = index;
}

} // namespace erhe::graphics
