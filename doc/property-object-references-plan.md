# Object reference properties - implementation plan

Status: plan, nothing implemented. Written 2026-09-03 for review; the
decisions below are proposals until the user confirms them. The design
record `doc/property-system.md` gains the settled form of D1-D4 below as
new decisions (D28, D29) when the work lands; until then this document is
the only place they live.

## Goal

Two kinds of item state that today sit outside the property system become
registered properties, so they get the generic rows, undo, MCP, styles,
observers and dirty tracking every other property has:

- the material of each `Mesh_primitive` of a `Mesh`
  (`Mesh_primitive::material`, written only through
  `Mesh::set_primitive_material`);
- the five texture slots of a `Material`
  (`Material_data::texture_samplers.<slot>.texture_reference`, edited by
  the Properties texture combo, MCP `edit_material` and the texture graph
  output nodes).

Both are pointers to other items, a value kind `Property_value` cannot hold
(D2 of the design record lists scalars, vectors, strings and
enumerations). The plan adds that kind to the library (D1), then migrates
the two users (D3, D4). The sampler state of a texture slot
(`erhe::graphics::Sampler`) is not an item and stays in `Material_data`
with `Material_change_operation`; the slot's `texgen_mode`, `rotation`,
`offset` and `scale` are ordinary value types and could become properties
by the Material recipe, but that is a separate scope decision and is not
part of this plan.

## Decisions

### D1 - `Object_reference` value type (library)

- `Property_value` gets a thirteenth alternative, appended so the existing
  variant indices stay stable: `Object_reference`, a struct holding
  `std::shared_ptr<Dependency_object> object` with defaulted `operator==`
  (pointer identity). `Property_type::object = 12`, `c_str` `"object"`,
  `zero_value` is the null reference, `Property_storable` admits it, and
  `make_value` / `get_as` pass it through. The library keeps D1 of the
  design record: it knows `Dependency_object`, not `Item_base`.
- The reference is strong. Both users hold `std::shared_ptr` today
  (a material keeps its textures alive, a mesh keeps its materials alive,
  and the asset manager's scene-close watchdog blesses the texture case as
  a transitive pin), so the property store holds exactly what the member
  held. A copy of the object (D10) shares the pointee, as the member copy
  did.
- Type restriction: the registration's validate callback (R4, value only)
  rejects a non-null reference that is not of the owner's accepted class,
  with a `dynamic_cast` on the pointee. Generic code never needs the
  restriction; the editor's drop filter and picker read
  `Property_ui::reference_item_types` (next point).
- `Property_ui` gets `uint64_t reference_item_types{0}`: the `Item_type`
  mask of the items the row accepts. It is data only, opaque to the
  library, like every other `Property_ui` field; the editor reads it for
  `item_reference_imgui`'s `allowed_types` and for the picker candidates.
- Member-backed registration. `Property<T>::register_member(name,
  owner_type, member pointer, metadata, after_set)` registers a bridged
  property (D18) whose storage is the owner's member: the library builds
  the `get` / `set` pair from the pointer-to-member, `set` writes the
  member and then runs the optional `after_set(Owner&)` hook (the
  consequence a hand-written bridge ran inline: the transform update, a
  graph node's `mark_dirty`, the primitive's scene-host notify). A
  `Member_value_traits<Member>` customization converts between the member
  type and the stored `Property_value`; the library provides the identity
  for every `Property_storable` type and, for a `std::shared_ptr<U>`
  member, the `dynamic_pointer_cast` to and from
  `Object_reference::object` (a non-null value whose pointee is not a `U`
  is rejected by the registration's validate, which `register_member`
  supplies for `shared_ptr` members). The semantics are exactly D18's -
  no second copy of the value exists - so owners no longer spell out a
  bridge; the existing `make_projection_bridge` (`camera.cpp`) and
  `make_node_member_bridge` (graph nodes) are candidates to migrate to it
  as follow-up work outside this plan.
- Text form (D16). `to_string` of a non-null reference is the pointee's
  `get_reference_path()`, a new virtual on `Dependency_object` that is the
  inverse of `resolve_expression_object`: the library returns an empty
  string, `Item_base` returns `get_name()`. A null reference is the empty
  string. Parsing needs a context object, so the library adds
  `parse_value(const Dependency_object& context, const Dependency_property&
  property, std::string_view text)`, which for `Property_type::object`
  resolves the text through `context.resolve_expression_object(text)`
  (empty text is the null reference; an unresolved name is `nullopt`) and
  delegates every other type to the existing overload. Names are not
  unique, exactly as for D22 expression references; MCP additionally
  accepts a session id (D5).
