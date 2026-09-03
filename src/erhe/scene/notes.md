# erhe_scene

## Purpose
A glTF-like 3D scene graph providing hierarchical transforms, node attachments (meshes, cameras, lights, skins), animations, and scene management. Nodes form a parent-child tree with automatic world transform propagation. The library is graphics-API-agnostic and does not perform any rendering itself.

## Key Types
- `Scene` -- Top-level container owning the root node, flat node list, mesh layers, light layers, cameras, and skins. Provides `update_node_transforms()` and lookup by ID.
- `Node` -- Extends `Hierarchy` (parent/child tree). Holds `Node_transforms` (parent-from-node and world-from-node `Trs_transform`), attachments, and a `Scene_host` pointer. Supports cloning. Registers `translation`, `rotation` and `scale` as erhe::property properties bridged onto the parent-from-node `Trs_transform` (`Node::translation_property` etc.; writes run the same world-transform update as `set_parent_from_node`), so the editor's generic property rows, undo and MCP reach the transform without a second copy of it (doc/property-system.md section 4.2). `world_translation_property` / `world_rotation_property` / `world_scale_property` are computed properties (D26) reading `world_from_node_transform()`; `handle_transform_update` pushes them to expressions, so a descendant's values follow a parent move when `Scene::update_node_transforms` recomputes it.
- `Node_attachment` -- Base class for things attached to nodes (Mesh, Camera, Light). Receives notifications on node transform changes and scene host changes. Its property inheritance parent is its node (`visible`, `shadow_cast`, `lightmapped` flow node -> attachment; `set_node` brackets the move with the inheritance snapshot and keeps the attachment alive while the old node's list releases it).
- `Mesh_primitive` -- Primitive + Material pair, a `Dependency_object` with its own owner type: `material` is an object property (`Mesh_primitive::material_property`, `register_member` over the member, `doc/property-system.md` D28 / section 4.9) whose `after_set` notifies the owning mesh's scene host; `Mesh::set_primitive_material` writes it and stays the one writer. Carries an owner link (mesh, index) the mesh stamps after every primitive-list change. Registers member-backed properties only and is never observed: the mesh holds primitives by value, and a vector reallocation copy-constructs the base.
- `Mesh` -- Node attachment holding a vector of `Mesh_primitive`. Addresses its primitives as property sub-objects (D29: `get_property_sub_object_count` / `get_property_sub_object` / `get_property_sub_object_label`). Supports raytrace primitives for CPU-side picking. `get_aabb_world()` returns POSED world bounds for a skinned mesh: it unions the primitives' per-joint rest boxes (`Buffer_mesh::joint_bounding_boxes`) transformed by `world_from_bind` (`get_skinned_aabb_world()`), and does NOT apply the mesh node's transform, which skinning ignores. Correct because a skinned position is a convex combination of its per-joint images, so it lies inside the union. Uncached - joints move every frame and primitives can be rebuilt behind the Mesh's back, so there is no reliable invalidation signal. `world_bounds_min_property` / `world_bounds_max_property` are computed properties (D26) reading `get_aabb_world()` (zero for an invalid box), pushed to expressions from `handle_node_transform_update` and the primitive changes.
- `Camera` -- Node attachment with a `Projection` (perspective/orthogonal/XR). Computes `clip_from_world` transforms. The `Projection` fields are registered as bridged `erhe::property` properties (`Camera::z_far_property`, ...), so `projection()` writes and property writes reach the same state; exposure and shadow range live in the property store (`doc/property-system.md` section 4.4).
- `Light` -- Node attachment for directional, point, and spot lights. Computes shadow projection transforms. The authored state (`light_type`, `color`, `intensity`, `temperature`, `range`, spot angles, `cast_shadow`) is registered `erhe::property` properties read through `get_color()`-style accessors; every change re-resolves the scene light set through the shared changed callback (`Scene_host::on_light_changed`), so no writer notifies by hand (`doc/property-system.md` section 4.3, D19).
- `Layout` -- Node attachment that owns a volume (an `Aabb` in the node's local space) and arranges its node's direct children inside that volume by computing each child's `parent_from_node` (`Layout::update()`). A single class selects between `Layout_type::stack` (one signed axis), `grid` (an X/Y/Z cell grid), and `flow` (children wrapped into lines along the primary axis, lines into sheets along the secondary axis, sheets stacked along the tertiary axis). The layout owns each child's translation and (for `stretch` alignment) scale; child rotation is forced to identity. A child's footprint is measured via `compute_content_local_aabb()` (its own mesh primitives plus descendants); a child that is itself a `Layout` contributes its declared volume instead, which both matches intent and breaks the recursion cycle. The parameters (type, volume, axes, gap, grid track count) are registered `erhe::property` properties (doc/property-system.md section 4.13) behind typed accessors; the grid track extent lists are not.
- `Layout_item` -- Optional per-child node attachment holding alignment (`negative`/`positive`/`stretch` per axis), margins, and grid cell/span. A child without one is laid out using default values.
- `Projection` -- Camera projection configuration supporting many types (perspective vertical/horizontal, orthogonal, XR asymmetric, generic frustum).
- `Transform` -- Matrix + inverse matrix pair with factory methods for projection setups.
- `Trs_transform` -- Extends `Transform` with decomposed translation, rotation, scale, and skew. Supports interpolation.
- `Animation` / `Animation_sampler` / `Animation_channel` -- Keyframe animation system supporting step, linear, and cubic spline interpolation for translation, rotation, scale, and weights.
- `Skin` -- Skeletal skinning data (joint nodes + inverse bind matrices, plus the optional glTF `skeleton` pivot node). `get_skin_transform_root()` returns the node an editor should transform to move a skinned mesh: skinning ignores the mesh node's own transform (glTF 2.0 requires it), so only a common ancestor of the joints moves the posed result. Uses `Skin_data::skeleton` when set, else the closest common ancestor of the joints.
- `Mesh_layer` / `Light_layer` -- Organize meshes and lights into layers with flags and IDs.
- `Scene_host` -- Abstract interface for registering/unregistering scene objects.

## Public API
- Create a `Scene`, add nodes with `register_node()`, attach meshes/cameras/lights.
- Call `scene.update_node_transforms()` each frame to propagate world transforms.
- Use `Node::set_parent_from_node()` / `set_world_from_node()` to position nodes.
- `Camera::projection_transforms(viewport)` returns clip-from-world matrices.
- `Animation::apply(time)` drives node transforms from keyframe data.

## Dependencies
- erhe::item (Item, Hierarchy, Unique_id)
- erhe::primitive (Primitive, Material)
- erhe::raytrace (IGeometry, IInstance, IScene -- for CPU raytrace picking)
- erhe::math (Viewport, Aabb)
- glm

## Notes
- Transform updates use a global serial number to avoid redundant recomputation.
- `get_attachment<T>(node)` is a convenience template for finding typed attachments.
- Mesh layers use a `Layer_id` (uint64) and flag bits for filtering during rendering.
