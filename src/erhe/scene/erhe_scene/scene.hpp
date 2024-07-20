#pragma once

#include "erhe_scene/scene_message_bus.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/unique_id.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::scene {

class Camera;
class Light;
class Mesh;
class Node;
class Scene;
class Scene_host;
class Skin;

using Layer_id = uint64_t;

class Mesh_layer
{
public:
    Mesh_layer(const std::string_view name, uint64_t flags, Layer_id id);

    // Public API
    [[nodiscard]] auto get_mesh_by_id(const erhe::Unique_id<Node>::id_type mesh_id) const -> std::shared_ptr<Mesh>;
    [[nodiscard]] auto get_name() const -> const std::string&;

    void add   (const std::shared_ptr<Mesh>& mesh);
    void remove(const std::shared_ptr<Mesh>& mesh);

    std::vector<std::shared_ptr<Mesh>> meshes;
    std::string                        name;
    uint64_t                           flags{0};
    Layer_id                           id;
};

class Light_layer
{
public:
    Light_layer(const std::string_view name, Layer_id id);

    [[nodiscard]] auto get_light_by_id(std::size_t id) const -> std::shared_ptr<Light>;
    [[nodiscard]] auto get_name       ()               const -> const std::string&;

    void add   (const std::shared_ptr<Light>& light);
    void remove(const std::shared_ptr<Light>& light);

    std::vector<std::shared_ptr<Light>> lights;
    glm::vec4                           ambient_light{0.0f, 0.0f, 0.0f, 0.0f};
    std::string                         name;
    Layer_id                            id;
};

class Scene : public erhe::Item<erhe::Item_base, erhe::Item_base, Scene>
{
public:
    Scene(erhe::scene::Scene_message_bus& scene_message_bus, const std::string_view name, Scene_host* host = nullptr);
    explicit Scene(const Scene& src);
    Scene& operator=(const Scene& src);
    ~Scene() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Scene"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Overrrides Item_base
    auto get_item_host() const -> erhe::Item_host* override;

    // Public API
    void sanity_check          () const;
    void sort_transform_nodes  ();
    void update_node_transforms();

    [[nodiscard]] auto get_mesh_by_id       (erhe::Unique_id<Node>::id_type id) const -> std::shared_ptr<Mesh>;
    [[nodiscard]] auto get_light_by_id      (erhe::Unique_id<Node>::id_type id) const -> std::shared_ptr<Light>;
    [[nodiscard]] auto get_camera_by_id     (erhe::Unique_id<Node>::id_type id) const -> std::shared_ptr<Camera>;
    [[nodiscard]] auto get_mesh_layer_by_id (Layer_id id) const -> std::shared_ptr<Mesh_layer>;
    [[nodiscard]] auto get_light_layer_by_id(Layer_id id) const -> std::shared_ptr<Light_layer>;
    [[nodiscard]] auto get_root_node        () const -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto get_cameras          () -> std::vector<std::shared_ptr<Camera>>&;
    [[nodiscard]] auto get_cameras          () const -> const std::vector<std::shared_ptr<Camera>>&;
    [[nodiscard]] auto get_skins            () -> std::vector<std::shared_ptr<Skin>>&;
    [[nodiscard]] auto get_skins            () const -> const std::vector<std::shared_ptr<Skin>>&;
    [[nodiscard]] auto get_flat_nodes       () -> std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto get_flat_nodes       () const -> const std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto get_mesh_layers      () -> std::vector<std::shared_ptr<Mesh_layer>>&;
    [[nodiscard]] auto get_mesh_layers      () const -> const std::vector<std::shared_ptr<Mesh_layer>>&;
    [[nodiscard]] auto get_light_layers     () -> std::vector<std::shared_ptr<Light_layer>>&;
    [[nodiscard]] auto get_light_layers     () const -> const std::vector<std::shared_ptr<Light_layer>>&;

    void add_mesh_layer (const std::shared_ptr<Mesh_layer>& mesh_layer);
    void add_light_layer(const std::shared_ptr<Light_layer>& light_layer);

    void register_node    (const std::shared_ptr<Node>& node);
    void unregister_node  (const std::shared_ptr<Node>& node);
    void register_camera  (const std::shared_ptr<Camera>& camera);
    void unregister_camera(const std::shared_ptr<Camera>& camera);
    void register_mesh    (const std::shared_ptr<Mesh>& mesh);
    void unregister_mesh  (const std::shared_ptr<Mesh>& mesh);
    void register_skin    (const std::shared_ptr<Skin>& skin);
    void unregister_skin  (const std::shared_ptr<Skin>& skin);
    void register_light   (const std::shared_ptr<Light>& light);
    void unregister_light (const std::shared_ptr<Light>& light);

private:
    Scene_message_bus&                        m_message_bus;
    Scene_host*                               m_host       {nullptr};
    std::shared_ptr<erhe::scene::Node>        m_root_node;
    std::vector<std::shared_ptr<Node>>        m_flat_node_vector;
    std::vector<std::shared_ptr<Mesh_layer>>  m_mesh_layers;
    std::vector<std::shared_ptr<Skin>>        m_skins;
    std::vector<std::shared_ptr<Light_layer>> m_light_layers;
    std::vector<std::shared_ptr<Camera>>      m_cameras;
    bool                                      m_nodes_sorted{false};
};

} // namespace erhe::scene
