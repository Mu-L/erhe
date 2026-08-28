#include "erhe_graphics/gl/gl_thread_role.hpp"

namespace erhe::graphics {

namespace {

thread_local Gl_thread_role s_gl_thread_role{Gl_thread_role::none};

} // anonymous namespace

auto get_gl_thread_role() -> Gl_thread_role
{
    return s_gl_thread_role;
}

void set_gl_thread_role(const Gl_thread_role role)
{
    s_gl_thread_role = role;
}

} // namespace erhe::graphics
