# Property migrations: order and recipe

The remaining migrations to `erhe::property`, in the order to work them,
with the recipe each follows and the verification each must pass. The
design record is `doc/property-system.md` (sections 4.1 to 4.14 own the
landed designs; section 6 owns the future-work list this order comes
from); `doc/property-inventory.md` owns the per-field status and is
updated in the same commit as every registration change; the library
reference is `src/erhe/property/notes.md`; the holders that give the
migrations their point are `doc/content-library-folders.md` (folders) and
`doc/style-library.md` (styles, any class per style).

## 1. Order

Two kinds of work, interleaved as listed. A member-backed (bridged, D18)
registration is always local, so no node, folder or style can hold it and
no descendant inherits it; moving it to the entry store is what makes it
holdable. A hand-written row is authored state the Properties window still
draws by hand and no property carries at all.

1. `Rendertarget_mesh`: width, height, pixels per meter (hand-written
   rows).
2. `Animation`: start time, end time (hand-written rows).
3. `Node_joint`: enable collision, connected node as a node-typed object
   reference (hand-written rows).
4. Geometry graph and texture graph node parameters (section 4.5), last;
   sharing them through a style is rarely wanted.

Done and the template for a derived row: the Light flux slider and
blackbody swatch (section 4.3) are computed properties (D26), flux
writable through a setter over intensity. Done and following the bridged
owner recipe below: `Layout` (section 4.13), `Grid` and `Brush_placement`
(section 4.11).

Stays as it is: `Node`'s transform (bridged for the reasons D18 gives),
`Physics_joint_settings` limits and drives (no property value form) and
the scene's ambient light (a settings block).

## 2. Recipe for a bridged owner

The Light (section 4.3), Camera (section 4.4), Node_physics (section
4.10) and Layout (section 4.13) migrations are the template. A property that models "this body
instance" belongs to the attachment; one that models "what kind of matter
this is" belongs to the shared material (Node_physics kept the KHR motion
fields and its role, the physics material took damping, wind receptivity
and density) - settle that split before registering anything.

- Register every field with `register_property` in the entry store,
  `.inherits = true`, the previous initializer as `.default_value`. An
  enumeration passes its `Enum_info` table.
- Keep the engineered struct the readers use (the rigid body create info,
  the `Projection`) as a MIRROR of the effective values: override
  `on_property_changed` (the virtual hook), test the property's owner
  type against the class's own with `is_owner_type_or_descendant`, and
  refresh the mirror from `get_value`. The hook runs for every source of
  a change - local, style, inherited - so a value held by the node above
  reaches the struct.
- Route every writer through setters (`set_x()` per field, plus a whole
  struct setter where writers set several at once). The non-const
  accessor to the struct goes away; `grep -rn "->struct()"` finds the
  writers. A per-frame writer (the XR camera) calls the setter and pays
  nothing while the value is unchanged: the store early-outs.
- Keep the consequence a change has (recreate the body, set the body's
  damping) in the same hook, where the old `after_set` did it.
- `visible_when` callbacks may keep casting to the class: the D12
  listing rule never evaluates them on a holder (a holder lists a
  secondary property by its own value, D30).
- A `property_changed` METADATA callback (as opposed to the virtual hook)
  also casts to the class; `deliver` skips it on a holder. Prefer the
  virtual hook.
- Object references (D28) stay `Property<Object_reference>`; a holder
  resolves the reference by identity, and the picker's candidate list
  comes from `collect_reference_candidates`. On glTF load a reference in
  an `ERHE_node` / `ERHE_light` / `ERHE_camera` `properties` map resolves
  late through `Gltf_data::unresolved_object_properties` (D28); an owner
  with a native KHR carrier for the reference (Node_physics: the
  collider's material and filter) keeps the identity the constructor got
  and lets its own map decide only whether the value stays local.
- A constructor that takes an engineered struct (a create info) writes a
  local value only for the fields that differ from the property defaults,
  so a default-constructed instance stays open to a holder.
- A value that is "unset" in the engineered struct (an optional mass)
  maps to source `default`: test `get_value_source` in the hook instead
  of adding an "is set" property.
- glTF: the owner's extension keeps writing its explicit fields and the
  `properties` map of local values; on load the map is the item's
  complete local set (`clear_local_properties_not_listed`, the rule
  `ERHE_light` and `ERHE_camera` follow), so a value inherited from the
  node survives a reload. A post-load pass that "widens" or "adjusts" a
  value (the scene-open content fit) writes a LOCAL value only
  (`has_local_value`), never one that comes from a node or a style: the
  editor-state operations that assign styles run after such passes.
- Update `doc/property-inventory.md` (storage kind `entry`, `inherits`)
  and the owner's subsection in `doc/property-system.md` in the same
  commit. Add a test where the owner has a test directory
  (`src/erhe/scene/test/test_camera_properties.cpp` is the shape:
  defaults, setter to mirror, untyped access with enum labels, node-held
  value inherited into the mirror, clone).

## 3. Recipe for a hand-written row

The Material recipe of section 4.1 for a stored value; for a derived
value a writable computed property (D26, the Light flux slider of
section 4.3 is the shape): `register_computed` with the provider, a
setter that writes the stored property, and that property, so the
generic row edits it and undo records the stored property's change.
Delete the hand-written row in the same commit; the generic section
draws it.

## 4. Verification

Headless, from the repo root, the loop of `erhe-headless-verify`:

1. `cmake --build build_vs2026_vulkan_headless --target editor --config Debug`,
   launch with `ERHE_AI_DRIVER=1`, wait for `Main loop: completed frame 12`
   in `logs/log.txt`.
2. Over `scripts/mcp_call.py` (or a small `urllib` script; `scene_name`
   is required by the node tools, item ids reshuffle per launch):
   `get_addable_item_properties` on an empty node lists `<Owner>.<name>`
   for every migrated field; `set_item_property` of one on the node and
   `null` on the attachment below makes the attachment read it with
   source `inherited`; `create_style` plus `set_item_property` of
   `<Owner>.<name>` on the style and the style on the node reads
   `inherited` on the attachment too; the engineered struct reflects it
   (`get_node_details` / the owner's query tool); `save_scene`,
   `open_scene`: sources are still `inherited`; `close_scene` and
   `scene-close check: all N released` in the log.
3. `./scripts/build_ninja_win_vulkan.bat editor` links the windowed
   editor; the owner's gtest suite passes (`build_tests`, serially).
4. Hand off for the user's interactive check; do not keep headless
   testing after that.

## 5. Traps already hit

- A computed property of a descendant class (Mesh world bounds) listed as
  secondary on a node ran its compute on the node: the D30 rule excludes
  computed and bridged properties for that reason. Do not weaken it.
- `for_each_local_value` needs its bridged list sorted by index; the
  registration order across translation units is not the chain order.
- `find_scene("")` in the MCP server does not default to the first scene
  even where a tool's schema says so; default explicitly.
- MCP `edit_physics_body` is not undoable: an `undo` after it pops the
  previous operation.
- The Visual Studio MCP server may refuse connections; the crash fallback
  is the headless editor launched with stderr redirected to a file (the
  crash handler prints a symbolized backtrace there).
