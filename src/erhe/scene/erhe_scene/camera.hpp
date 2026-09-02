#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/transform.hpp"
#include "erhe_property/dependency_property.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace erhe::scene {

class Camera;

class Camera_projection_transforms
{
public:
    Transform clip_from_camera;
    Transform clip_from_world;
};

class Camera : public erhe::Item<Item_base, Node_attachment, Camera, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Camera();
    explicit Camera(const Camera&);
    Camera& operator=(const Camera&) = delete;
    ~Camera() noexcept override;

    explicit Camera(std::string_view name);
    Camera(const Camera&, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Camera"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::camera; }

    // Implements Node_attachment
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Public API
    [[nodiscard]] auto projection           () -> Projection*;
    [[nodiscard]] auto projection           () const -> const Projection*;
    [[nodiscard]] auto projection_transforms(
        const erhe::math::Viewport&               viewport,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) const -> Camera_projection_transforms;
    [[nodiscard]] auto get_exposure         () const -> float { return get_value(exposure_property); }
    [[nodiscard]] auto get_shadow_range     () const -> float { return get_value(shadow_range_property); }
    [[nodiscard]] auto get_projection_scale () const -> float;
    void set_exposure    (float value) { set_value(exposure_property, value); }
    void set_shadow_range(float value) { set_value(shadow_range_property, value); }

    // Registered properties (erhe::property, doc/property-system-plan.md
    // section 4.4). The projection properties are bridged (D18) over
    // m_projection, so projection() writes and property writes reach the
    // same state; exposure and shadow range live in the property store.
    static const erhe::property::Property<Projection::Type> projection_type_property;
    static const erhe::property::Property<float>            fov_x_property;
    static const erhe::property::Property<float>            fov_y_property;
    static const erhe::property::Property<float>            fov_left_property;
    static const erhe::property::Property<float>            fov_right_property;
    static const erhe::property::Property<float>            fov_up_property;
    static const erhe::property::Property<float>            fov_down_property;
    static const erhe::property::Property<float>            ortho_left_property;
    static const erhe::property::Property<float>            ortho_width_property;
    static const erhe::property::Property<float>            ortho_bottom_property;
    static const erhe::property::Property<float>            ortho_height_property;
    static const erhe::property::Property<float>            frustum_left_property;
    static const erhe::property::Property<float>            frustum_right_property;
    static const erhe::property::Property<float>            frustum_bottom_property;
    static const erhe::property::Property<float>            frustum_top_property;
    static const erhe::property::Property<float>            z_near_property;
    static const erhe::property::Property<float>            z_far_property;
    static const erhe::property::Property<bool>             infinite_z_far_property;
    static const erhe::property::Property<float>            exposure_property;
    static const erhe::property::Property<float>            shadow_range_property;

private:
    Projection m_projection;
};

} // namespace erhe::scene
