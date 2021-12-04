#pragma once

#include "scene/collision_generator.hpp"

#include "erhe/geometry/types.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/build_info.hpp"

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::physics
{
    class ICollision_shape;
    class IWorld;
}

namespace erhe::primitive
{
    class Material;
    class Primitive_geometry;
}

//namespace erhe::raytrace
//{
//    class IGeometry;
//    class IScene;
//}

namespace erhe::scene
{
    class Mesh;
    class Mesh_layer;
    class Node;
    class Scene;
}

namespace editor
{

class Node_physics;
class Node_raytrace;
class Scene_manager;

class Reference_frame
{
public:
    Reference_frame() = default;

    Reference_frame(
        const erhe::geometry::Geometry& geometry,
        erhe::geometry::Polygon_id      polygon_id
    );

    Reference_frame(const Reference_frame& other);
    Reference_frame(Reference_frame&& other);
    Reference_frame& operator=(const Reference_frame& other);
    Reference_frame& operator=(Reference_frame&& other);

    void transform_by(const glm::mat4 m);

    auto transform() const -> glm::mat4;

    auto scale() const -> float;

    uint32_t                   corner_count{0};
    erhe::geometry::Polygon_id polygon_id{0};
    glm::vec3                  centroid{0.0f, 0.0f, 0.0f}; // polygon centroid
    glm::vec3                  position{1.0f, 0.0f, 0.0f}; // one of polygon corner points
    glm::vec3                  B{0.0f, 0.0f, 1.0f};
    glm::vec3                  T{1.0f, 0.0f, 0.0f};
    glm::vec3                  N{0.0f, 1.0f, 0.0f};
};

class Brush_create_info final
{
public:
    Brush_create_info(
        const std::shared_ptr<erhe::geometry::Geometry>&        geometry,
        erhe::primitive::Build_info_set&                        build_info_set,
        const erhe::primitive::Normal_style                     normal_style,
        const float                                             density,
        const float                                             volume,
        const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape
    );

    Brush_create_info(
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        erhe::primitive::Build_info_set&                 build_info_set,
        const erhe::primitive::Normal_style              normal_style,
        const float                                      density,
        const Collision_volume_calculator                collision_volume_calculator,
        const Collision_shape_generator                  collision_shape_generator
    );

    ~Brush_create_info();

    std::shared_ptr<erhe::geometry::Geometry>        geometry;
    erhe::primitive::Build_info_set&                 build_info_set;
    erhe::primitive::Normal_style                    normal_style;
    float                                            density{1.0f};
    float                                            volume{1.0f};
    std::shared_ptr<erhe::physics::ICollision_shape> collision_shape;
    Collision_volume_calculator                      collision_volume_calculator;
    Collision_shape_generator                        collision_shape_generator;
};

class Instance
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh{};
    std::shared_ptr<Node_physics>      node_physics{};
    std::shared_ptr<Node_raytrace>     node_raytrace{};
};

class Brush final
{
public:
    using Create_info = Brush_create_info;

    explicit Brush(erhe::primitive::Build_info_set& build_info_set);
    explicit Brush(const Create_info& create_info);
    ~Brush        ();
    Brush         (const Brush&) = delete;
    void operator=(const Brush&) = delete;
    Brush         (Brush&& other) noexcept;
    void operator=(Brush&&)      = delete;

    void initialize(const Create_info& create_info);

    auto get_reference_frame(const uint32_t corner_count) -> Reference_frame;

    static constexpr float c_scale_factor = 65536.0f;

    class Scaled
    {
    public:
        int                                                  scale_key;
        std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry;
        std::shared_ptr<erhe::physics::ICollision_shape>     collision_shape;
        float                                                volume;
        glm::vec3                                            local_inertia;
    };

    auto get_scaled   (const float scale) -> const Scaled&;
    auto create_scaled(const int scale_key) -> Scaled;

    auto make_instance(
        const glm::mat4                                   world_from_node,
        const std::shared_ptr<erhe::primitive::Material>& material,
        const float                                       scale
    ) -> Instance;

    auto name() const -> const std::string&;

    std::shared_ptr<erhe::geometry::Geometry>            geometry;
    erhe::primitive::Build_info_set                      build_info_set;
    std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry;
    erhe::primitive::Normal_style                        normal_style{erhe::primitive::Normal_style::corner_normals};
    std::shared_ptr<erhe::physics::ICollision_shape>     collision_shape;
    Collision_volume_calculator                          collision_volume_calculator;
    Collision_shape_generator                            collision_shape_generator;
    float                                                volume {0.0f};
    float                                                density{1.0f};
    std::vector<Reference_frame>                         reference_frames;
    std::vector<Scaled>                                  scaled_entries;
};

}