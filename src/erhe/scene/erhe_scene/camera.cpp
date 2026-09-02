#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtc/constants.hpp>

namespace erhe::scene {

namespace {

using erhe::property::Dependency_object;
using erhe::property::Property;
using erhe::property::Property_bridge;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;
using Type = Projection::Type;

constexpr uint64_t         c_owner = erhe::Item_type::camera;
constexpr std::string_view c_group = "Projection";

// D18: the property reads and writes the Projection member directly, so
// Camera::projection() writers and property writers see one state.
template <typename T>
auto make_projection_bridge(T Projection::*member) -> Property_bridge
{
    return Property_bridge{
        .get = [member](const Dependency_object& object) -> Property_value {
            return erhe::property::make_value(static_cast<const Camera&>(object).projection()->*member);
        },
        .set = [member](Dependency_object& object, const Property_value& value) {
            if constexpr (std::is_enum_v<T>) {
                static_cast<Camera&>(object).projection()->*member = static_cast<T>(std::get<erhe::property::Enum_value>(value).value);
            } else {
                static_cast<Camera&>(object).projection()->*member = std::get<T>(value);
            }
        }
    };
}

auto projection_type_of(const Dependency_object& object) -> Type
{
    return static_cast<const Camera&>(object).projection()->projection_type;
}

auto is_perspective(const Dependency_object& object) -> bool
{
    const Type type = projection_type_of(object);
    return (type == Type::perspective) || (type == Type::perspective_xr) || (type == Type::perspective_horizontal) || (type == Type::perspective_vertical);
}
auto uses_fov_x    (const Dependency_object& object) -> bool { const Type t = projection_type_of(object); return (t == Type::perspective) || (t == Type::perspective_horizontal); }
auto uses_fov_y    (const Dependency_object& object) -> bool { const Type t = projection_type_of(object); return (t == Type::perspective) || (t == Type::perspective_vertical); }
auto uses_fov_sides(const Dependency_object& object) -> bool { return projection_type_of(object) == Type::perspective_xr; }
auto uses_width    (const Dependency_object& object) -> bool { const Type t = projection_type_of(object); return (t == Type::orthogonal) || (t == Type::orthogonal_horizontal) || (t == Type::orthogonal_rectangle); }
auto uses_height   (const Dependency_object& object) -> bool { const Type t = projection_type_of(object); return (t == Type::orthogonal) || (t == Type::orthogonal_vertical) || (t == Type::orthogonal_rectangle); }
auto uses_corner   (const Dependency_object& object) -> bool { return projection_type_of(object) == Type::orthogonal_rectangle; }
auto uses_frustum  (const Dependency_object& object) -> bool { return projection_type_of(object) == Type::generic_frustum; }

auto angle(float Projection::*member, const float min, const float max, const std::string_view label, const Property_ui::Visible_when visible_when) -> Property_metadata
{
    return Property_metadata{
        .default_value = Projection{}.*member,
        .ui            = Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::angle_degrees, .group = c_group, .label = label, .visible_when = visible_when},
        .bridge        = make_projection_bridge<float>(member)
    };
}

auto extent(float Projection::*member, const std::string_view label, const Property_ui::Visible_when visible_when, const std::string_view tooltip = {}) -> Property_metadata
{
    return Property_metadata{
        .default_value = Projection{}.*member,
        .ui            = Property_ui{.min = 0.0f, .max = 1000.0f, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .group = c_group, .tooltip = tooltip, .label = label, .visible_when = visible_when},
        .bridge        = make_projection_bridge<float>(member)
    };
}

auto log_slider(const float min, const float max, const std::string_view label, const std::string_view tooltip = {}) -> Property_ui
{
    return Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .tooltip = tooltip, .label = label};
}

constexpr float c_pi      = glm::pi<float>();
constexpr float c_half_pi = glm::half_pi<float>();

} // anonymous namespace

