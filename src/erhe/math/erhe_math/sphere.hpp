#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace erhe::math {

class Sphere
{
public:
    glm::vec3 center;
    float radius;

    auto contains(const glm::vec3& point) const -> bool;
    auto contains(const glm::vec3& point, float epsilon) const -> bool;
};

auto optimal_enclosing_sphere(const std::vector<glm::vec3>& points) -> Sphere;
// auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b) -> Sphere;
// auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) -> Sphere;
// auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) -> Sphere;
// auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& e, std::size_t& redundant_point) -> Sphere;

} // namespace erhe::math
