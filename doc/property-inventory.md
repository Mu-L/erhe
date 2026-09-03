# Property inventory

The status of every editor-visible item field with respect to the
property system (`erhe::property`, design record `doc/property-system.md`):
which fields are registered properties, how each is stored, and which
fields the Properties window still draws by hand. This document is the
owner of that status; the design record's sections 4.1 to 4.13 own the
design of each migration and refer here for the per-field list.

Update this document in the same commit as any registration added,
removed or changed in storage kind, and whenever a hand-written row is
migrated or added. The same commit updates the owner's subsection in
`doc/property-system.md` (4.1 to 4.13) when the design changed, and
`src/erhe/property/notes.md` when a library mechanism changed; the design
record's "Document roles" paragraph states the split.

## Storage kinds

- **entry** - the value lives in the object's entry store
  (`Dependency_object`), with the default, style, inherited and
  expression layers (D5).
- **member** - member-backed through `Property<T>::register_member`
  (D18): the object's member is the storage, the property reads and
  writes it, and an `after_set` hook runs the consequence. Always reports
  `Value_source::local`.
- **bridge** - member-backed through a hand-written `Property_bridge`
  (D18), used where the value is not a plain member or the set has a side
  effect the member write does not cover. Does its own no-op check.
- **computed** - read-only, `register_computed` (D26).
- **attached** - `register_attached` (D3); no user yet.

The Properties window tints a row's label by its value source (D12):
member and bridge rows are blue, entry rows green / gray / cyan / orange /
purple by layer, computed rows dim gray. Untinted rows are hand-written.

## Registered properties

### Item_base (`src/erhe/item/erhe_item/item.cpp`)

| Property | Storage | Notes |
|---|---|---|
| visible | entry | flag mirror |
| shadow_cast | entry | flag mirror, group Rendering |
| lightmapped | entry | flag mirror, group Rendering |

### Hierarchy (`src/erhe/item/erhe_item/hierarchy.cpp`)

| Property | Storage | Notes |
|---|---|---|
| child_count | computed | |

### Node (`src/erhe/scene/erhe_scene/node.cpp`, section 4.2)

| Property | Storage | Notes |
|---|---|---|
| translation, rotation, scale | bridge | over `Trs_transform`, no matrix round trip |
| world_translation, world_rotation, world_scale | computed | |

### Mesh and Mesh_primitive (`src/erhe/scene/erhe_scene/mesh.cpp`, sections 4.9, D29)

| Owner | Property | Storage | Notes |
|---|---|---|---|
| Mesh | world_bounds_min, world_bounds_max | computed | |
| Mesh_primitive (sub-object) | material | member | object reference, Material |

### Material (`src/erhe/primitive/erhe_primitive/material.cpp`, section 4.1)

| Property | Storage | Notes |
|---|---|---|
| base_color, opacity, roughness, metallic, reflectance, emissive, ior, transmission, normal_texture_scale, occlusion_texture_strength, alpha_cutoff | entry | scalars and colors |
| normalmap_encoding, bxdf_model, blending_mode, circular_brushed_metal_texgen_mode | entry | enumerations |
| double_sided, use_circular_brushed_metal, use_aniso_control | entry | booleans |
| base_color_texture, metallic_roughness_texture, normal_texture, occlusion_texture, emissive_texture | member | object references, texture or graph texture |
| `<slot>`_texture_texgen_mode (5) | member | enumeration, affects shader variant |
| `<slot>`_texture_uv_rotation, _uv_offset, _uv_scale (15) | member | |

Not properties: the five slot samplers (`Material_data`, edited through
`Material_change_operation`).

### Light (`src/erhe/scene/erhe_scene/light.cpp`, section 4.3)

| Property | Storage | Notes |
|---|---|---|
| light_type, color, intensity, temperature, range, inner_spot_angle, outer_spot_angle, cast_shadow | entry | shared property_changed re-resolves the light set |

### Camera (`src/erhe/scene/erhe_scene/camera.cpp`, section 4.4)

| Property | Storage | Notes |
|---|---|---|
| projection_type, infinite_z_far | member | over `Projection` |
| fov_x, fov_y, fov_left, fov_right, fov_up, fov_down | member | over `Projection`, angle rows |
| ortho_left, ortho_width, ortho_bottom, ortho_height, frustum_left, frustum_right, frustum_bottom, frustum_top, z_near, z_far | member | over `Projection`, logarithmic extents |
| exposure, shadow_range | entry | |

### Node_physics (`src/editor/scene/node_physics.cpp`, section 4.10)

| Property | Storage | Notes |
|---|---|---|
| motion_mode | member | enumeration, sets the body's effective mode |
| is_trigger | member | recreates the body |
| friction, restitution, linear_damping, angular_damping, gravity_factor | member | over the create info, pushed to the live body |
| wind_receptivity, initial_linear_velocity, initial_angular_velocity | member | no live consequence |
| mass | bridge | optional in the create info, read from the live body until set |
| center_of_mass_offset | bridge | collision shape wrapper, recreates the body |
| physics_material, collision_filter | member | object references |