- Expressions (D22): an object property is neither a source nor a target,
  the same rule and the same code paths as `Property_type::string`.
- Inheritance, styles, coerce, computed and read-only apply as to any
  type; nothing in `Dependency_object` special-cases the alternative.
- `Property_set`, `Property_style`, copy / paste and `operator==` of
  items work unchanged through the variant's `operator==`.

### D2 - Editor and MCP handling of object values

- Rows (D12): `Property_type::object` draws `item_reference_imgui`
  (`src/editor/windows/item_reference.hpp`), so a row is a drop target
  filtered by `reference_item_types`, a drag source, a picker and a clear
  button, with `immediate` commit like the enumeration combo. The picker
  candidates come from one editor helper,
  `collect_reference_candidates(Scene_root&, uint64_t item_types, out)`,
  that walks the content library folders whose items match the mask
  (materials; textures and graph textures) into a caller-owned scratch
  vector cleared after the draw (the `m_material_candidates` discipline
  of the scene-close bug class). The value-to-text switch at
  `dependency_property_rows.cpp:551` shows the reference path.
- Host check. The library's validate is value-only, so the editor's write
  funnel `apply_item_property` (`property_set_operation.cpp`) checks an
  object value before applying it: the referenced item is hosted by the
  target's host, or the asset manager reports it cross-scene
  referenceable (`is_cross_scene_referenceable`). A failing check logs a
  warning naming both items and applies nothing, the way a rejected
  sealed write is reported.
- Asset userships (asset-manager plan R5.4): `Property_set_operation`
  and `Property_set_apply_operation` whose property is object-typed
  adopt an `Asset_reference` usership for every managed-asset-typed item
  in their before and after states at first execute, exactly as
  `Mesh_material_assign_operation::adopt_userships` does, and
  `collect_item_references` reports those items too, so
  `get_editor_references` and unload refusals see the undo history's
  pins.
- MCP (D13): `get_item_properties` reports an object value as its path
  string in `value` plus `reference_id` (the pointee's session
  `get_id()`, `null` when unset) and `reference_type` (its type name).
  `set_item_property` takes `value` (a name, resolved through the target
  item's host as D1 parses it, empty to clear) or `reference_id` (an
  integer, resolved by `find_item_in_scene_by_id`, which disambiguates
  same-named items). `scene.set_property` takes the same two forms.
- Serialization: object values ride their native glTF carriers (material
  index of a primitive, `textureInfo` of a material), never the
  `properties` extras: both users are bridged (D3, D4) and
  `item_local_properties_to_json` already skips bridged values
  (`gltf_item_flags.cpp:134`). When `doc/gltf-properties-extension-plan.md`
  step 0 lands, its `Property_flags::native_gltf` is set on all six
  registrations of this plan. `apply_item_local_property` parses object
  values through the D1 context overload so the legacy extras path and
  MCP share one parser.

### D3 - Material texture slots

- `Material` registers `base_color_texture`, `metallic_roughness_texture`,
  `normal_texture`, `occlusion_texture` and `emissive_texture` as
  `Property<Object_reference>` on `Material::property_owner_type()`, each
  through `register_member` (D1) over
  `data.texture_samplers.<slot>.texture_reference`, so the `shared_ptr`
  traits do the `Dependency_object` / `Texture_reference` conversion and
  supply the validate. Member-backed (D18) because the per-frame
  readers (`Material_buffer::gather_texture`, `Shader_key::derive`) keep
  reading the member with no variant access, the `Material_create_info`
  designated initializers at every construction site keep working, and
  the `Material_data` snapshot / `Material_change_operation` keeps
  serving the sampler and transform fields of the same slot.
- The traits' cast to `erhe::graphics::Texture_reference` needs the complete class,
  so `erhe::primitive` links `erhe::graphics` (a new DAG edge:
  `erhe::graphics` depends on `erhe::item` only, and `material.hpp`
  already names `erhe::graphics::Texture_reference` and `Sampler`).
  `reference_item_types = Item_type::texture | Item_type::graph_texture`,
  the two folders `Content_library::texture_reference_combo` lists;
  `Rendergraph_node` also implements `Texture_reference` and passes
  validate, matching what the rendertarget path assigns today.
