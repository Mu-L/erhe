#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

using erhe::physics::IRigid_body_create_info;
using erhe::physics::IRigid_body;
using erhe::physics::Motion_mode;
using erhe::scene::Node_attachment;
using erhe::property::Dependency_object;
using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_bridge;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

namespace {

constexpr std::string_view c_group = "Rigid Body";

auto is_movable(const Dependency_object& object) -> bool
{
    return static_cast<const Node_physics&>(object).get_motion_mode() != Motion_mode::e_static;
}

auto slider(const float min, const float max, const std::string_view label, const std::string_view tooltip = {}, const Property_ui::Visible_when visible_when = {}) -> Property_ui
{
    return Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::slider, .group = c_group, .tooltip = tooltip, .label = label, .visible_when = visible_when};
}

auto unit_range(const Property_value& value) -> bool
{
    const float f = std::get<float>(value);
    return (f >= 0.0f) && (f <= 1.0f);
}

} // anonymous namespace

const Property<Motion_mode> Node_physics::motion_mode_property = Property<Motion_mode>::register_member(
    "motion_mode", Node_physics::property_owner_type(), erhe::physics::c_motion_mode_enum_info, &Node_physics::m_motion_mode,
    Property_metadata{
        .default_value = erhe::property::make_value(Motion_mode::e_dynamic),
        .ui            = Property_ui{.group = c_group, .tooltip = "The intended mode; a static trigger body is created kinematic non-physical", .label = "Motion Mode"}
    },
    [](Node_physics& node_physics) { node_physics.apply_motion_mode(); }
);
const Property<bool> Node_physics::is_trigger_property = Property<bool>::register_member<Node_physics, bool>(
    "is_trigger", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.is_sensor; },
    Property_metadata{.default_value = false, .ui = Property_ui{.group = c_group, .tooltip = "Sensor body: reports overlaps, no collision response (recreates the rigid body)", .label = "Is Trigger"}},
    [](Node_physics& node_physics) { node_physics.recreate_rigid_body(); }
);
const Property<float> Node_physics::mass_property = Property<float>::register_property(
    "mass", Node_physics::property_owner_type(),
    Property_metadata{
        .default_value = 1.0f,
        .ui            = Property_ui{.min = 0.01f, .max = 1000.0f, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .group = c_group, .tooltip = "kg; until set, the density-derived mass of the live body", .label = "Mass"},
        .bridge        = Property_bridge{
            .get = [](const Dependency_object& object) -> Property_value { return static_cast<const Node_physics&>(object).get_mass(); },
            .set = [](Dependency_object& object, const Property_value& value) { static_cast<Node_physics&>(object).apply_mass(std::get<float>(value)); }
        }
    },
    [](const Property_value& value) -> bool { return std::get<float>(value) > 0.0f; }
);
const Property<float> Node_physics::friction_property = Property<float>::register_member<Node_physics, float>(
    "friction", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.friction; },
    Property_metadata{.default_value = 0.5f, .ui = slider(0.0f, 1.0f, "Friction", "Overridden by the physics material in contact resolution while one is set")},
    [](Node_physics& node_physics) { node_physics.apply_friction(); }, unit_range
);
const Property<float> Node_physics::restitution_property = Property<float>::register_member<Node_physics, float>(
    "restitution", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.restitution; },
    Property_metadata{.default_value = 0.2f, .ui = slider(0.0f, 1.0f, "Restitution", "Overridden by the physics material in contact resolution while one is set")},
    [](Node_physics& node_physics) { node_physics.apply_restitution(); }, unit_range
);
const Property<float> Node_physics::linear_damping_property = Property<float>::register_member<Node_physics, float>(
    "linear_damping", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.linear_damping; },
    Property_metadata{.default_value = 0.05f, .ui = slider(0.0f, 1.0f, "Linear Damping", {}, is_movable)},
    [](Node_physics& node_physics) { node_physics.apply_damping(); }, unit_range
);
const Property<float> Node_physics::angular_damping_property = Property<float>::register_member<Node_physics, float>(
    "angular_damping", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.angular_damping; },
    Property_metadata{.default_value = 0.05f, .ui = slider(0.0f, 1.0f, "Angular Damping", {}, is_movable)},
    [](Node_physics& node_physics) { node_physics.apply_damping(); }, unit_range
);
const Property<float> Node_physics::gravity_factor_property = Property<float>::register_member<Node_physics, float>(
    "gravity_factor", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.gravity_factor; },
    Property_metadata{.default_value = 1.0f, .ui = slider(0.0f, 2.0f, "Gravity Factor", {}, is_movable)},
    [](Node_physics& node_physics) { node_physics.apply_gravity_factor(); }
);
const Property<float> Node_physics::wind_receptivity_property = Property<float>::register_member(
    "wind_receptivity", Node_physics::property_owner_type(), &Node_physics::m_wind_receptivity,
    Property_metadata{.default_value = 0.0f, .ui = slider(0.0f, 10.0f, "Wind Receptivity", "kg/s: force = wind_receptivity * (wind velocity - body velocity) at the center of mass each fixed step; 0 = unaffected by wind")}
);
const Property<glm::vec3> Node_physics::initial_linear_velocity_property = Property<glm::vec3>::register_member<Node_physics, glm::vec3>(
    "initial_linear_velocity", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.linear_velocity; },
    Property_metadata{.ui = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "World space; applied when the rigid body is (re)created", .label = "Initial Linear Velocity"}}
);
const Property<glm::vec3> Node_physics::initial_angular_velocity_property = Property<glm::vec3>::register_member<Node_physics, glm::vec3>(
    "initial_angular_velocity", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.angular_velocity; },
    Property_metadata{.ui = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "World space; applied when the rigid body is (re)created", .label = "Initial Angular Velocity"}}
);
const Property<glm::vec3> Node_physics::center_of_mass_offset_property = Property<glm::vec3>::register_property(
    "center_of_mass_offset", Node_physics::property_owner_type(),
    Property_metadata{
        .ui     = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "Offset-center-of-mass wrapper around the collision shape (recreates the rigid body)", .label = "Center of Mass"},
        .bridge = Property_bridge{
            .get = [](const Dependency_object& object) -> Property_value { return static_cast<const Node_physics&>(object).get_center_of_mass_offset(); },
            .set = [](Dependency_object& object, const Property_value& value) { static_cast<Node_physics&>(object).apply_center_of_mass_offset(std::get<glm::vec3>(value)); }
        }
    }
);
const Property<Object_reference> Node_physics::physics_material_property = Property<Object_reference>::register_member<Node_physics, std::shared_ptr<erhe::physics::Physics_material>>(
    "physics_material", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.physics_material; },
    Property_metadata{.ui = Property_ui{.group = c_group, .tooltip = "Shared material; its friction and restitution override the scalar ones in contact resolution", .label = "Physics Material", .reference_item_types = erhe::Item_type::physics_material}},
    [](Node_physics& node_physics) { node_physics.reapply_physics_material(); }
);
const Property<Object_reference> Node_physics::collision_filter_property = Property<Object_reference>::register_member<Node_physics, std::shared_ptr<erhe::physics::Collision_filter>>(
    "collision_filter", Node_physics::property_owner_type(), [](auto& node_physics) -> auto& { return node_physics.m_create_info.collision_filter; },
    Property_metadata{.ui = Property_ui{.group = c_group, .label = "Collision Filter", .reference_item_types = erhe::Item_type::collision_filter}},
    [](Node_physics& node_physics) { node_physics.reapply_collision_filter(); }
);

