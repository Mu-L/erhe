#pragma once

#include "erhe_scene/node_attachment.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_property/dependency_property.hpp"

#include <vector>

namespace erhe          { class Item_host; }
namespace erhe::physics {
    class Collision_filter;
    class IWorld;
    class Physics_material;
}

namespace editor {

class Node_physics
    : public erhe::Item<
        erhe::Item_base,
        erhe::scene::Node_attachment,
        Node_physics,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    explicit Node_physics(const erhe::physics::IRigid_body_create_info& create_info);
    Node_physics(const Node_physics& src, erhe::for_clone);

    explicit Node_physics(const Node_physics&);
    Node_physics& operator=(const Node_physics&);
    ~Node_physics() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Node_physics"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::physics; }

    // Implements / overrides Node_attachment
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Registered properties (erhe::property, doc/property-system.md
    // section 4.10), member-backed (D18) over the create info and the
    // intended motion mode: the members are the authored state the rigid
    // body is (re)created from, and a property write pushes the change to
    // the live body or recreates it. The accessors below write through the
    // properties, so every writer notifies. mass and center_of_mass_offset
    // carry hand-written bridges (the mass is optional in the create info
    // and read from the live body until set; the offset is a wrapper
    // around the collision shape).
    static const erhe::property::Property<erhe::physics::Motion_mode>      motion_mode_property;
    static const erhe::property::Property<bool>                            is_trigger_property;
    static const erhe::property::Property<float>                           mass_property;
    static const erhe::property::Property<float>                           friction_property;
    static const erhe::property::Property<float>                           restitution_property;
    static const erhe::property::Property<float>                           linear_damping_property;
    static const erhe::property::Property<float>                           angular_damping_property;
    static const erhe::property::Property<float>                           gravity_factor_property;
    static const erhe::property::Property<float>                           wind_receptivity_property;
    static const erhe::property::Property<glm::vec3>                       initial_linear_velocity_property;
    static const erhe::property::Property<glm::vec3>                       initial_angular_velocity_property;
    static const erhe::property::Property<glm::vec3>                       center_of_mass_offset_property;
    static const erhe::property::Property<erhe::property::Object_reference> physics_material_property;
    static const erhe::property::Property<erhe::property::Object_reference> collision_filter_property;

    // Public API
    [[nodiscard]] auto get_rigid_body()       ->       erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_rigid_body() const -> const erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_collision_shape() const -> const std::shared_ptr<erhe::physics::ICollision_shape>&;

    // Replaces the collision shape wholesale (any center-of-mass offset
    // wrapper carried by the old shape is dropped; set it again afterwards
    // if needed). Changing the shape recreates the rigid body.
    void set_collision_shape(const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape);

    void before_physics_simulation();
    void after_physics_simulation();

    void set_physics_world(erhe::physics::IWorld* value);
    [[nodiscard]] auto get_physics_world() const -> erhe::physics::IWorld*;

    // Returns the intended/persistent motion mode.
    // This is always the "real" mode, even when the rigid body
    // is temporarily overridden to kinematic during interaction.
    [[nodiscard]] auto get_motion_mode() const -> erhe::physics::Motion_mode;
    void               set_motion_mode(erhe::physics::Motion_mode mode);

    // Temporarily override rigid body to kinematic for user interaction.
    // get_motion_mode() continues to return the intended mode.
    void begin_interaction();
    void end_interaction();

    // Snap the rigid body instantly to the node's current world pose with zero
    // linear/angular velocity (and wake it if dynamic). Used by editor operations
    // that reposition a body (joint create / flip, transform edits) so the
    // simulation does not react with a corrective impulse or a kinematic velocity
    // injection (the MoveKinematic path in set_world_transform). No-op for bodies
    // that cannot produce such a reaction (no body / static / kinematic non-physical).
    void teleport_to_node();

    // The rigid body's scalar dynamics. The create info holds the authored
    // value, the live body gets it on set; the physics tool's transient
    // overrides go to the body only and are not seen here.
    [[nodiscard]] auto get_mass           () const -> float; // the create info's mass, else the live body's (0 when neither)
    void               set_mass           (float mass);      // scales the local inertia with the mass
    [[nodiscard]] auto get_friction       () const -> float;
    void               set_friction       (float friction);
    [[nodiscard]] auto get_restitution    () const -> float;
    void               set_restitution    (float restitution);
    [[nodiscard]] auto get_linear_damping () const -> float;
    void               set_linear_damping (float linear_damping);
    [[nodiscard]] auto get_angular_damping() const -> float;
    void               set_angular_damping(float angular_damping);

    // Shared physics material; updates both create info and the live rigid
    // body. reapply_physics_material() pushes the current material to the
    // body again after the Physics_material item was edited, so the
    // backend re-snapshots the values.
    [[nodiscard]] auto get_physics_material    () const -> const std::shared_ptr<erhe::physics::Physics_material>&;
    void               set_physics_material    (const std::shared_ptr<erhe::physics::Physics_material>& physics_material);
    void               reapply_physics_material();

    // Shared collision filter; updates both create info and the live rigid
    // body. reapply_collision_filter() pushes the current filter to the
    // body again after the Collision_filter item was edited, so the
    // backend re-snapshots the compiled filter.
    [[nodiscard]] auto get_collision_filter    () const -> const std::shared_ptr<erhe::physics::Collision_filter>&;
    void               set_collision_filter    (const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter);
    void               reapply_collision_filter();

    // Trigger (sensor) flag. Changing it recreates the rigid body. A static
    // trigger body is created kinematic non-physical (Jolt sensors must be
    // non-static to detect static bodies); get_motion_mode() continues to
    // return the user-facing motion mode.
    [[nodiscard]] auto is_trigger () const -> bool;
    void               set_trigger(bool trigger);

    [[nodiscard]] auto get_gravity_factor() const -> float;
    void               set_gravity_factor(float gravity_factor);

    // Wind receptivity in kg/s: the scene wind system applies
    // force = wind_receptivity * (wind_velocity - body_velocity) at the body
    // center of mass each fixed step. 0 (default) = unaffected by wind (the
    // body is never touched, so it can sleep normally).
    [[nodiscard]] auto get_wind_receptivity() const -> float;
    void               set_wind_receptivity(float wind_receptivity);

    // Bodies enter the world deactivated (quiet scene loading). With
    // wake_on_attach set, a dynamic body is woken right after it is added
    // to the world, so a freshly created body starts simulating without a
    // separate wake_physics_bodies pass.
    void set_wake_on_attach(bool wake_on_attach);

    // Initial velocities apply when the rigid body is (re)created - at scene
    // attach or after set_trigger() / set_center_of_mass_offset(); they do
    // not change a live body (use the rigid body API for that).
    [[nodiscard]] auto get_initial_linear_velocity () const -> glm::vec3;
    void               set_initial_linear_velocity (const glm::vec3& velocity);
    [[nodiscard]] auto get_initial_angular_velocity() const -> glm::vec3;
    void               set_initial_angular_velocity(const glm::vec3& velocity);

    // Center of mass offset, implemented by (re)wrapping the collision shape
    // in an offset-center-of-mass wrapper. Changing it recreates the rigid body.
    [[nodiscard]] auto get_center_of_mass_offset() const -> glm::vec3;
    void               set_center_of_mass_offset(const glm::vec3& offset);

    std::vector<glm::vec3> markers;

private:
    // The motion mode the rigid body is actually created with: same as
    // m_motion_mode, except that static trigger bodies are created kinematic
    // non-physical (Jolt sensors must be non-static to detect static bodies).
    [[nodiscard]] auto get_effective_motion_mode() const -> erhe::physics::Motion_mode;

    // Creates m_rigid_body from m_create_info, applying the effective motion
    // mode and the current node world transform. Does not add the body to the
    // world; Scene_root::register_node_physics() does that.
    void create_rigid_body(erhe::physics::IWorld& physics_world);

    // Recreates the rigid body from (updated) create info while attached to a
    // scene, going through Scene_root unregister/register so that dependent
    // bookkeeping (joint constraints) stays consistent. No-op when detached.
    void recreate_rigid_body();

    // The consequences of a property write (the after_set hooks and the
    // hand-written bridge sets): the member already holds the new value.
    void apply_motion_mode          ();
    void apply_mass                 (float mass);
    void apply_friction             ();
    void apply_restitution          ();
    void apply_damping              ();
    void apply_gravity_factor       ();
    void apply_center_of_mass_offset(const glm::vec3& offset);

    erhe::physics::IWorld*                      m_physics_world{nullptr};
    erhe::physics::IRigid_body_create_info      m_create_info;
    std::shared_ptr<erhe::physics::IRigid_body> m_rigid_body;
    erhe::physics::Motion_mode                  m_motion_mode{erhe::physics::Motion_mode::e_dynamic};
    float                                       m_wind_receptivity{0.0f};
    bool                                        m_wake_on_attach{false};
};

}