- Flags: `serialize` (the R5.8 dirty mark in `on_item_property_changed`)
  and `affects_shader_variant`: a bound slot selects the texture-using
  variant and a normal texture's two-component flag travels on the
  texture (`shader_key.cpp:100`), so the D11 hook rebuilds the draw lists
  on the change instead of the draw lists discovering it through the R12
  per-flush material hash compare. `Property_ui`: group `Textures`,
  labels `Base Color Texture` and so on, `visible_when` for the PBR slots
  mirrors the existing PBR rows (unlit materials hide them).
- Accessors: `get_base_color_texture() -> std::shared_ptr<Texture_reference>`
  / `set_base_color_texture(...)` and the four others, on the store.
- Writers of a live material go through the property so D19 observers
  (the D21 thumbnails), the D11 hook and dirty tracking fire:
  `mcp_server_material.cpp:367`, `rendertarget_mesh.cpp:166`,
  `texture_material_output_node.cpp:471,480`, `texture_output_node.cpp:242`,
  and the Properties window's texture combo rows, which are deleted in
  favor of the generic rows (the Transform and Sampler rows under each
  slot stay hand-written, shown while the slot's property is non-null).
  Construction-time fills stay member writes under the D18 caveat:
  `Material_create_info.data` and `Gltf_image_residency::bind_material_textures`
  (the material has no consumers yet when the import binds).
- D21 update: a slot texture edit now refreshes the material thumbnails
  through the any-property observer; sampler and transform edits still do
  not.

### D4 - Mesh primitive material through property sub-objects

`Mesh_primitive` is a value in `Mesh::m_primitives` (134 call sites read
`get_primitives()`), not an item: no id, no name, no host, no `shared_ptr`.
The property lives on the primitive, and the editor addresses the
primitive through its mesh and index.

- `Mesh_primitive` derives from `erhe::property::Dependency_object` with
  a `property_owner_type()` function-local static allocated under the
  root and named `Mesh_primitive`, a `get_property_owner_type()`
  override, and a static `material_property` (`Property<Object_reference>`)
  registered with `register_member` over `material` (the traits validate
  the pointee as an `erhe::primitive::Material`; `after_set` is the owner
  notify below), `reference_item_types = Item_type::material`,
  label `Material`, flags `serialize`. The `Property_ui` reuses the
  Properties picker's behavior: no clear button (`show_clear_button` is a
  new `Property_ui` bool, false here; a primitive keeps a material).
- Owner link: `Mesh_primitive` carries `Mesh* owner{nullptr}` and
  `std::size_t index{0}`, stamped by one private
  `Mesh::stamp_primitive_owners()` called from `set_primitives`,
  `add_primitive`, `clear_primitives`, the clone constructor and the move
  constructor / assignment. The registration's `after_set` hook calls
  `owner->notify_primitive_material_changed()` (private,
  the body of today's `set_primitive_material` after the store: the
  `weak_from_this` lock and `Scene_host::on_mesh_material_changed`).
  `Mesh::set_primitive_material` becomes `m_primitives[i].set_value(
  material_property, Object_reference{material})`: still the one writer
  (draw list material set plan R4), the early-out on an unchanged value
  is the library's own (R4 of the design record), and every writer -
  `Mesh_material_assign_operation`, the generic row, MCP,
  `scene.set_property` - reaches the same callback.
- Storage rule, stated because `std::vector` reallocation copy-constructs
  a `Dependency_object`, which drops observers, dependents and pending
  batches (D10): a `Mesh_primitive` registers bridged properties only (no
  entries to lose) and nothing subscribes an observer or an expression to
  a primitive. The stamp keeps `owner` / `index` correct across
  reallocation; `Mesh::get_primitives()` stays `const`, so no caller can
  reach `set_value` on a primitive without going through `Mesh`.
- Sub-object addressing (editor-neutral, on `Item_base`): three virtuals
  with empty defaults,
  `get_property_sub_object_count() const -> std::size_t`,
  `get_property_sub_object(std::size_t) -> Dependency_object*` and
  `get_property_sub_object_label(std::size_t) const -> std::string_view`;
  `Mesh` returns its primitive count, `&m_primitives[index]` and the
  primitive's name (`Primitive::get_name()`, falling back to
  `Primitive N`). A sub-object index is stable for the mesh's primitive
  list and is the same index `Mesh_material_assign_operation::Entry`
  records.
