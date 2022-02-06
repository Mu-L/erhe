#pragma once

#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::scene
{

class Node;
class Mesh;
class Camera;
class Light;

class Mesh_layer
{
public:
    Mesh_layer(
        const std::string_view name,
        const uint64_t         flags
    );

    [[nodiscard]] auto get_mesh_by_id(
        const erhe::toolkit::Unique_id<Node>::id_type id
    ) const -> std::shared_ptr<Mesh>;

    std::vector<std::shared_ptr<Mesh>> meshes;
    std::string                        name;
    uint64_t                           flags{0};
};

class Light_layer
{
public:
    explicit Light_layer(const std::string_view name);

    [[nodiscard]] auto get_light_by_id(
        const erhe::toolkit::Unique_id<Node>::id_type id
    ) const -> std::shared_ptr<Light>;

    std::vector<std::shared_ptr<Light>> lights;
    glm::vec4                           ambient_light{0.0f, 0.0f, 0.0f, 0.0f};
    float                               exposure{1.0f};
    std::string                         name;
};

class Scene
{
public:
    void sanity_check          () const;
    void sort_transform_nodes  ();
    void update_node_transforms();

    [[nodiscard]] auto get_node_by_id         (const erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Node>;
    [[nodiscard]] auto get_mesh_by_id         (const erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Mesh>;
    [[nodiscard]] auto get_light_by_id        (const erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Light>;
    [[nodiscard]] auto get_camera_by_id       (const erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Camera>;
    [[nodiscard]] auto transform_update_serial() -> uint64_t;

    std::vector<std::shared_ptr<Node>>        nodes;
    std::vector<std::shared_ptr<Mesh_layer>>  mesh_layers;
    std::vector<std::shared_ptr<Light_layer>> light_layers;
    std::vector<std::shared_ptr<Camera>>      cameras;

    bool nodes_sorted{false};

private:
    uint64_t m_transform_update_serial{0};
};

} // namespace erhe::scene
