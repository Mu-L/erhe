# Properties window: one path for every row

The Properties window (`src/editor/windows/properties.cpp`) draws its rows
through two paths, and the two handle a multi-selection differently. The
task is to make the registered-property path the only path for the
window's rows, so a selection of any size and any mix of types gets the
same treatment: per-type sections, mixed-value display, one compound
undo entry per edit, Copy / Paste Properties and Add Property.

The design record is `doc/property-system.md` (D12 owns the window's
generic section, D30 the holder rule, section 6 the future work);
`doc/property-inventory.md` owns the per-field status and its "Not yet
migrated" table lists the hand-written rows that are authored state;
`doc/property-migration-handoff.md` owns the order and recipe of the
per-owner migrations that this task depends on.

## 1. The two paths

1. Registered properties. `Dependency_property_rows::add_rows` draws the
   properties of an item's own chain, the attached properties of its
   holder type and the secondary properties it holds (D12), for one item
   or for a group of items of one property owner type. `Properties::imgui`
   partitions a multi-selection by owner type (a content-library entry
   standing for its item, a node followed by its attachments) and draws
   one such section per type; a single selection draws the section under
   the item's own group through `item_properties` and `dependency_properties`.
2. Hand-written rows. `Properties::item_properties` and the per-class
   functions it calls (`scene_properties`, `light_properties`,
   `mesh_properties`, `node_physics_properties`, `node_joint_properties`,
   `collision_filter_properties`, `physics_joint_settings_properties`,
   `brush_properties`, `texture_properties`, ...) add `add_entry` rows
   per item: the name row, the Locks row, the `item_flags` grid, the
   tags, the attachment Remove buttons, the read-only diagnostics, and
   the authored rows the inventory's "Not yet migrated" table lists.
   `material_properties` is a third variant with its own inspect
   snapshot for the sampler rows. These rows are drawn once per selected
   item, in a group per item, with no mixed-value display and no shared
   edit.

## 2. Requirements

- R1 Every row the window draws for an item comes from the registered
  property path or is a read-only diagnostic. A selection of two items
  of one type shows one section for that type with mixed markers; a
  selection of two types shows one section per type; nothing is hidden
  because a second item is selected.
- R2 Authored state that is not yet a property becomes one, or gets a
  bridge (D18) where the storage must stay a member: the name (a bridged
  string property over `Item_base::get_name` / `set_name`), the
  persistent flags that are authored (`show_in_ui`, `lock_edit`, the
  viewport locks, `no_transform_update`, ... as bridged booleans or one
  enumeration-like group), the tags, and the rows of the inventory's
  table (`Rendertarget_mesh`, `Animation`, `Node_joint`, the Light
  derived rows as computed properties with setters, D26).
- R3 Diagnostics (counts, dimensions, the live rigid body's state,
  raytrace state, skin joints) stay read-only rows, drawn per item; they
  are not authored state and need no mixed-value handling. They may
  become read-only computed properties (D26) where that removes a
  hand-written function for free.
- R4 The material inspect snapshot (`material_properties`,
  `m_material_state`) is retired once every material row is a property
  row; the preview render that an open Properties window triggers stays.
- R5 List-valued state with no `Property_value` form
  (`Physics_joint_settings` limits and drives, the scene's ambient light
  settings block) keeps its hand-written editor, drawn per item, and is
  the documented exception.

## 3. Order

1. Make the item-level rows properties: name (bridge), the authored
   flags (bridges; `item_flags` grid goes), tags. This alone gives every
   item type the shared multi-selection treatment for the rows every
   item has.
2. The per-owner migrations in the order `doc/property-migration-handoff.md`
   gives (Light derived rows, Layout, Grid, Brush_placement,
   Rendertarget_mesh, Animation, Node_joint, graph nodes), each deleting
   its hand-written rows in the same commit.
3. Retire `material_properties` and the inspect snapshot (R4).
4. Fold the remaining per-class functions into diagnostics-only helpers
   (R3), and make `item_properties` a thin frame: the group header, the
   diagnostics, the registered section, the attachment Remove buttons.

## 4. Verification

Headless over MCP: `get_item_properties` lists the migrated rows with the
right storage and sources; `set_item_property` and `undo` round-trip each
bridged field. The multi-selection behaviour is interactive only: select
two nodes with meshes and bodies, two physics materials, and a material
plus a body, and check each type has one section, mixed markers where
values differ, and one undo entry per edit; the user drives that check
(AGENTS.md "Once the user starts testing").