- Editor: `Property_set_operation` gains an optional sub-object index
  (`std::optional<std::size_t>`) and applies to
  `item->get_property_sub_object(index)` when set (an index past the count
  at apply time logs a warning and applies nothing, the sealed-write
  shape). `Dependency_property_rows::add_rows` gains a sub-object
  variant that lists the sub-object's owner type properties under the
  caller's group; `Properties::mesh_properties` calls it inside each
  existing `Primitive N` group in place of the hand-written material
  picker (the developer-mode slot rows stay). Single-item selection only,
  as the mesh section is today. `Mesh_material_assign_operation` stays
  the operation of the paint tool, the drag-drop targets and the
  item-tree drop: it already batches primitives, dedups unchanged ones and
  adopts userships, and it writes through the same funnel.
- MCP: `get_item_properties` gains `sub_objects`, an array of
  `{index, label, properties: [...]}` in the same entry shape as the
  item's own list; `set_item_property` and `scene.set_property` take an
  optional integer `sub_object`. Sealing (D24) is the mesh's:
  `Mesh_primitive` is not an item, so the editor funnel checks
  `item->is_sealed()` before applying to a sub-object.
- glTF: the material index is native; nothing changes in the exporter or
  importer. `ERHE_mesh_properties` (the extension plan) never carries a
  primitive's material.

## Steps

Each step: edit, build the primary tree (`scripts\build_ninja_win_vulkan.bat
editor` plus the affected `erhe_*_tests` targets), self-review the diff,
commit. Steps 1-3 are independent of 4-5; 4-5 need 1 and 2 (the editor
row for the object type).

### 1. Library: `Object_reference` (D1)

- `property_value.hpp`: the struct, the variant alternative,
  `Property_type::object`, `c_str`, `zero_value`, the concept,
  `make_value` / `get_as`.
- `dependency_object.hpp/.cpp`: `get_reference_path()` virtual.
- `property_string.hpp/.cpp`: `to_string` case; the context overload of
  `parse_value`; the plain overload returns `nullopt` for the object type.
- `expression.cpp`: the object type joins every `Property_type::string`
  exclusion (`:122`, `:207`, `:361`, `:394`).
- `property_metadata.hpp`: `Property_ui::reference_item_types`,
  `Property_ui::show_clear_button{true}`.
- `dependency_property.hpp`: `Member_value_traits` (identity and
  `shared_ptr` specializations) and `Property<T>::register_member`.
- Every other `switch (Property_type)` in the library (registry listing,
  validate type check, `Property_set` diff) gets its case.
- `item.hpp/.cpp`: `Item_base::get_reference_path()` returns the name.
- Tests, `src/erhe/property/test/test_object_reference.cpp`: store and
  read back, pointer-identity equality and `Property_set::diff`, default
  is null, validate rejects a wrong class, `to_string` through the
  virtual, `parse_value` with a context whose `resolve_expression_object`
  names test objects (found, not found, empty), copy shares the pointee,
  `set_expression` on an object property is rejected, an expression
  referencing an object property reports the type error;
  `register_member` over a float member and over a `shared_ptr` member of
  the test object (read agrees with the member, `set` writes it and runs
  `after_set`, a wrong-class pointee is rejected).

### 2. Material texture properties (D3)

- `src/erhe/primitive/CMakeLists.txt`: link `erhe::graphics`.
- `material.hpp/.cpp`: the five `register_member` registrations,
  accessors, `Property_ui` groups and `visible_when`.
- Writers: the five live-material sites listed in D3 call the setters.
- Test, `src/erhe/primitive/test/test_material_textures.cpp`: bridged
  read agrees with the member after a member write and after a property
  write; a slot change fires an any-property observer once; validate
  rejects a `Material` as a texture; `operator==` of two materials
  differs on the slot alone.

### 3. Editor and MCP for object values (D2)

- `dependency_property_rows.cpp`: the widget case, the candidates helper,
  the text case, `show_clear_button`.
- `property_set_operation.cpp`: host check in `apply_item_property`,
  usership adoption, `collect_item_references`.
- `mcp_server_properties.cpp`: `reference_id` / `reference_type` in the
  listing; `value` / `reference_id` on set; `editor.cpp`
  `scene.set_property` likewise.
- `properties.cpp`: delete the texture combo rows of
  `material_properties`; the Transform / Sampler rows read the slot's
  property to decide visibility.
- `thumbnails.cpp` needs no change (the observer already exists); note
  the D21 behavior change in the design record (step 6).

### 4. `Mesh_primitive` as a property sub-object (D4, library side)

- `mesh.hpp/.cpp`: the base class, owner type, `material_property`,
  owner stamp, `notify_primitive_material_changed`,
  `set_primitive_material` through the property, the three `Item_base`
  virtuals (`item.hpp` defaults, `Mesh` overrides).
