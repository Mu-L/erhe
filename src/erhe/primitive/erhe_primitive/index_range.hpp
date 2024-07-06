#pragma once

#include "erhe_gl/wrapper_enums.hpp"

#include <cstddef>

namespace erhe::primitive {

/// Holds index range for specified GL primitive type
class Index_range
{
public:
    gl::Primitive_type primitive_type{gl::Primitive_type::triangles};
    std::size_t        first_index   {0};
    std::size_t        index_count   {0};
};

} // namespace erhe::primitive
