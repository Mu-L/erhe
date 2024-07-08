#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry::operation {

class Reverse : public Geometry_operation
{
public:
    Reverse(Geometry& source, Geometry& destination);
};

[[nodiscard]] auto reverse(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