- Test, `src/erhe/scene/test/test_mesh_primitive_material.cpp`: a
  `Scene_host` test double counts `on_mesh_material_changed`;
  `set_primitive_material` with a new material fires once and with the
  same material fires zero times; the property reads the member; the
  sub-object virtuals agree with `get_primitives()`; `set_primitives`,
  clone and move re-stamp owners (assign through the property on the
  copy and observe the copy's mesh notified, not the source's).

### 5. Editor and MCP for sub-objects (D4, editor side)

- `property_set_operation.hpp/.cpp`: the sub-object index and the sealed
  check.
- `dependency_property_rows.hpp/.cpp`: the sub-object `add_rows`.
- `properties.cpp`: the `Primitive N` groups call it; delete the
  hand-written material picker and `m_material_candidates`.
- `mcp_server_properties.cpp` and `editor.cpp`: `sub_objects` /
  `sub_object`.

### 6. Documentation

- `doc/property-system.md`: D28 (object references, from D1 and D2), D29
  (sub-objects, from D4), section 4.1 gains the texture properties and
  `Material_data` narrows in the text, a new section 4.x for
  `Mesh_primitive`, the D21 thumbnail sentence, D22 exclusion list, D14
  serialization note (native carriers), `Property_ui` fields in D4.
- `src/erhe/property/notes.md`: the value type, the virtual, the parse
  overload.
- `src/erhe/item/notes.md`, `src/erhe/scene/notes.md`,
  `src/erhe/primitive/notes.md`: the new API.
- `doc/mcp_api_guidelines.md` or the MCP tool descriptions: the new
  arguments.
- `doc/gltf-properties-extension-plan.md` step 0: add the six
  registrations to the `native_gltf` audit list.
- Delete this document once the above carries everything.

## Verification (once, at the end)

- Builds: `build_ninja_win_vulkan` every step; the Windows opengl and
  vulkan VS Debug trees at the end; `scripts\configure_ninja_win_clang.bat`
  after the CMake link change (the clangd database).
- Unit tests: `erhe_property_tests`, `erhe_item_tests`,
  `erhe_primitive_tests`, `erhe_scene_tests`, serially.
- Editor over MCP (`scripts/mcp_call.py`, `ERHE_MCP_PORT=3743`, headless
  build):
  1. `get_item_properties` on a material lists the five slots with
     `source: local`, `value` empty, `reference_id null`.
  2. Import a textured glTF (`res/editor/assets/Sponza` or
     `ABeautifulGame.glb`); a material's `base_color_texture` reports the
     texture's name and id.
  3. `set_item_property` `base_color_texture` by name and by
     `reference_id` on a default-scene material; `capture_screenshot`
     shows the texture on a mesh using it; `undo` clears it and the
     screenshot reverts; `get_undo_redo_stack` shows one
     `Property_set_operation` per write.
  4. `get_item_properties` on a mesh lists `sub_objects` with one entry
     per primitive; `set_item_property` with `sub_object 0` and a
     material name reassigns; `undo` restores; `get_draw_lists` shows the
     material slot change.
  5. Cross-scene reference: two scenes open, set a material texture to the
     other scene's texture by `reference_id`; the write is refused with
     the host warning in `logs/log.txt`.
  6. Close the scene of step 2 and grep `logs/log.txt` for
     `scene-close leak`: the count of intentionally pinned items matches
     the pre-change run.
- Interactive (user): drag a texture from the content library onto the
  `Base Color Texture` row and onto a `Primitive N` `Material` row; the
  hotbar thumbnail of the edited material refreshes; the Properties
  material picker behaves as before (no clear button).

## Rejected alternatives

- A weak reference in `Object_reference`: changes the lifetime the members
  give today (a material would stop keeping its textures alive) and the
  asset manager's pinning model is built on the strong holds.
- Registering the primitive material on `Mesh` itself: the registry is
  static per owner type, so a property cannot carry an index; a single
  `material` on the mesh loses the per-primitive materials of every glTF
  mesh.
- Making `Mesh_primitive` an `Item_base`: gives it an id, a name, a host
  and a `shared_ptr` at the cost of hierarchy, selection, serialization
  and content-library changes far larger than the property it needs.
- `std::vector<std::unique_ptr<Mesh_primitive>>` for pointer stability:
  134 `get_primitives()` call sites; the storage rule in D4 covers what
  stability would buy.
- A `Texture_reference` interface moved below `erhe::primitive` to avoid
  the link: the class already belongs to `erhe::graphics` in every other
  respect and the link edge is a valid DAG edge.