Node_physics::Node_physics(const Node_physics&) = default;
Node_physics& Node_physics::operator=(const Node_physics&) = default;

Node_physics::Node_physics(const Node_physics& src, erhe::for_clone)
    : Item          {src, erhe::for_clone{}}
    , markers       {}        // clone does not initially have markers
    , m_physics_world{nullptr} // clone is initially detached
    , m_create_info {src.m_create_info}
    , m_rigid_body  {}        // clone rigid body is not initially created
    , m_motion_mode {src.m_motion_mode}
    , m_wind_receptivity{src.m_wind_receptivity}
{
}

Node_physics::Node_physics(const IRigid_body_create_info& create_info)
    : m_create_info{create_info}
    , m_motion_mode{create_info.motion_mode}
{
}

Node_physics::~Node_physics() noexcept
{
    set_node(nullptr);
}

void Node_physics::set_physics_world(erhe::physics::IWorld* value)
{
    if (value != nullptr) {
        ERHE_VERIFY(m_physics_world == nullptr);
    }
    m_physics_world = value;
}

auto Node_physics::get_physics_world() const -> erhe::physics::IWorld*
{
    return m_physics_world;
}

void Node_physics::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    ERHE_VERIFY(old_item_host != new_item_host);

    // NOTE: This also keeps this alive is old host is only shared_ptr to it
    const auto shared_this = std::static_pointer_cast<Node_physics>(shared_from_this());

    if (old_item_host != nullptr) {
        Scene_root* old_scene_root = static_cast<Scene_root*>(old_item_host);
        old_scene_root->unregister_node_physics(shared_this);
        m_rigid_body.reset();
    }
    if (new_item_host != nullptr) {
        Scene_root* new_scene_root = static_cast<Scene_root*>(new_item_host);
        auto& physics_world = new_scene_root->get_physics_world();

        log_physics->trace("making rigid body for {}", get_node()->get_name());
        create_rigid_body(physics_world);
        new_scene_root->register_node_physics(shared_this);
        if (m_wake_on_attach && m_rigid_body && (m_rigid_body->get_motion_mode() == Motion_mode::e_dynamic)) {
            m_rigid_body->begin_move();
            m_rigid_body->end_move();
        }
    }
}

