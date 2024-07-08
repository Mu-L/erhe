#pragma once

#include "erhe_raytrace/iinstance.hpp"

#include <glm/glm.hpp>

#include <string>

namespace erhe::raytrace {

class Bvh_scene;

class Bvh_instance : public IInstance
{
public:
    explicit Bvh_instance(const std::string_view debug_label);
    ~Bvh_instance() noexcept override;

    // Implements IInstance
    void commit       ()                           override;
    void enable       ()                           override;
    void disable      ()                           override;
    void set_transform(const glm::mat4 transform)  override;
    void set_scene    (IScene* scene)              override;
    void set_mask     (uint32_t mask)              override;
    void set_user_data(void* ptr)                  override;
    auto get_transform() const -> glm::mat4        override;
    auto get_scene    () const -> IScene*          override;
    auto get_mask     () const -> uint32_t         override;
    auto get_user_data() const -> void*            override;
    auto is_enabled   () const -> bool             override;
    auto debug_label  () const -> std::string_view override;

    // Bvh_instance public API
    auto intersect(Ray& ray, Hit& hit) -> bool;

private:
    glm::mat4   m_transform{1.0f};
    bool        m_enabled  {true};
    IScene*     m_scene    {nullptr};
    uint32_t    m_mask     {0xffffffffu};
    void*       m_user_data{nullptr};
    std::string m_debug_label;
};

} // namespace erhe::raytrace