const Property<Type> Camera::projection_type_property = Property<Type>::register_property(
    "projection_type", c_owner, c_projection_type_enum_info,
    Property_metadata{
        .default_value = erhe::property::make_value(Projection{}.projection_type),
        .ui            = Property_ui{.group = c_group, .label = "Type"},
        .bridge        = make_projection_bridge<Type>(&Projection::projection_type)
    }
);
const Property<float> Camera::fov_x_property     = Property<float>::register_property("fov_x",     c_owner, angle(&Projection::fov_x,     0.0f,       c_pi,      "Fov X",     uses_fov_x));
const Property<float> Camera::fov_y_property     = Property<float>::register_property("fov_y",     c_owner, angle(&Projection::fov_y,     0.0f,       c_pi,      "Fov Y",     uses_fov_y));
const Property<float> Camera::fov_left_property  = Property<float>::register_property("fov_left",  c_owner, angle(&Projection::fov_left,  -c_half_pi, c_half_pi, "Fov Left",  uses_fov_sides));
const Property<float> Camera::fov_right_property = Property<float>::register_property("fov_right", c_owner, angle(&Projection::fov_right, -c_half_pi, c_half_pi, "Fov Right", uses_fov_sides));
const Property<float> Camera::fov_up_property    = Property<float>::register_property("fov_up",    c_owner, angle(&Projection::fov_up,    -c_half_pi, c_half_pi, "Fov Up",    uses_fov_sides));
const Property<float> Camera::fov_down_property  = Property<float>::register_property("fov_down",  c_owner, angle(&Projection::fov_down,  -c_half_pi, c_half_pi, "Fov Down",  uses_fov_sides));
const Property<float> Camera::ortho_left_property     = Property<float>::register_property("ortho_left",     c_owner, extent(&Projection::ortho_left,     "Left",   uses_corner));
const Property<float> Camera::ortho_width_property    = Property<float>::register_property("ortho_width",    c_owner, extent(&Projection::ortho_width,    "Width",  uses_width));
const Property<float> Camera::ortho_bottom_property   = Property<float>::register_property("ortho_bottom",   c_owner, extent(&Projection::ortho_bottom,   "Bottom", uses_corner));
const Property<float> Camera::ortho_height_property   = Property<float>::register_property("ortho_height",   c_owner, extent(&Projection::ortho_height,   "Height", uses_height));
const Property<float> Camera::frustum_left_property   = Property<float>::register_property("frustum_left",   c_owner, extent(&Projection::frustum_left,   "Frustum Left",   uses_frustum));
const Property<float> Camera::frustum_right_property  = Property<float>::register_property("frustum_right",  c_owner, extent(&Projection::frustum_right,  "Frustum Right",  uses_frustum));
const Property<float> Camera::frustum_bottom_property = Property<float>::register_property("frustum_bottom", c_owner, extent(&Projection::frustum_bottom, "Frustum Bottom", uses_frustum));
const Property<float> Camera::frustum_top_property    = Property<float>::register_property("frustum_top",    c_owner, extent(&Projection::frustum_top,    "Frustum Top",    uses_frustum));
const Property<float> Camera::z_near_property = Property<float>::register_property("z_near", c_owner, extent(&Projection::z_near, "Z Near", {}));
const Property<float> Camera::z_far_property  = Property<float>::register_property("z_far",  c_owner, extent(&Projection::z_far,  "Z Far",  {}, "Stays the depth hint for shadow fitting and the gizmo while Infinite Z Far is set"));
const Property<bool> Camera::infinite_z_far_property = Property<bool>::register_property(
    "infinite_z_far", c_owner,
    Property_metadata{
        .default_value = false,
        .ui            = Property_ui{.group = c_group, .tooltip = "Far plane at infinity (perspective projections only)", .label = "Infinite Z Far", .visible_when = is_perspective},
        .bridge        = make_projection_bridge<bool>(&Projection::infinite_z_far)
    }
);
const Property<float> Camera::exposure_property = Property<float>::register_property(
    "exposure", c_owner, Property_metadata{.default_value = 1.0f, .ui = log_slider(0.0f, 800000.0f, "Exposure")}
);
const Property<float> Camera::shadow_range_property = Property<float>::register_property(
    "shadow_range", c_owner, Property_metadata{.default_value = 22.0f, .ui = log_slider(1.0f, 1000.0f, "Shadow Range", "Radius of the bounding sphere the directional shadow fit covers around the camera")}
);

Camera::Camera()                         = default;
Camera::Camera(const Camera&)            = default;
Camera::~Camera() noexcept               = default;

Camera::Camera(const std::string_view name)
    : Item{name}
{
}

Camera::Camera(const Camera& src, erhe::for_clone)
    : Item        {src, erhe::for_clone{}} // exposure / shadow range entries copy with the base (D10)
    , m_projection{src.m_projection}
{
}

void Camera::handle_item_host_update(Item_host* const old_item_host, Item_host* const new_item_host)
{
    const auto shared_this = std::static_pointer_cast<Camera>(shared_from_this()); // keep alive

    Scene_host* old_scene_host = static_cast<Scene_host*>(old_item_host);
    Scene_host* new_scene_host = static_cast<Scene_host*>(new_item_host);

    if (old_scene_host != nullptr) {
        old_scene_host->unregister_camera(shared_this);
    }
    if (new_scene_host != nullptr) {
        new_scene_host->register_camera(shared_this);
    }
}

auto Camera::projection_transforms(
    const erhe::math::Viewport&               viewport,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
) const -> Camera_projection_transforms
{
    const auto clip_from_node = m_projection.clip_from_node_transform(viewport, reverse_depth, depth_range, conventions);
    const Node* node = get_node();
    ERHE_VERIFY(node != nullptr);
    return Camera_projection_transforms{
        .clip_from_camera = clip_from_node,
        .clip_from_world = Transform{
            clip_from_node.get_matrix() * node->node_from_world(),
            node->world_from_node()     * clip_from_node.get_inverse_matrix()
        }
    };
}

auto Camera::projection() -> Projection*
{
    return &m_projection;
}

auto Camera::projection() const -> const Projection*
{
    return &m_projection;
}

auto Camera::get_projection_scale() const -> float
{
    return m_projection.get_scale();
}

} // namespace erhe::scene