auto Node_physics::get_effective_motion_mode() const -> Motion_mode
{
    // Jolt sensors must be non-static to detect overlaps with static bodies.
    return (m_create_info.is_sensor && (m_motion_mode == Motion_mode::e_static))
        ? Motion_mode::e_kinematic_non_physical
        : m_motion_mode;
}

void Node_physics::create_rigid_body(erhe::physics::IWorld& physics_world)
{
    m_create_info.motion_mode = get_effective_motion_mode();
    const erhe::scene::Node* node = get_node();
    if (node != nullptr) {
        const erhe::scene::Trs_transform& world_from_node_transform = node->world_from_node_transform();
        m_create_info.position    = world_from_node_transform.get_translation();
        m_create_info.orientation = world_from_node_transform.get_rotation();
    }
    m_rigid_body = physics_world.create_rigid_body_shared(m_create_info);
    m_rigid_body->set_owner(this);
}

void Node_physics::recreate_rigid_body()
{
    if (m_physics_world == nullptr) {
        // Not attached to a scene; the updated create info is picked up when
        // the rigid body is created at attach time.
        return;
    }
    Scene_root* scene_root = static_cast<Scene_root*>(get_item_host());
    ERHE_VERIFY(scene_root != nullptr);
    const auto shared_this = std::static_pointer_cast<Node_physics>(shared_from_this());
    // unregister_node_physics() tears down joint constraints referencing the
    // old body and removes it from the world; register_node_physics() adds
    // the new body and retries pending joint constraints.
    scene_root->unregister_node_physics(shared_this);
    m_rigid_body.reset();
    create_rigid_body(scene_root->get_physics_world());
    scene_root->register_node_physics(shared_this);
}

void Node_physics::before_physics_simulation()
{
    const erhe::scene::Trs_transform& world_from_node_transform = get_node()->world_from_node_transform();
    erhe::physics::Transform transform{
        glm::mat3_cast(world_from_node_transform.get_rotation()),
        world_from_node_transform.get_translation()
    };
    m_rigid_body->set_world_transform(transform);

    const auto get_transform = m_rigid_body->get_world_transform();
    const glm::vec3 get_world_position = glm::vec3{get_transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
}

// This is intended to be called from physics backend
void Node_physics::after_physics_simulation()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_node != nullptr);
    ERHE_VERIFY(m_rigid_body);

    const auto motion_mode = m_rigid_body->get_motion_mode();
    if (motion_mode != erhe::physics::Motion_mode::e_dynamic) {
        return;
    }

    const auto transform = m_rigid_body->get_world_transform();
    const glm::vec3 world_position = glm::vec3{transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};

    if (world_position.y < -100.0f) {
        const glm::vec3 respawn_location{0.0f, 8.0f, 0.0f};
        m_rigid_body->set_world_transform (erhe::physics::Transform{glm::mat3{1.0f}, respawn_location});
        m_rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});
        m_rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
        const glm::mat4 matrix = erhe::math::create_translation<float>(respawn_location);
        get_node()->set_world_from_node(matrix);
    } else {
        get_node()->set_world_from_node(transform);
    }
}

auto Node_physics::get_rigid_body() -> IRigid_body*
{
    return (m_physics_world != nullptr) ? m_rigid_body.get() : nullptr;
}

auto Node_physics::get_rigid_body() const -> const IRigid_body*
{
    return (m_physics_world != nullptr) ? m_rigid_body.get() : nullptr;
}

auto Node_physics::get_collision_shape() const -> const std::shared_ptr<erhe::physics::ICollision_shape>&
{
    return m_create_info.collision_shape;
}