### Grid (`src/editor/grid/grid.cpp`, section 4.11)

| Property | Storage | Notes |
|---|---|---|
| plane_type, center, rotation | member | after_set re-derives the transform |
| intersect_enable, snap_enabled, cell_size, cell_div, cell_count | member | |
| level0_color .. level3_color, level0_width .. level3_width | member | accessor lambdas into the two arrays |
| label_enable, label_text_fraction, label_spacing, label_fade, label_color | member | |

Every Grid property's property_changed touches the settings store.

### Physics_material (`src/erhe/physics/erhe_physics/physics_material.cpp`, section 4.12)

| Property | Storage | Notes |
|---|---|---|
| static_friction, dynamic_friction, restitution | entry | bodies re-snapshot through the Node_physics observer |
| friction_combine, restitution_combine | entry | enumerations |

### Layout (`src/erhe/scene/erhe_scene/layout.cpp`, section 4.13)

| Property | Storage | Notes |
|---|---|---|
| type, primary, secondary, tertiary | member | enumerations |
| volume_min, volume_max | member | accessor lambdas into the Aabb |
| gap, grid_track_count | member | track count validated to at least 1 per axis, visible for grid |

Not properties: the per-axis grid track extent lists.

### Brush_placement (`src/editor/brushes/brush_placement.cpp`, section 4.11)

| Property | Storage | Notes |
|---|---|---|
| brush | member | object reference, Brush |
| facet, corner | bridge | `GEO::index_t` as int, developer-only |

### Geometry graph nodes (`src/editor/geometry_graph/nodes/`, section 4.5)

One owner type per node kind; every parameter is `member` with
`mark_node_dirty` as after_set unless listed as a bridge.

| Node | Properties | Storage |
|---|---|---|
| Mesh_box_node | size, subdivisions, power | member |
| Mesh_cone_node | height, radius, use_bottom, slices, stacks | member |
| Mesh_disc_node | outer_radius, inner_radius, slices, stacks | member |
| Mesh_sphere_node | radius, slices, stacks | member |
| Mesh_torus_node | major_radius, minor_radius, major_steps, minor_steps | member |
| Conway_node | kis_height, truncate_ratio, chamfer_ratio, gyro_ratio | member |
| Subdivide_node | mode, iterations | member |
| Boolean_node | operation | member |
| Math_node | operation, a, b | member |
| Transform_node | translation, rotation_mode, rotation, rotation_quaternion, scale | member |
| Distribute_points_node | count, seed | member |
| Instance_on_points_node | scale, align | member |
| Scene_mesh_geometry_node | primitive | member |
| Geometry_output_node | name, physics, physics_motion | member |
| Float_value_node, Integer_value_node, Vector_value_node | value | member |
| Lattice_node | cage_min, cage_max, regenerate_attributes, show_cage | member |
| Lattice_node | auto_fit, divisions, interpolation | bridge (cage freeze, offset resample) |
| Sdf_sphere_node | radius, center | member |
| Sdf_capsule_node | p0, p1, radius0, radius1 | member |
| Sdf_sphere_node, Sdf_capsule_node, Sdf_from_geometry_node | voxel_size | bridge |
| Sdf_to_geometry_node | adaptivity | member |
| Sdf_boolean_node | operation | member |
| Sdf_offset_node | distance | member |
| Sdf_smooth_node | iterations | member |

Not properties: `Asset_reference` pickers, the lattice control points,
and the nodes with no parameters (transform_from_node, passthrough,
join, unary-op, groups, brush source).

### Texture graph nodes (`src/editor/texture_graph/texture_graph_properties.cpp`, section 4.5)

One owner type per descriptor, registered at startup: one `bridge` per
float, color, enumeration, bool and size parameter plus the seed of a
seeded descriptor. Gradient and curve parameters are not properties.

## Not yet migrated

Hand-written rows of the Properties window that are authored state, in
migration priority order. Each migration follows the Material recipe
(design record section 4.1).

| Owner | Fields | Notes |
|---|---|---|
| Physics_joint_settings | limits, drives | lists; no `Property_value` form, stay hand-written |
| Layout_item | align x / y / z, margin min and max, auto cell, grid cell, grid span | first attached-property user (design record section 6) |
| Light | flux, blackbody temperature | derived rows over intensity and temperature |
| Scene | ambient light | the per-scene overrides stay a settings block |
| Rendertarget_mesh | width, height, pixels per meter | |
| Animation | start time, end time | |
| Node_joint | enable collision, connected node | connected node as a node-typed object reference |

Rows that are not properties and stay hand-written: read-only
diagnostics (geometry and buffer mesh counts, texture dimensions,
raytrace state, skin joints, rigid body label / position / activity /
shape / inertia, brush polygon counts), name and id rows, and list
editors (attachments, samplers, animation channels and samplers, joint
limits and drives).
