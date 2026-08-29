#include "erhe_graphics/gl/gl_context_index.hpp"

namespace erhe::graphics {

namespace {

thread_local int s_gl_context_index{-1};

} // anonymous namespace

auto get_gl_context_index() -> int
{
    return s_gl_context_index;
}

void set_gl_context_index(const int index)
{
    s_gl_context_index = index;
}

} // namespace erhe::graphics