void Node_physics::set_collision_shape(const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape)
{
    if (m_create_info.collision_shape == collision_shape) {
        return;
    }
    m_create_info.collision_shape = collision_shape;
    recreate_rigid_body();
}

auto Node_physics::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

void Node_physics::set_motion_mode(Motion_mode mode)
{
    set_value(motion_mode_property, mode);
}

void Node_physics::apply_motion_mode()
{
    m_create_info.motion_mode = get_effective_motion_mode();
    if (m_rigid_body) {
        m_rigid_body->set_motion_mode(m_create_info.motion_mode);
    }
}

auto Node_physics::get_mass() const -> float
{
    if (m_create_info.mass.has_value()) {
        return m_create_info.mass.value();
    }
    const IRigid_body* rigid_body = get_rigid_body();
    return (rigid_body != nullptr) ? rigid_body->get_mass() : 0.0f;
}

void Node_physics::set_mass(const float mass)
{
    set_value(mass_property, mass);
}

void Node_physics::apply_mass(const float mass)
{
    if (mass == get_mass()) {
        return; // R4: the bridge set is reached for every write
    }
    m_create_info.mass = mass;
    IRigid_body* rigid_body = get_rigid_body();
    if (rigid_body != nullptr) {
        // Inertia scales linearly with mass for a fixed shape (the same
        // ratio math as the create-path mass override in brush.cpp);
        // keeping the old inertia would leave the body tumbling as if it
        // still had the old mass.
        const float old_mass = rigid_body->get_mass();
        glm::mat4 inertia = rigid_body->get_local_inertia();
        if (old_mass > 0.0f) {
            inertia = glm::mat4{glm::mat3{inertia} * (mass / old_mass)};
        }
        rigid_body->set_mass_properties(mass, inertia);
    }
}

auto Node_physics::get_friction() const -> float
{
    return m_create_info.friction;
}

void Node_physics::set_friction(const float friction)
{
    set_value(friction_property, friction);
}

void Node_physics::apply_friction()
{
    if (m_rigid_body) {
        m_rigid_body->set_friction(m_create_info.friction);
    }
}

auto Node_physics::get_restitution() const -> float
{
    return m_create_info.restitution;
}

void Node_physics::set_restitution(const float restitution)
{
    set_value(restitution_property, restitution);
}

void Node_physics::apply_restitution()
{
    if (m_rigid_body) {
        m_rigid_body->set_restitution(m_create_info.restitution);
    }
}

auto Node_physics::get_linear_damping() const -> float
{
    return m_create_info.linear_damping;
}

void Node_physics::set_linear_damping(const float linear_damping)
{
    set_value(linear_damping_property, linear_damping);
}

auto Node_physics::get_angular_damping() const -> float
{
    return m_create_info.angular_damping;
}

void Node_physics::set_angular_damping(const float angular_damping)
{
    set_value(angular_damping_property, angular_damping);
}

void Node_physics::apply_damping()
{
    // A static body has no motion properties (Jolt); the create info keeps
    // the values for the next non-static mode.
    if (m_rigid_body && (m_rigid_body->get_motion_mode() != Motion_mode::e_static)) {
        m_rigid_body->set_damping(m_create_info.linear_damping, m_create_info.angular_damping);
    }
}

auto Node_physics::get_physics_material() const -> const std::shared_ptr<erhe::physics::Physics_material>&
{
    return m_create_info.physics_material;
}

void Node_physics::set_physics_material(const std::shared_ptr<erhe::physics::Physics_material>& physics_material)
{
    set_value(physics_material_property, Object_reference{physics_material});
}

void Node_physics::reapply_physics_material()
{
    if (m_rigid_body) {
        m_rigid_body->set_physics_material(m_create_info.physics_material);
    }
}

auto Node_physics::get_collision_filter() const -> const std::shared_ptr<erhe::physics::Collision_filter>&
{
    return m_create_info.collision_filter;
}

void Node_physics::set_collision_filter(const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter)
{
    set_value(collision_filter_property, Object_reference{collision_filter});
}

void Node_physics::reapply_collision_filter()
{
    if (m_rigid_body) {
        m_rigid_body->set_collision_filter(m_create_info.collision_filter);
    }
}

auto Node_physics::is_trigger() const -> bool
{
    return m_create_info.is_sensor;
}

void Node_physics::set_trigger(const bool trigger)
{
    set_value(is_trigger_property, trigger);
}

auto Node_physics::get_gravity_factor() const -> float
{
    return m_create_info.gravity_factor;
}

void Node_physics::set_gravity_factor(const float gravity_factor)
{
    set_value(gravity_factor_property, gravity_factor);
}

void Node_physics::apply_gravity_factor()
{
    if (m_rigid_body && (m_rigid_body->get_motion_mode() != Motion_mode::e_static)) {
        m_rigid_body->set_gravity_factor(m_create_info.gravity_factor);
    }
}

auto Node_physics::get_wind_receptivity() const -> float
{
    return m_wind_receptivity;
}

void Node_physics::set_wind_receptivity(const float wind_receptivity)
{
    set_value(wind_receptivity_property, wind_receptivity);
}

void Node_physics::set_wake_on_attach(const bool wake_on_attach)
{
    m_wake_on_attach = wake_on_attach;
}

auto Node_physics::get_initial_linear_velocity() const -> glm::vec3
{
    return m_create_info.linear_velocity;
}

void Node_physics::set_initial_linear_velocity(const glm::vec3& velocity)
{
    set_value(initial_linear_velocity_property, velocity);
}

auto Node_physics::get_initial_angular_velocity() const -> glm::vec3
{
    return m_create_info.angular_velocity;
}

void Node_physics::set_initial_angular_velocity(const glm::vec3& velocity)
{
    set_value(initial_angular_velocity_property, velocity);
}

auto Node_physics::get_center_of_mass_offset() const -> glm::vec3
{
    const std::shared_ptr<erhe::physics::ICollision_shape>& shape = m_create_info.collision_shape;
    if (!shape) {
        return glm::vec3{0.0f};
    }
    return shape->get_offset().value_or(glm::vec3{0.0f});
}

void Node_physics::set_center_of_mass_offset(const glm::vec3& offset)
{
    set_value(center_of_mass_offset_property, offset);
}

void Node_physics::apply_center_of_mass_offset(const glm::vec3& offset)
{
    if (offset == get_center_of_mass_offset()) {
        return; // R4: the bridge set is reached for every write
    }
    std::shared_ptr<erhe::physics::ICollision_shape> shape = m_create_info.collision_shape;
    if (!shape) {
        log_physics->warn("set_center_of_mass_offset() ignored: no collision shape");
        return;
    }
    if (shape->get_offset().has_value()) {
        shape = shape->get_inner_shape(); // unwrap existing offset-center-of-mass wrapper
    }
    if (offset != glm::vec3{0.0f}) {
        shape = erhe::physics::ICollision_shape::create_offset_center_of_mass_shape_shared(shape, offset);
    }
    m_create_info.collision_shape = shape;
    recreate_rigid_body();
}

void Node_physics::begin_interaction()
{
    if (!m_rigid_body) {
        return;
    }
    // m_motion_mode already holds the intended mode - no need to re-read
    // from the rigid body, which may already be kinematic from a prior call.
    m_rigid_body->set_motion_mode(Motion_mode::e_kinematic_physical);
    m_rigid_body->begin_move();
}

void Node_physics::end_interaction()
{
    if (!m_rigid_body) {
        return;
    }
    m_rigid_body->set_motion_mode(get_effective_motion_mode());
    m_rigid_body->end_move();
}

void Node_physics::teleport_to_node()
{
    if (!m_rigid_body) {
        return;
    }
    const Motion_mode mode = m_rigid_body->get_motion_mode();
    // Only dynamic and kinematic-physical bodies can carry or produce kinetic energy
    // that the simulation reacts to. Static and kinematic-non-physical bodies need no
    // settling (the latter is already teleported via set_world_transform without
    // inducing velocity).
    if ((mode != Motion_mode::e_dynamic) && (mode != Motion_mode::e_kinematic_physical)) {
        return;
    }
    const erhe::scene::Trs_transform& world_from_node = get_node()->world_from_node_transform();
    m_rigid_body->teleport(
        erhe::physics::Transform{
            glm::mat3_cast(world_from_node.get_rotation()),
            world_from_node.get_translation()
        }
    );
    m_rigid_body->set_linear_velocity (glm::vec3{0.0f});
    m_rigid_body->set_angular_velocity(glm::vec3{0.0f});
    if (mode == Motion_mode::e_dynamic) {
        // Wake the body so gravity and the (possibly new) constraint take effect from
        // the placed pose instead of the body floating asleep.
        m_rigid_body->begin_move();
        m_rigid_body->end_move();
    }
}

}
