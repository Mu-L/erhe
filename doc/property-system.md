# Property system (WPF dependency-property port)

Status: implemented. The library (`erhe::property`), the `Item_base`
integration, the editor operation / generic rows / MCP tools / startup
command, the `Material`, `Node`, `Light` and `Camera` migrations, observer
users, expressions and bindings, inherited flags, sealing, the style layer,
computed properties, the owner type id keyed registry, the geometry
and texture graph node migrations, object references (the material
texture slots) and property sub-objects (the mesh primitives with their
material) are all in and verified. This is a live document; section 6
holds the remaining work.

Document roles. This document is the design record: goal, requirements,
the design decisions with their WPF mapping and rationale, the item
migrations, the editor / MCP / glTF integration, future work and the
verification workflow. `src/erhe/property/notes.md` is the library
reference: the current types and semantics of `erhe::property` as one
type-by-type summary, kept in sync with the code and free of rationale.
Look a mechanism up there; read here for why it is shaped that way and
how the rest of the codebase uses it.

Reference: the WPF property system, `https://github.com/dotnet/wpf` at the
commit named in `doc/property-system-wpf-comparison.md`, files under
`src/Microsoft.DotNet.Wpf/src/WindowsBase/System/Windows/`:
`DependencyProperty.cs` (registration, global index, owner metadata),
`PropertyMetadata.cs` (default value, changed / coerce callbacks),
`DependencyObject.cs` (`GetValue` / `SetValue` / `ClearValue` / `CoerceValue` /
`InvalidateProperty` / `ReadLocalValue` / `OnPropertyChanged`),
`EffectiveValueEntry.cs` (sparse per-object value store, base source, animated
and coerced modifiers), `DependencyPropertyKey.cs` (read-only properties), and
in `PresentationFramework/System/Windows/`: `FrameworkPropertyMetadata.cs`
(`Inherits`) and `TreeWalkHelper.cs` (inherited-value invalidation on tree
change and on inheritable property change).

## 1. Goal

A C++ library `erhe::property` that gives every erhe item (`Item_base` and
everything derived from it: `Node`, `Material`, `Light`, `Mesh`, ...) a WPF-style
property store, and the migration of items onto it so the
editor can edit, undo and inspect item state through one generic
mechanism instead of one hand-written path per field.

Value types in scope: `bool`, `int`, `float`, `glm::vec2`, `glm::vec3`,
`glm::vec4`, `glm::ivec2`, `glm::ivec3`, `glm::ivec4`, `glm::quat`,
`std::string`, C++ enumerations (any `enum class` with an enumerator
table, see D2a), and references to other objects (D28).

## 2. Requirements

- R1 Registration. A property is registered once, statically, with a name, a
  value type, the owner type id of the registering class (D27) and default
  metadata, and gets a stable global index. Registration and lookup by
  (owner type, name), by (object, name) through the owner type chain, and
  by index are available at runtime for the editor and MCP.
- R2 Typed access. Callers read and write through a typed handle
  (`Property<T>`), so a `glm::vec3` property cannot be written with a `float`.
  An untyped path (`Property_value` variant) exists for generic code: the
  Properties window, undo, serialization, MCP.
- R3 Precedence. The effective value of a property on an object is, in
  decreasing precedence: coerced, local, style (D25), inherited, default.
  Each layer can be present or absent independently; clearing a layer
  exposes the next one.
  The layer order leaves room for an animated layer between coerced and
  local (future work, section 6).
- R4 Callbacks. A property has an optional validate callback (value only, no
  object), and its metadata has an optional coerce callback and an optional
  property-changed callback, both receiving the object. The object also gets a
  virtual `on_property_changed`. Changed notification fires only when the
  effective value actually changes.
- R5 Per-owner metadata. A derived item type can override the metadata
  (default value, callbacks, inherits flag) of a property registered by a base
  type, as WPF `OverrideMetadata` / `AddOwner` do.
- R6 Read-only. A property can be registered read-only: writes go through a
  key object the owner holds privately; the public handle reads only.
- R7 Attached. A property can be registered by a type other than the type of
  the objects it is set on (WPF attached properties), so the editor and tools
  can store per-item state without the item class knowing.
- R8 Inheritance. A property flagged `inherits` reads the closest ancestor's
  value through `erhe::Hierarchy` when the object has no local value;
  descendants get changed callbacks when the ancestor's value or the tree
  structure changes.
- R9 Enumerations. An enumeration property carries its enumerator table
  (label and integer value per enumerator) in the registry, so generic code
  (Properties window combo, MCP, serialization) can present and parse it by
  name, and typed code reads and writes the C++ enum directly.
- R10 Enumeration. The local values of an object can be enumerated (WPF
  `LocalValueEnumerator`), and the properties registered for an item type can
  be enumerated in registration order.
- R11 Copy semantics. Copying or cloning an object copies its local values
  (with their coerced values, which derive from them). Inherited state,
  observers and pending batches are not copied.
- R12 Threading. Property values are item state and are guarded by the same
  item-host mutex as the rest of the item (`Item_host_lock_guard`). The
  registry's writes end with single-threaded early startup - C++ static
  initialization plus descriptor-driven registrations run from
  `run_editor()` before the first thread other than main exists (section
  4.5) - and reads after that are lock-free.
- R13 Undo. Every property write made from the editor UI is an `Operation` on
  the operation stack and restores the exact previous state on undo,
  including "no local value" (a clear), not just the previous effective value.
- R14 Performance gate for `Node`. After the `Node` transform migration, the
  per-frame physics transform writeback, animation playback and
  `Scene::update_node_transforms` on Sponza and VirtualCity stay within 10%
  of the pre-migration frame time.
  This held by construction: the transform properties are bridged (D18), no
  per-frame path changed, and `get_transform_update_stats` matched the
  pre-migration run on the default scene.
- R15 Consequence flags. Property metadata states what a change affects
  (transform, draw-list partitioning, shader variant, serialization), so
  generic code decides what to rebuild from the flags instead of from a
  per-field check (WPF `FrameworkPropertyMetadataOptions`, `AffectsRender`
  and friends).
- R16 Observers. Code outside the object can subscribe to changes of one
  property on one object and unsubscribe with a token (WPF
  `DependencyPropertyDescriptor.AddValueChanged`), so a consumer reacts to
  exactly the property it reads.
- R17 UI metadata. Property metadata carries what the Properties window
  needs to draw the row as well as the hand-written rows do today: range,
  step, presentation (color, angle in degrees, slider), group, tooltip and
  developer-only visibility.
- R18 String conversion. Every `Property_type` converts to and from a string
  through one function pair (WPF `TypeConverter`), shared by MCP, command
  scripts and serialization.
- R19 Value bags. A set of (property, value) pairs is a first-class value
  (`Property_set`) that can be read from an object, applied to an object,
  compared, and carried by the clipboard and by multi-selection editing.

## 3. Design decisions

- D1 Library placement. Library `src/erhe/property` (target
  `erhe_property`, alias `erhe::property`, namespace `erhe::property`),
  header prefix `erhe_property/`. Dependencies: `glm::glm-header-only`,
  `erhe::utility`, `erhe::profile` (public); `erhe::log`, `erhe::verify`,
  `fmt`, `tinyexpr` (private). `erhe::item` links it publicly and `Item_base`
  derives from `Dependency_object`. The library holds no scene, item or
  hierarchy knowledge; tree walking reaches it through one virtual on the
  object (D8).
- D2 Value representation.
  `Property_value = std::variant<bool, int, float, glm::vec2, glm::vec3,
  glm::vec4, glm::quat, std::string, Enum_value, glm::ivec2, glm::ivec3,
  glm::ivec4, Object_reference>` (the integer vectors and the object
  reference appended so the variant indices of the earlier types are
  stable; `Object_reference` is D28) with a matching
  `enum class Property_type : uint8_t` whose enumerators are the variant
  indices. `Property<T>` is valid for `T` in that list or for any C++
  enumeration type (constrained with a concept); an enumeration `T` maps to
  `Enum_value` per D2a.
- D2a Enumerations. `Enum_value` is `struct { int32_t value; }`, kept
  distinct from `int` so generic code can tell a combo from a drag-int.
  `Enum_info` is an immutable table `{ std::string_view type_name;
  std::span<const Enum_entry> entries; }` with `Enum_entry { std::string_view
  label; int32_t value; }`, plus `label_for(value)` and `value_for(label)`.
  A `Dependency_property` of type `Enum_value` holds a `const Enum_info*`;
  registration through `Property<E>::register_property` for an enumeration
  `E` requires the caller to pass the `Enum_info` (one `static const`
  table per enumeration, next to the enumeration's existing `c_str`
  function so the labels have one source). Validate for an enumeration
  property rejects any integer that is not in its table. `Property<E>::
  get_value` returns `E` by `static_cast` from the stored integer; `set_value`
  stores `static_cast<int32_t>(value)`.
- D3 Registry. `Dependency_property` is an immutable registration record:
  global index (`uint16_t`, assigned sequentially as WPF `GlobalIndex`),
  name, `Property_type`, owner type (the `Owner_type` id of the registering
  class, D27), flags (read-only, attached), validate callback, default
  `Property_metadata`, and a small vector of (owner type,
  `Property_metadata`) overrides. `Property_registry` is a function-local
  static (safe against static-initialization order) holding records by
  index, an index by (owner type, name), a per-owner-type list in
  registration order, and the owner type id table. Registration happens
  from static members of the owning class:

  ```cpp
  // material.hpp
  static const erhe::property::Property<glm::vec3> base_color_property;
  // material.cpp
  const erhe::property::Property<glm::vec3> Material::base_color_property =
      erhe::property::Property<glm::vec3>::register_property(
          "base_color", Material::property_owner_type(),
          erhe::property::Property_metadata{ .default_value = glm::vec3{1.0f} }
      );
  ```

  `Property<T>` is a copyable handle wrapping `const Dependency_property*`.
  `Property_key<T>` (R6) is the same handle plus write permission; the owner
  keeps it private and exposes a `Property<T>` obtained from it.
- D4 Metadata. `Property_metadata` holds `default_value` (a
  `Property_value`), `property_changed` (`void(Dependency_object&, const
  Property_changed_args&)`), `coerce` (`Property_value(const
  Dependency_object&, const Property_value&)`), `bool inherits`, a
  `Property_flags` bitmask (R15: `affects_transform`,
  `affects_draw_list_partition`, `affects_shader_variant`, `serialize`;
  `serialize` is set by default) and a `Property_ui` block (R17:
  `std::optional<float> min, max, step`, `enum class Presentation { plain,
  color, angle_degrees, slider }`, `std::string_view group, tooltip, label`,
  `bool developer_only`, `Visible_when visible_when` - a predicate on the
  inspected object). The flags and the UI block are data only; the editor
  reads them (D11, D12) and the library never acts on them. Metadata
  resolution for (property, object) walks the object's owner type chain
  (D27) from its own id to the root and returns the first override
  registered for an id on it, else the default metadata, so an override on
  a base class applies to every derived class and a derived-class override
  wins. Override lists are short (usually empty), so each level is a
  linear scan with no cache.
- D5 Effective value store. `Dependency_object` owns
  `std::vector<Effective_value_entry>` sorted by property index and searched
  by binary search (WPF `EffectiveValueEntry` array). An entry holds: index,
  the local `Property_value`, and `std::optional<Property_value> coerced`
  set only when the coerce callback changed the local value (the future
  animated layer of section 6 is another optional in the entry). An entry
  exists only for properties with a local value; reading a property with no
  entry returns the inherited or default value without allocating.
- D6 Public object API (mirrors WPF names in erhe casing):
  - `get_value(property) -> T` / `get_value(const Dependency_property&) ->
    Property_value` (effective value, R3).
  - `set_value(property, value)` (local layer; validate, then coerce, then
    store, then notify), `clear_value(property)` (drops the local layer).
  - `read_local_value(property) -> std::optional<T>` (R10, R13).
  - `coerce_value(property)` re-runs the coerce callback against the current
    local value and notifies if the effective value changed (WPF
    `CoerceValue`); a property without a local value is coerced on every
    read and has nothing to re-run.
  - `get_value_source(property) -> Value_source`, `is_coerced(property)` for
    UI indicators; `has_local_value(property)`.
  - `for_each_local_value(callback)` (R10).
  - `add_observer(property, callback) -> Observer_token` (D15).
  - `capture_inheritance_snapshot()` / `apply_inheritance_snapshot()` (D8).
  - `virtual void on_property_changed(const Property_changed_args&)`.
  `Property_changed_args` carries the property, old and new effective
  values, and old and new `Value_source`.
- D7 Validate and coerce semantics. Validate (type check, enumeration
  table, callback) runs on the incoming value before any store; a failing
  validate logs an error and drops the write, so a bad value from a file,
  MCP or a script never reaches the store and never aborts the editor.
  Coerce runs on the local value at write time and its result is stored as
  the coerced value; a coerce that returns its input stores none. A property
  without a local value is coerced on every read.
- D8 Inheritance mechanics. `Dependency_object` has
  `virtual auto get_inheritance_parent() const -> const Dependency_object*`
  returning `nullptr` and `virtual void for_each_inheritance_child(const
  std::function<void(Dependency_object&)>&)` doing nothing. `Hierarchy`
  overrides both with its parent and children. An inherits-flagged property
  with no local value on an object reads the effective value from the
  closest ancestor that has one on every read (no cached copy: the walk is
  as deep as the tree and a cache would need tree-change invalidation).
  `set_value` / `clear_value` on an inherits-flagged property notifies every
  descendant that has no local value for it, stopping the walk at a
  descendant that has one (WPF
  `TreeWalkHelper.InvalidateOnInheritablePropertyChange`). A tree change
  captures the subtree's inherited values before the move
  (`capture_inheritance_snapshot`) and notifies the differences after it
  (`apply_inheritance_snapshot`; WPF `InvalidateOnTreeChange`);
  `Hierarchy::set_parent` does both.
- D9 Change batching. `Dependency_object::Change_batch` is an RAII guard:
  while one is alive on an object, changed notifications are queued and
  delivered once when the outermost guard ends, deduplicated per property
  with the value before the batch and the value after. `Material::
  set_values` and `Property_set::apply` use it.
- D10 Copy. `Dependency_object`'s copy constructor and assignment copy the
  entries and nothing else (R11). `Item_base` and `Hierarchy` clone paths
  keep working unchanged because they already copy-construct the base.
- D11 Editor undo. `Property_set_operation` in `src/editor/operations/`
  records the item (`std::shared_ptr<Item_base>`), the property, `before`
  as `std::optional<Local_state>` (the local value or expression text, or
  nullopt for "no local value") and `after` likewise. `execute` applies
  `after` (set or clear), `undo` applies `before`. It calls one editor hook,
  `App_context::on_item_property_changed(item, property)`, after each apply,
  which reads the property's `Property_flags` (D4) and runs the matching
  editor consequence: `affects_draw_list_partition` rebuilds the draw lists,
  `affects_shader_variant` re-derives the material's shader variant. The
  hook is the only place that maps flags to editor actions. The operation
  carries an optional sub-object index (D29): the target is then
  `item->get_property_sub_object(index)`, the item stays the one the
  operation names and seals against, and a sub-object that no longer
  exists at apply time is a logged no-op.
  `Material_change_operation` stays for the fields that remain in
  `Material_data` (texture samplers). A `Property_set_apply_operation`
  applies a `Property_set` (D17) to a list of items and records one before
  bag per item, for paste and multi-selection edits.
- D12 Editor UI. `Property_editor` has a generic
  `dependency_properties(item)` section that lists the registered properties
  of the item's type in registration order and draws one widget per
  `Property_type`: checkbox, drag int, drag float, drag float2/3/4, quaternion
  as Euler-degree drag with a raw x/y/z/w readout, input text, a combo
  filled from the property's `Enum_info` labels, and for an object
  property the `item_reference_imgui` field (D28). Every such row's
  label is tinted light blue, so a hand-written row of the same window
  (state not yet a registered property) is told apart at a glance. The
  `Property_ui` block
  (D4) selects the widget variant and its limits: `color` draws a color
  edit for vec3 / vec4, `angle_degrees` converts radians to degrees for
  display, `slider` draws a slider within `min` / `max`, `group` collapses
  rows under a header, `tooltip` is the hover text, `label` replaces the
  property name as the row label, `developer_only` rows show only in the
  editor's developer mode, and a row with `visible_when` is listed only
  while the predicate holds for every selected item (Material hides its
  PBR rows for unlit materials, its anisotropy rows for BxDF models
  without anisotropy, and alpha cutoff outside the alpha-test blending
  mode). Each row's tooltip shows
  its `Value_source` (local, inherited, default), its default value and a
  quaternion's raw x y z w; the label carries a `*` prefix when a local
  value differs from the default; the context menu offers "Reset to default"
  (a clear). Drag edits follow the existing
  begin-edit-snapshot / commit-on-release pattern used by material editing
  (`m_material_state`), producing exactly one `Property_set_operation` per
  completed drag. With several items selected the section shows the
  properties common to all selected types, marks a row whose values differ
  across the items as mixed, and a commit writes the edited property to
  every item through one `Compound_operation` of per-item
  `Property_set_operation`s (D11). The
  section's context menu offers Copy Properties (reads a `Property_set` of
  the item's local values into an editor clipboard) and Paste Properties
  (applies it to the selection through `Property_set_apply_operation`,
  skipping properties the target type does not have).
- D13 MCP. Two tools in `src/editor/mcp/`: `get_item_properties(item)` lists
  (name, type, effective value, source, local value) and
  `set_item_property(item, name, value)` writes through
  `Property_set_operation`. Values travel as strings through D16, so
  enumeration values travel as their labels; an object value (D28)
  travels as the referenced item's name with `reference_id` (its session
  id) and `reference_type` alongside, and `set_item_property` also takes
  `reference_id`. The listing has `sub_objects` and the write takes
  `sub_object` (D29). A `scene.set_property` command
  (`config/editor/commands.json`, `doc/command_script.md`) with args
  `item`, `property`, `value` (and `sub_object`) uses the same conversion,
  so startup scripts can author properties.
- D14 Serialization. glTF import and export read and write the
  fields they natively map (`Node` TRS, `Material` PBR fields) through the
  typed accessors; the file format did not change except the D23 extras.
  Writing local values of non-glTF properties to extras
  (`item_local_properties_to_json` / `apply_item_local_property` in
  `erhe_gltf/gltf_item_flags.hpp`, honoring the `serialize` flag) is done
  for nodes, meshes (D23, `ERHE_node` `properties` / `mesh_properties`),
  lights (`ERHE_light` `properties`) and cameras (`ERHE_camera`
  `properties`); materials still export field-by-field through their
  native glTF fields plus the `ERHE_material` extension (section 6). An
  object value (D28) rides its native glTF carrier - a primitive's
  material index, a material's `textureInfo` - and never the extras:
  both users are member-backed (D18), which the extras writer skips.
- D15 Observers (R16). `Dependency_object::add_observer(property, callback)
  -> Observer_token` and `remove_observer(Observer_token)`; the token is a
  move-only RAII object that unsubscribes on destruction, and an object
  being destroyed invalidates its outstanding tokens (they become no-ops).
  Observers are stored in a per-object vector allocated on first
  subscription, and are invoked after the metadata callback and the virtual
  hook, with the same `Property_changed_args`, under the same batch rules as
  D9. The users are settled in D21.
- D16 String conversion (R18). `to_string(const Property_value&) ->
  std::string` and `parse_value(Property_type, std::string_view, const
  Enum_info*) -> std::optional<Property_value>` in
  `erhe_property/property_string.hpp`. Vectors and quaternions are
  space-separated components, booleans are `true` / `false`, enumerations are
  their `Enum_info` labels, strings are verbatim. Both directions round-trip
  every value exactly for the non-float types and to the shortest float
  representation that round-trips for `float` (`std::to_chars`). An
  object reference (D28) needs the referencing object to parse, so a
  second overload `parse_value(const Dependency_object& context,
  property, text)` resolves the text through
  `context.resolve_expression_object` and delegates every other type to
  the plain one.
- D17 Value bags (R19). `Property_set` in `erhe_property/property_set.hpp`
  is a vector of (`const Dependency_property*`, `Property_value`) sorted by
  property index with `read_local_values(const Dependency_object&) ->
  Property_set` (R10 enumeration), `apply(Dependency_object&)` (one batch,
  D9), `diff(const Property_set&, const Property_set&) -> Property_set` and
  `operator==`. `Material::operator==` (section 4.1) compares the bags read
  from both materials.
- D18 Bridged storage. `Property_metadata::bridge` (`Property_bridge`
  with `get` / `set` callbacks) stores a property outside the entry store:
  the object's own member is the local value, `set_value` / `clear_value`
  go through `set` (clear writes the default), the property always reports
  `Value_source::local`, never inherits, is listed by
  `for_each_local_value` and `Property_set::read_local_values`, and is
  coerced on every read. For state that already has an engineered
  representation the object must keep. `Node`'s transform is the case
  (section 4.2): `Trs_transform` defers matrix decomposition and
  `set_parent_from_node` copies TRS components without a matrix round trip
  so rotation survives near-zero scale, and the physics writeback runs that
  path per body per frame; moving the components into the entry store would
  force decomposition on every matrix write and change the world matrix of
  glTF matrix nodes to compose(decompose(M)).
  `Property<T>::register_member(name, owner_type, member, metadata,
  after_set)` registers a bridged property without spelling the bridge
  out: the library builds `get` / `set` from a pointer-to-member or an
  accessor lambda (`[](auto& o) -> auto& { return o.a.b.c; }` for a
  nested member), `set` writes the member and then runs the optional
  `after_set(owner)` hook - the consequence a hand-written bridge ran
  inline - and a write of the value the member already holds does
  neither (R4). `Member_value_traits<Member>` converts between the member
  type and the stored value: the identity for every storable type, and
  for a `std::shared_ptr<U>` member the cast to and from an
  `Object_reference` (D28) plus a validate that rejects a pointee that is
  not a `U`. An enumeration member registers through the overload that
  takes its `Enum_info`, as `register_property` does. The material texture
  slots (section 4.1), the mesh primitive material (section 4.9), the
  Camera projection fields (section 4.4) and the graph node parameters
  (section 4.5) register this way.

- D19 Item-side consequences through metadata callbacks. When a change of
  a property has a consequence inside the item library itself (not in the
  editor, which D11 covers), the owner registers the property with a
  `property_changed` callback (D4) that runs the consequence, so every
  writer - typed accessor, Properties row, `Property_set_operation`, MCP,
  `scene.set_property`, glTF import, `Property_set::apply` - reaches it
  and no call site has to remember a manual "notify" call. `Light` is the
  first user (section 4.3): the light-set re-resolve
  (`Scene_host::on_light_changed`) is the changed callback of every
  `Light` property, and `Light::notify_changed()` is not public API.
  A callback only fires on an effective change (R4), so a write of the
  current value costs nothing downstream, and D9 batching still collapses
  a multi-property write into one callback per property.
- D20 Logarithmic sliders. `Property_ui` has `bool logarithmic` (data
  only, like the rest of the block); the generic float slider row draws
  with `ImGuiSliderFlags_Logarithmic` and a `%.4f` no-rounding format when
  it is set. The light range / intensity and camera near / far rows are
  logarithmic and would lose usability as linear sliders over 0..20000.
- D21 Observer users. Of the three D15 candidates, one is an observer
  user; the other two are settled without observers:
  - `Thumbnails` (the material and brush thumbnails of the Hotbar and the
    Inventory window). A slot was rendered once when claimed and again
    only while hovered, so editing a material left its thumbnails stale
    until the next hover or eviction. Now a slot subscribes to the item
    it shows when it is claimed, with an any-property observer (below),
    and the callback only marks the slot stale. `Thumbnails::draw` of a
    stale slot re-queues the render callback through the same deferred
    path the hover re-render uses (stored in the slot, run from
    `Thumbnails::update` at the start of the next frame), so only
    thumbnails that are drawn re-render, at most once per frame, and a
    hidden one re-renders when it is next drawn. The token lives in the
    slot and is replaced when the slot is reclaimed for another item; a
    destroyed item deactivates it (D15). A texture slot's texture is a
    property (D28, section 4.1) and refreshes the thumbnail; the slot's
    sampler and transform fields are not properties and do not. The
    Properties window material preview keeps its per-frame render: it
    must also follow those sampler edits and the window size.
  - `Shadow_render_node`. The `cast_shadow` consequence already arrives
    through the D19 changed callback: `Scene_host::on_light_changed`
    invalidates the scene's `Light_set` and `Light_set::resolve` recomputes
    lazily. The node has no per-light state of its own; a direct observer
    would have to follow light registration to know which lights to
    subscribe to, which is what the `Scene_host` register / unregister
    hooks and the light set already do. No change.
  - Geometry graph `transform_from_node`. The driver's world transform is
    a derived cache, not a stored property (section 4.2); the direct
    `Trs_transform` writers (transform tool, physics writeback, animation)
    raise no property notification, and an ancestor move changes the
    driver's world transform with no property change on the driver at all.
    The world transform is a computed property since D26, and a computed
    property raises no observer notification, so `update_live` keeps
    polling (the transform serial is the cheap compare).
  - Library: `Dependency_object::add_observer(callback)` without a
    property subscribes to every property of the object, for a consumer
    that mirrors the whole object (a thumbnail, a preview, a serializer)
    rather than one value; same token, ordering and batch rules as D15.
    WPF has no equivalent (`DependencyPropertyDescriptor` is per
    property); it saves one token per registered property of the type.
- D22 Expressions and bindings (WPF `Expression`, `DependentList`,
  `SetCurrentValue`). A property of an object can be driven by a formula
  instead of a stored value; the formula sits at the local level of R3
  (WPF stores an expression as the local value) and its result goes
  through validate and coerce like a stored value. A binding is a formula
  that is one reference, so both share one implementation.
  - Text form. The formula string is the only authored representation
    (rows, MCP, `scene.set_property`, undo, future serialization). It is a
    comma-separated list of tinyexpr expressions, one per component of
    the target (`bool`, `int`, `float`, enumeration: one; `vec2` / `vec3`
    / `vec4`: two to four; `quat`: four, `x y z w`); a single expression
    on a vector target broadcasts. Top-level commas split components,
    commas inside parentheses are function arguments. A reference is
    written in braces: `{[object/]property[.component]}`, where `object`
    absent means the target's own object, `..` its inheritance parent,
    and anything else an item name resolved by the object
    (`resolve_expression_object`, below); `component` is `x`, `y`, `z`
    or `w`. A reference without a component in the formula of component
    `k` reads component `k` of the source (a scalar source reads its
    value), so `{../scale}` on a `vec3` target mirrors the parent's scale.
    `bool` sources read as `0` / `1`, enumerations as their integer.
    `string` and object (D28) properties are neither sources nor targets. Results convert
    per target type: `bool` is `value != 0`, `int` and enumeration round
    to nearest (an enumeration value outside its table is an error, the
    previous value stays), `float` truncates from `double`.
  - Store. `Effective_value_entry` carries `std::unique_ptr<Expression>`;
    `local` holds the last evaluated value, so every existing read path
    stays as it is. On a bridged property (D18) the entry carries only the
    expression and the evaluated value goes through `bridge.set`, so a
    node's `translation` can be driven the same way as any stored
    property. `get_value_source` reports `Value_source::expression`.
    `set_value` on a driven property replaces the formula with the value
    (WPF semantics); `set_current_value` writes the value and keeps the
    formula (WPF `SetCurrentValue`); `clear_value` drops both. A copy of
    the object (D10) copies the formula text unresolved; the copy resolves
    its references itself, lazily.
  - API. `set_expression(property, text) -> bool` compiles the text and
    installs it (a syntax error, a `string` target or a formula that
    references its own target property on the same object is rejected the
    way a failed validate is, D7: logged, nothing changes, `false`);
    `get_expression(property) -> std::optional<std::string_view>`;
    `get_expression_error(property) -> std::string_view` (empty when the
    last evaluation succeeded; else the unresolved reference, the type
    problem or the cycle); `set_current_value`; `read_local_state` /
    `apply_local_state` over `Local_state = std::variant<Property_value,
    Expression_text>`, the exact local layer for undo (D11).
  - Resolution and evaluation. References resolve lazily: at compile
    time only the syntax is checked, and each evaluation retries the
    references still unresolved through
    `virtual auto resolve_expression_object(std::string_view path) const
    -> Dependency_object*` (the library default resolves the empty path to
    the object itself). `Item_base` resolves `..` to
    `get_inheritance_parent()` and a name through its `Item_host`
    (`Item_host::find_hosted_item(name)`, implemented by `Scene_host`
    over the scene's nodes and attachments and extended by the editor's
    `Scene_root` with the content library's materials), so a source is
    always hosted by the target's own host and evaluation runs under the
    target's item-host lock without a cross-host read order. A property
    named in a reference is looked up by `find_for_type` on the resolved
    object. tinyexpr binds each reference to a slot in a per-expression
    value array (variables `ref0`, `ref1`, ... substituted in parentheses
    for the braces); the compiled tree is kept for the life of the
    expression.
  - Push and pull. Each resolved source object keeps a dependent list of
    (target object, target property, source property), registered at
    resolution and removed when the expression is replaced or dropped,
    when the target is destroyed, or when the source is destroyed (which
    also turns the reference unresolved again). A change delivered on a
    source property (`deliver`, so batches collapse first) re-evaluates
    every dependent target and notifies it with the value before and
    after, through the target's own `notify` (batches, observers, D19
    callbacks and the editor hook all see an expression result exactly as
    a stored write). `invalidate_dependents(property)` is the public entry
    for a source whose storage changed outside `set_value`:
    `Node::handle_transform_update` calls it for `translation`, `rotation`
    and `scale`, so the transform tool, physics and animation drive
    expressions too; with no dependents it is one null check. A read of
    a driven property whose references are still unresolved retries the
    resolution first (pull), so a source that appears later, a loaded
    scene or a clone converge without a frame hook.
  - Cycles. `set_expression` rejects a direct self reference and walks the
    resolved expression graph from each reference back to the target,
    rejecting a formula that reaches it; a cycle closed later through lazy
    resolution is caught at evaluation by a per-expression re-entry
    guard, which reports `cycle` in the error and keeps the last value.
  - Undo (D11). `Property_set_operation` `before` / `after` are
    `std::optional<Local_state>`, so attaching, editing and removing a
    formula are undoable and undo of a value write restores the formula
    it replaced. `Property_set` (D17) and paste stay value bags: reading
    the local values of a driven property bakes its current result.
  - Rows (D12). A row whose first item is driven draws the formula text
    in place of the widget (commit on Enter or deactivation, one
    operation), a `=` label prefix, the error as a tooltip with a red
    frame, and `Source: expression` in the tooltip. The context menu
    has `Edit as expression` (starts from the current value in formula
    form) and `Remove expression` (bakes the current result as the local
    value); `Reset to default` clears both.
  - MCP and command (D13). `get_item_properties` reports `expression` (the
    text or `null`) and `expression_error`; `set_item_property` and
    `scene.set_property` accept `expression` instead of `value`.
  - Serialization stays with D14: formulas are session state until the
    property serialization work of section 6 lands.

- D23 Inherited flags (`visible`, `shadow_cast`, `lightmapped`).
  - Before the change. `Item_flags::visible` was a self bit; the only
    propagation was `Node_attachment::handle_node_update` /
    `handle_node_flag_bits_update` copying the node's bit onto each
    attachment, so a mesh was visible exactly when its node was and a hidden
    node did not hide its child nodes. `shadow_cast` and `lightmapped` were
    self bits on meshes with no propagation. `Item_flags::invisible_parent`
    is not visibility propagation: `Scene_root` sets it on the scene root
    node and the item tree skips that row and lists its children in its
    place; it stays as it is.
  - Properties. `Item_base` registers `visible` (default `true`),
    `shadow_cast` (default `false`) and `lightmapped` (default `false`) as
    inherits-flagged `bool` properties with owner type
    `Item_base::property_owner_type()`, the id every item class descends
    from (D27), so every item type lists them. The semantics are the D8 ones: the closest ancestor with a
    local value wins, as CSS `visibility` (a child under a hidden parent can
    be shown with a local `true`); WPF `IsVisible` (parent AND self) is not
    reproduced, because the coerced value of a local write is stored at
    write time (D7) and cannot follow a later parent change. None of the
    three carries an R15 editor flag: the draw lists read the derived bits
    (next point), so nothing in the editor hook is needed. `shadow_cast`
    and `lightmapped` carry `.ui.group = "Rendering"`; `visible` is
    ungrouped. All three keep `serialize`.
  - Inheritance tree. A `Node_attachment` is not a `Hierarchy`; it overrides
    `get_inheritance_parent()` to return its node and `Node::
    for_each_inheritance_child` visits child nodes, then attachments.
    `Node_attachment::set_node` (the one path that changes the node
    pointer) captures the attachment's inheritance snapshot before the move
    and applies it after, the way `Hierarchy::set_parent` does for a
    subtree, so a mesh moved between nodes is notified of its new
    inherited visibility.
    This replaces the `visible` half of the attachment flag mirroring; the
    `selected` / `hovered_*` mirroring stays (those are not properties).
  - Derived bits (R14). The three bits stay in the flag word as derived
    bits: the properties' shared changed callback writes the new effective
    value into the item's bit through a private `Item_base::
    set_derived_flag_bit`, which bumps the mutation serial and runs
    `handle_flag_bits_update` as `set_flag_bits` does. An inherited change
    reaches every descendant without a local value through D8, and a tree
    move through the snapshots, so the bit is always the effective value.
    Every reader is unchanged: `Item_filter` over the draw-list entry flag
    words, `is_visible()`, the shadow / lightmap / raytrace mask tests, the
    glTF exporter's flag list. `Item_base`'s constructor starts the word
    with `visible` set (the property default, no parent yet), and the copy
    constructor and assignment re-derive the three bits from the copied
    entries after `Dependency_object` is copied (a copy has no parent, so
    an inherited `false` does not survive the copy; the bit must agree).
  - Writers. `set_flag_bits` (and the `enable_` / `disable_` wrappers) with
    a derived bit in the mask is a programming error: it logs one error
    naming the bit and drops those bits from the mask. `set_visible` /
    `show` / `hide` write the `visible` property (local value), which is
    what the item tree, the tools and the raytrace hide-restore need;
    `Node_raytrace` restores the previous *local state* (`read_local_state`
    / `apply_local_state`) around its hide, so an inherited value is not
    baked into a local one. Construction-site
    `enable_flag_bits(... | visible | ...)` masks dropped the `visible` bit:
    with the default `true` the item is visible, and a local `true` on a
    mesh would stop the node's hide from reaching it (the point of the
    change). A construction site that relied on the bit being absent to
    start hidden calls `hide()` explicitly. `shadow_cast` and `lightmapped`
    construction writes are `set_value(shadow_cast_property, true)`
    (a local value, the per-mesh semantics; a group node's local
    value reaches only meshes without one).
  - Editor. The generic property rows (D12) draw the three checkboxes with
    source, reset and undo for free; the `Properties::item_flags` grid
    skips the derived bits. "Enable / Disable Lightmap (Recursive)" in the
    item context menu is one `Compound_operation` that sets the local
    value on the selected items and clears it on their descendants, so the
    subtree follows the ancestor afterward; `Item_set_flag_bits_operation`
    keeps serving `no_transform_update`. MCP `set_item_flags` rejects the
    three names with a message pointing at `set_item_property`;
    `get_item_properties` lists them.
  - Serialization (the first user of the D14 extras work). The
    `ERHE_node` extension carries `"properties"` (the node's) and
    `"mesh_properties"` (its mesh's) objects of local values, written with
    D16 `to_string` for every non-bridged, non-expression local value whose
    property has `serialize`, and read back through
    `parse_value` and `set_value` (a failed parse or an unknown name is
    logged and skipped). The same mechanism serves the `ERHE_light` and
    `ERHE_camera` `properties` objects (D14). The exporter does not list `visible`,
    `shadow_cast` and `lightmapped` in the `flags` arrays; the importer
    keeps reading them from old files (`visible` absent from a listed
    `flags` array sets local `false`; `shadow_cast` / `lightmapped` present
    set local `true`) so existing scenes load as before.

- D24 Sealing (WPF `Freezable.Freeze`, reversible).
  - Before the change. `Item_flags::lock_edit` is the user's edit lock
    (toggled in the Properties window Locks row, MCP `lock_items` /
    `unlock_items`, persisted by name in glTF) and the prefab editing model
    seals an instance subtree with it (`seal_instance_subtree`, together
    with the two viewport locks, never unsealed: a refresh re-clones). It
    was honored only by hand-written checks: the Properties window disables
    its typed blocks and the name field, MCP `edit_material` refuses a
    locked material, delete skips locked items, and the transform and
    selection tools read the viewport locks. The generic property rows
    (D12), `Property_set_operation` (D11), MCP `set_item_property` and
    `scene.set_property` did not read it, so every registered property of a
    locked item, prefab interiors included, was editable through them; the
    seal at the write funnel closes that for every writer at once.
  - Library. `Dependency_object::seal()` / `unseal()` / `is_sealed()`.
    While sealed, the local layer is frozen: `set_value`,
    `set_current_value`, `clear_value`, `set_expression` and
    `apply_local_state` are rejected the way a read-only write is (D7: one
    logged error naming the property, nothing changes). Reads, inherited
    values and their notifications, coerce, observers and the evaluation
    of an expression already installed keep working: a sealed prefab
    interior still follows its instance root's `visible`, and a formula
    on a sealed object still tracks its sources; only authoring the local
    layer is closed. `apply_local_state` and the untyped `set_value` /
    `clear_value` return `bool` so undo and MCP can report a rejected
    write instead of recording a no-op. A copy (D10) is not sealed (WPF
    `Clone` of a frozen object is unfrozen); `Item_base` re-derives the
    seal from its copied flags (next point). Unlike WPF, `unseal` exists
    because the editor's lock is a user toggle; the prefab code simply
    never calls it.
  - Item integration. The seal is the `lock_edit` flag: `Item_base::
    set_flag_bits` calls `seal()` / `unseal()` when the bit changes and
    the copy paths re-sync, so every existing writer of the flag (the
    Locks row, `lock_items`, `set_item_flags`, `seal_instance_subtree`,
    the scene builder's floor, glTF import) seals through the one path and
    `is_lock_edit()` and `is_sealed()` agree. `set_name`, the flag word
    and the tags are not properties and keep their own checks.
  - Editor. The generic rows draw disabled for a sealed first item, queue
    nothing, and disable Reset to default, Edit as expression, Remove
    expression and Paste Properties (Copy stays); the row tooltip says
    `Sealed (lock_edit)`. `Property_set_operation` and
    `Property_set_apply_operation` log a warning when the write was
    rejected. MCP `set_item_property` and `scene.set_property` refuse a
    sealed item with a message naming `lock_edit` / `unlock_items`, and
    `get_item_properties` reports `sealed` on the item. The typed blocks'
    `edit_disabled` and the delete / transform / selection checks stay:
    they guard state that is not a property.

- D25 Style layer (WPF `Style` setters, `BaseValueSourceInternal.Style`).
  - Context. The graphics presets are a generated POD
    (`Graphics_preset_entry` inside `Graphics_settings`) that the Settings
    window edits in place, with no local-override concept to preserve; they
    stay as they are until `Graphics_settings` is an item (section 6). The
    first user is the material library, and the layer is generic for every
    item type.
  - Library. `Property_style` (`erhe_property/property_style.hpp`) is a
    name plus a `Property_set` (D17), immutable after construction and
    shared as `std::shared_ptr<const Property_style>` (WPF seals a style
    once used; a changed preset is a new object re-applied by whoever owns
    the preset list). `Dependency_object::set_style(style)` /
    `get_style()` install one style per object; R3 is coerced > local
    (a stored value or an expression) > style > inherited > default, with
    `Value_source::style`. A bridged property (D18) is always local and
    ignores a style entry (the transform, the projection). A style entry
    for an inherits-flagged property is the object's effective value and
    so flows to descendants exactly as a local value would: the inheritance
    walk, the descendant notification and the tree-change snapshot treat
    "has a local value" as "has a local or style value". `set_style`
    notifies, through the normal path (batches, D19 callbacks, observers,
    descendants), every property in the union of the old and new style
    whose effective value or source changes; local values are untouched
    and shadow the style (WPF `IsSetOnContainer`). A sealed object (D24)
    rejects `set_style`. A copy (D10) carries the style pointer (it is
    authored state, WPF copies `StyleProperty`). `read_local_value`,
    `for_each_local_value` and `Property_set::read_local_values` stay
    local-only; `Material::operator==` also compares the style pointers.
  - Serialization. A style is session state like an expression (D14):
    the glTF exporters read effective values through the typed accessors,
    so a styled material or light exports its baked values and imports
    them as local values; the `properties` extras of D23 stay local-only.
    Writing a style by name is future work with the preset library that
    would own the names (section 6).
  - Editor. `Style_set_operation` (item, style before, style after) is the
    undo step. The generic rows show `Source: style (<name>)` in the
    tooltip and the `*` prefix only for a local value; the
    context menu has `Paste Properties as Style` (the clipboard bag,
    named after the item it was copied from, becomes the style of every
    selected item through one compound of `Style_set_operation`s) and
    `Clear Style`. MCP: `set_item_style(item, source_item)` builds the
    style from the source item's local values, `clear_item_style(item)`,
    and `get_item_properties` reports `style` (the name or `null`) on the
    item and `style` as a property source.
  - Material library. `add_default_materials` creates one `Property_style`
    `Brushed metal` (roughness, metallic, BxDF model, circular brushed
    metal, anisotropy control) and gives each metal only its base color as
    a local value plus that style, so the twelve materials share the
    traits and a material edited in the Properties window keeps its
    override when the style is swapped or cleared. `Material` has a
    name-only constructor that writes no local values; the
    `Material_create_info` constructor keeps writing every field as a
    local value (a full snapshot, which would shadow a style).

- D26 Computed properties (WPF read-only dependency property whose value
  the owner provides; R6).
  - Context. `Property_key<T>` (D3) is the write permission for a
    stored read-only property, and nothing registers one: every read-only
    candidate (world transform, world bounds, child count) is
    derived state that its owner already keeps or can compute on demand
    (`Node_transforms::world_from_node`, `Mesh::get_aabb_world`,
    `Hierarchy::get_child_count`). Storing it through a key would keep a
    second copy in the entry store and cost a validate / store / notify
    write on every transform update of every node in a moving subtree (R14).
    `Property_key<T>` stays for a read-only property whose value is authored
    state with no other home; computed properties use a value provider.
  - Library. `Property_metadata::compute` (`Compute_callback =
    std::function<Property_value(const Dependency_object&)>`), registered
    through `Property<T>::register_computed(name, owner_type, compute,
    metadata)`, which sets `read_only`. A computed property reads
    `compute(*this)` on every `get_value` with `Value_source::computed`
    (`c_str` `computed`); it has no entry, so
    `has_local_value` is `false`, `read_local_value` is `nullopt`,
    `for_each_local_value` skips it, and with that `Property_set`, copy
    and paste, `operator==` of items, the glTF `properties` extras and a
    copy of the object (D10) never see it. The style layer, inheritance,
    validate and coerce do not apply: the provider's value is the effective
    value. Every write path (`set_value`, `set_current_value`,
    `clear_value`, `set_expression`, `apply_local_state`) is rejected by
    the read-only check (D7 style: one logged error,
    `false`, nothing changes). `default_value` stays the type's zero value
    and is not shown.
  - Push. The owner calls `invalidate_dependents(property)` where the
    provider's inputs change, exactly as bridged storage does (D22), so an
    expression reading a computed property re-evaluates on the change;
    with no dependents that is one null check. Changed callbacks and
    observers do not fire for a computed property: the store never holds
    its previous value, so `Property_changed_args` could not be filled. A
    consumer that needs to react to one reads it through an expression on a
    stored property of its own, or keeps polling its serial (the geometry
    graph `transform_from_node` of D21 stays a poll for this reason).
  - Node. `world_translation` (vec3), `world_rotation` (quat) and
    `world_scale` (vec3) read the components of `world_from_node_transform()`
    (group `World`, tooltips naming the world space); `child_count` (int)
    reads `get_child_count()`. `Node::handle_transform_update` invalidates
    the three world properties next to the three bridged ones: it runs on
    the node that was written and, through `Scene::update_node_transforms`
    and `Node::update_transform`, on every descendant whose world transform
    the pass recomputes, so a child's `world_translation` follows a parent
    move one propagation pass later, which is when the value itself
    changes. `Hierarchy::handle_add_child` / `handle_remove_child`
    invalidate `child_count`; the property is registered by `Hierarchy`
    with its own owner type, so `Node` and `Content_library_node`, the
    two classes deriving from it, both list it.
  - Mesh. `world_bounds_min` and `world_bounds_max` (vec3, group `World`)
    read `get_aabb_world()`; a mesh whose box is invalid (no primitives)
    reports `0 0 0` for both. `Mesh::handle_node_transform_update` and
    the primitive mutation paths (`clear_primitives`, `add_primitive`,
    `set_primitives`, the primitive-data change notification) invalidate
    both. A skinned mesh's posed bounds move with its joints without any of
    these running; reading the property gives the current box, an
    expression on it follows only the pushes above.
  - Editor (D12). A computed row draws its widget disabled like any
    read-only row, with no `*` prefix (no local value), `Source: computed`
    in the tooltip, no `Default:` line, and the context menu's per-property
    entries (`Reset to default`, `Edit as expression`, `Remove expression`)
    disabled through the existing read-only test; the bag entries (copy,
    paste, style) act on the item and stay. MCP `get_item_properties` reports `source`
    `computed`, `local` `null` and `read_only` `true`; `set_item_property`
    refuses it as read-only. Expression references (D22) reach a
    computed property through `find_for_type` and `get_value` with no
    special case: `{cube/world_translation.y}` is a valid source.

- D27 Owner type id (WPF per-class `DependencyObjectType` keying). The
  registry key is a per-class `Owner_type` id (`erhe_property/owner_type.hpp`)
  with a parent link, not the `Item_type` bit mask: bits are shared by
  every graph node kind and carry no ancestor relation, so they can
  neither give a torus node and a sphere node different parameter sets nor
  resolve a metadata override unambiguously. Id 0 is the root that every
  `Dependency_object` belongs to; `allocate_owner_type(parent, name)`
  appends to the registry's id table. An object reports its id through
  `Dependency_object::get_property_owner_type()`. `Item<Base,
  Intermediate, Self>` allocates `Self`'s id under
  `Intermediate::property_owner_type()` in a function-local static, so
  every class in an `Item<>` chain has an id that follows its C++
  inheritance with no code of its own (`Mesh` -> `Node_attachment` ->
  `Item_base` -> root, `Node` -> `Hierarchy` -> `Item_base` -> root);
  `Item_base::property_owner_type()` is the id under the root that every
  item descends from. A class outside an `Item<>` chain (the geometry
  graph node kinds derive plainly from `Graph_editor_node`) keeps a
  `property_owner_type()` function-local static allocated under its base's
  id plus a `get_property_owner_type()` override; a runtime-defined kind
  allocates one id per kind under its class id (the texture graph
  descriptors, section 4.5). Lookup and listing walk the chain:
  `Property_registry::find(id, name)` is the exact registration,
  `find_for_object(id, name)` tries the object's id then each ancestor and
  the nearest registration wins (a derived-class registration shadows a
  base one by name), and `for_each_property_of_object(id)` lists the
  root's registrations first down to the object's own, each level in
  registration order, a shadowed name or a multiply-owned property
  (`add_owner`) once at its nearest level. Ids are run-local: nothing
  persists them, properties serialize by name everywhere (glTF extras,
  MCP, clipboard); the id's name serves logs only. `Item_type` bits keep
  serving `Item_filter`, `is<T>`, the content library cache and the
  editor's type masks, and the library knows nothing of them.

- D28 Object references. `Object_reference` (the last `Property_value`
  alternative, `Property_type::object`) holds a strong
  `std::shared_ptr<Dependency_object>` compared by identity: the same
  lifetime the members it replaced had (a material keeps its textures
  alive, a mesh its materials), so a copy of the object (D10) shares the
  pointee and the store holds exactly what the member held. The library
  knows `Dependency_object`, not `Item_base` (D1).
  - Class restriction. The registration's validate (R4, value only)
    rejects a non-null pointee that is not of the owner's accepted class
    (`Member_value_traits<std::shared_ptr<U>>` supplies it for a
    member-backed registration, D18). For the editor, `Property_ui`
    carries `reference_item_types` (the `Item_type` mask the row accepts,
    opaque to the library) and `show_clear_button`.
  - Text form. `to_string` is the pointee's `get_reference_path()`, a
    `Dependency_object` virtual that is the inverse of
    `resolve_expression_object` (`Item_base`: the item name; empty for a
    null reference), and the pointer an `Object_reference` stores comes
    from `get_shared_reference()` (`Item_base`: `shared_from_this`, null
    for an item no `shared_ptr` owns, which no reference can then hold).
    The library's context parse (D16) walks from the referencing object's
    `Item_host`; the editor resolves names itself through
    `resolve_reference_by_name` (`scene/item_lookup`), the lookup in the
    scene `find_scene_root_for_item` finds for the item - its host, else
    the content library that lists it, else the asset manager's defining
    record - because an asset-typed item such as a material has no host.
    Names are not unique, exactly as for D22 references; MCP disambiguates
    with `reference_id`.
  - Editor write funnel. `apply_item_property` applies an object value
    only when the referenced item belongs to the target's scene, or is a
    manager-owned asset (material, brush, animation) the asset manager
    reports cross-scene referenceable; a scene-hosted texture, graph
    texture or node never crosses scenes (the manager treats every item
    without a defining record as referenceable, which is meant for
    assets). A failing check logs a warning naming both items and
    applies nothing. `Property_set_operation` and
    `Property_set_apply_operation` adopt an `Asset_reference` usership
    for every managed asset in their before and after states at first
    execute (asset-manager plan R5.4) and report those items from
    `collect_item_references`.
  - Rows (D12). The row is `item_reference_imgui`: a drop target
    filtered by `reference_item_types`, a drag source, a picker over the
    target scene's items of that mask - the content library items, and
    the scene's nodes and node attachments for a node-typed reference
    (`collect_reference_candidates`, a caller-owned scratch cleared after
    the draw) - and a clear button when `show_clear_button` holds; it
    commits on selection like the enumeration combo.
  - Everything else applies as to any type: inheritance, styles, coerce,
    computed and read-only, `Property_set` bags (a pasted bag shares the
    pointee), and `operator==` of items.

- D29 Property sub-objects. A `Dependency_object` an item owns by value
  that is not an item itself - a `Mesh_primitive` - is addressed as
  (item, index) through three `Item_base` virtuals with empty defaults:
  `get_property_sub_object_count()`, `get_property_sub_object(index)`
  (const and non-const) and `get_property_sub_object_label(index)`. The
  index is stable for the item's current list. `Property_set_operation`
  carries the optional index (D11), `Dependency_property_rows::
  add_sub_object_rows` draws a sub-object's registered properties with
  the same edit, undo and context-menu machinery (the bag entries - copy,
  paste, style - act on items and are not offered), MCP lists
  `sub_objects` and takes `sub_object` (D13), and a sub-object of a
  sealed item (D24) is as sealed as the item, checked in the editor
  funnel because the sub-object has no seal of its own. Section 4.9 is
  the user.

## 4. Implementation

### 4.1 Material

The following former `Material_data` fields are registered properties of
`Material` (owner type `Material::property_owner_type()`), with the previous initializers
as defaults: `base_color` (vec3), `opacity`, `roughness` (vec2), `metallic`,
`reflectance`, `emissive` (vec3), `ior`, `transmission`,
`normal_texture_scale`, `occlusion_texture_strength`, `double_sided` (bool),
`alpha_cutoff`, `use_circular_brushed_metal` (bool), `use_aniso_control`
(bool), and the enumerations `normalmap_encoding` (`Normalmap_encoding`),
`bxdf_model` (`Bxdf_model`), `blending_mode` (`Material_blending_mode`) and
`circular_brushed_metal_texgen_mode` (`Texgen_mode`), each with an
`Enum_info` table placed next to its existing `c_str` in
`erhe_primitive/enums.hpp` (D2a), with the tables defined next to the
`c_str` implementations in `primitive.cpp`. Validate callbacks reject
values outside [0, 1] for `opacity`, `metallic`, `transmission`,
`alpha_cutoff` and `occlusion_texture_strength`; `ior` has no validate so a
file value outside the UI slider range still loads.

`double_sided`, `blending_mode` and `bxdf_model` register with
`affects_draw_list_partition`, `normalmap_encoding` and `bxdf_model` with
`affects_shader_variant`, so the D11 hook owns the rebuilds. Each migrated
field carries the `Property_ui` block of the hand-written row it replaced
(ranges, color presentation for `base_color` and `emissive`).

`Material_data` keeps only `texture_samplers`. `Material` exposes typed
getters and setters for every migrated field (`get_base_color()`,
`set_base_color()`, ...) implemented on the property store, so call sites
(`Material_buffer` upload, shader variant derivation, glTF import/export, the
material preview, MCP tools, tests) use the accessor and nothing else
changed. `operator==(Material)` compares the remaining `Material_data`, the
property bags of D17 and the style pointers (D25).

The five texture slots are object properties (D28) registered with
`register_member` (D18) over `data.texture_samplers.<slot>.texture_reference`:
`base_color_texture`, `metallic_roughness_texture`, `normal_texture`,
`occlusion_texture` and `emissive_texture` (group `Textures`,
`reference_item_types` texture | graph texture, the PBR slots
`visible_when` lit), flagged `affects_shader_variant` because a bound slot
selects the texture-using variant and a normal texture's two-component
flag rides the texture. Member-backed because the per-frame readers
(`Material_buffer::gather_texture`, `Shader_key::derive`) read the member
with no variant access, the `Material_create_info` designated initializers
at every construction site keep working, and the slot's sampler stays in
`Material_data` with `Material_change_operation`. The slot transforms are
member-backed the same way, over the same slot: `<slot>_texture_texgen_mode`
(`Texgen_mode`, flagged `affects_shader_variant` because `Shader_key`
selects the texgen variant from it), `<slot>_texture_uv_rotation`
(`angle_degrees`), `<slot>_texture_uv_offset` and `<slot>_texture_uv_scale`
(vec2), each in the `<Slot> Texture` UI group and `visible_when` the slot
is bound (the PBR slots also lit); the per-frame readers
(`Material_buffer`, `Shader_key`) and the glTF import and export read the
members.
The `Member_value_traits` cast to `erhe::graphics::Texture_reference` needs
the complete class, so `erhe::primitive` links `erhe::graphics` privately.
Writers of a live material use `set_base_color_texture()` and the other
setters, `set_slot_texture(slot, texture)` for a slot held by pointer,
`set_value` on a slot transform property, or `set_data(Material_data)`
(what `Material_change_operation` applies: the textures and slot
transforms through the properties in one change batch, the samplers
assigned);
construction-time fills (`Material_create_info.data`, the glTF importer's
`bind_material_textures`) stay member writes under the D18 caveat. The
Properties window draws the slot textures and transforms as generic rows
and, under a `<Slot> Sampler` group, the bound slot's sampler rows; its
material inspect snapshot ignores the property-backed slot fields so a
texture or transform edit records one operation.

### 4.2 Node

`Node` registers `translation` (vec3), `rotation` (quat) and `scale` (vec3)
with the identity defaults, flagged `affects_transform`, as bridged
properties (D18) over `Node_data::transforms.parent_from_node`: `get` reads
the `Trs_transform` component, `set` writes it and runs the same
`update_world_from_node` + `handle_transform_update` sequence as
`set_parent_from_node`, so every existing transform writer (transform tool,
physics writeback, animation, glTF import, MCP) is unchanged and every
property writer (Properties rows, `Property_set_operation`, MCP
`set_item_property`, `scene.set_property`) reaches the same state. The
transform's skew component stays internal. `world_from_node` stays a
derived cache; the world components are exposed as computed properties
(D26), not stored ones.

`Animation_sampler::apply` keeps writing the `Trs_transform` directly; the
bridged properties read that storage, so playback and the property view
agree. Playback still overwrites the authored transform; the animated layer
that would preserve it is section 6 work.

R14 holds by construction: no per-frame path changed.

### 4.3 Light

Every `Light` public field except `layer_id` is a registered property
(owner type `Light::property_owner_type()`) stored in the entry store, the Material way
(section 4.1), with the previous initializers as defaults: `light_type`
(`Light_type`, `Enum_info` table `c_light_type_enum_info` defined next to
`Light::c_type_strings` in `light.cpp`, labels Directional / Point / Spot;
named `light_type` with `get_light_type()` / `set_light_type()` because
`get_type()` is the `Item_base` type-bits accessor),
`color` (vec3, color presentation), `intensity` (float, logarithmic slider
0.01..20000, tooltip stating the KHR_lights_punctual units: lux for
directional, candela for point and spot), `temperature` (float, slider
0..12000, 0 = off), `range` (float, logarithmic slider 1..20000),
`inner_spot_angle` and `outer_spot_angle` (float, `angle_degrees`
presentation, 0..pi, `visible_when` the light is a spot light) and
`cast_shadow` (bool). `layer_id` stays a plain member: it is a resolver
output, not authored state. No property carries a validate callback:
`is_active()` already interprets non-positive range, intensity and spot
angles as "inactive", so a file that stores them must still load.

Every `Light` property registers with one shared `property_changed`
callback (D19) that calls the light-set re-resolve; `Light::notify_changed()`
is private and only called from that callback.

`Light` exposes `get_light_type()` ... `set_cast_shadow()` accessors on the
store (the same shape as Material's); every call site (glTF import / export,
scene builder, light buffer, shadow renderer, light set, lightmap baker,
light mesh, brush preview, MCP tools, debug visualizations, icon set, sky
renderer, example, tests) reads through them. The photometric helpers
(`get_solid_angle`, `get_luminous_flux`, `set_luminous_flux`,
`get_effective_color`, `is_active`, `casts_shadow`) read the store.
`Light(const Light&, for_clone)` copies only `layer_id`; the entries
copy through D10.

`Properties::light_properties` keeps only the derived rows the generic
section cannot draw: the flux (lumens) slider for point and spot lights,
the blackbody swatch shown while temperature is positive, and the
zero-range warning for point lights. The Light Type, Cast Shadow, spot
angle, Range, Intensity, Color and Temperature rows come from the generic
section.

### 4.4 Camera

`Camera` registers its `Projection` fields as bridged properties (D18) over
`m_projection`, so `Camera::projection()` keeps returning the mutable
`Projection*` that the fly camera, scene commands, previews, glTF import
and tests write through; a bridged property reads and writes the
same member, so both paths agree, with the D18 caveat that a direct member
write does not raise a changed notification (nothing observes camera
properties yet). The properties, all in the `Projection` UI group:
`projection_type` (`Projection::Type`, table `c_projection_type_enum_info`
defined next to `Projection::c_type_strings` in `projection.cpp`),
`fov_x`, `fov_y`, `fov_left`, `fov_right`, `fov_up`, `fov_down`
(`angle_degrees`, each `visible_when` the current projection type uses it),
`ortho_left`, `ortho_width`, `ortho_bottom`, `ortho_height`,
`frustum_left`, `frustum_right`, `frustum_bottom`, `frustum_top`
(logarithmic sliders 0..1000, `visible_when` per type as the hand-written
rows switched), `z_near`, `z_far` (logarithmic sliders 0..1000) and
`infinite_z_far` (bool, `visible_when` perspective types). Each registers
with `register_member` over its `Projection` field, the accessor reaching
the field through `Camera::projection()`.

`exposure` and `shadow_range` are the camera's own scalars with no
engineered representation, so they live in the entry store as ordinary
properties (logarithmic sliders 0..800000 and 1..1000) and the
`get_exposure()` / `set_exposure()` / `get_shadow_range()` /
`set_shadow_range()` accessors read and write the store. The clone
constructor keeps copying `m_projection`; the entries copy through D10.

`Properties::camera_properties` is gone: every row it drew is a generic
row now.

### 4.5 Graph nodes (geometry and texture)

Every geometry graph node kind registers its parameters as properties
keyed on its own owner type id (D27) with `register_member` over its
existing members (D18), `editor::mark_node_dirty`
(`src/editor/graph_editor/graph_node_property_bridge.hpp`) as the
`after_set` hook and `Mesh_torus_node` as the recipe: a `property_owner_type()`
function-local static allocated under `Graph_editor_node`'s id, a
`get_property_owner_type()` override, one
static `Property<T>` member per parameter whose `Property_ui` carries the
canvas widget's ranges, and `flags = Property_flags::none` because the
graph asset's JSON (`write_parameters` / `read_parameters`) stays the
only serializer - the members are the storage, so evaluation (including
the off-main-thread shadow-clone snapshot) and serialization are
untouched by the migration. Enumeration parameters carry `Enum_info`
tables built from the canvas combo labels; per-mode rows use
`visible_when` (the Conway operator floats, the transform node's
rotation representations, the lattice cage bounds). A parameter whose
canvas edit has a side effect keeps it in a hand-written bridge `set`
(the lattice `auto_fit` cage freeze and `divisions` offset resample).

Hosting and reachability: graph nodes share their owning asset's
`Item_host` (the `Scene_root` that hosts the asset through the content
library; `Graph_asset::set_item_host` forwards it, the owning-setter
covers nodes added later, and `erase_node` clears it so a node held only
by the undo stack cannot dangle). A D22 expression on a node parameter
therefore resolves scene items as same-host sources under one item-host
lock - `{cube/world_translation.x}` on a torus radius re-bakes the mesh
when the cube moves, pushed through `Node::handle_transform_update`,
with no polling - and `find_item_in_scene` also walks the library's
graph assets and their nodes, so `Scene_root::find_hosted_item` and the
MCP property tools reach graph nodes by name or id.

Writers and undo: canvas `imgui()` widgets keep writing the members
directly plus `mark_dirty()`, and their undo stays the whole-node JSON
`Graph_parameter_operation` (one per edit gesture); the generic rows in
the Node Properties window, MCP `set_item_property` and expression
deliveries write through the bridge (one `Property_set_operation` per
edit, reset-to-default and formulas included). The two undo owners do
not interact: `Graph_editor_node::on_property_changed` refreshes the
committed JSON baseline when a parameter property changes outside a
canvas gesture, and `Graph_editor_node::mark_dirty` re-runs expressions
reading the node (`invalidate_dependents` behind a `has_dependents`
check; shadow clones never have dependents, so the worker path stays one
null check). A canvas drag on an expression-driven parameter is
overwritten at the next delivery, the same way the gizmo behaves on a
driven translation (D22).

The Node Properties window draws the generic rows (D12) for any node
whose owner type id is not `Graph_editor_node`'s own (a kind with
registered properties); what cannot be a property - `Asset_reference`
pickers, the lattice control point editing - draws through the node's
`unregistered_parameters_imgui` override in an `Extra` row, and nodes
with no registered properties (transform_from_node, passthrough, join,
unary-op, groups, brush source) keep the hand-rolled `Parameters` entry.

Texture graph: the parameters are per *descriptor*, not per C++ type
(`Texture_descriptor_node` holds a `Parameter_value` vector parallel to
its `erhe::texgen::Node_descriptor`), and the descriptors are
function-local statics built on first use, so registration cannot ride
C++ static initialization: `register_texture_graph_properties()`
(`src/editor/texture_graph/texture_graph_properties.cpp`), called once
from `run_editor()` while still single-threaded (the registry's write
window, D3 / R12), allocates one owner type id per descriptor under
`Texture_descriptor_node`'s id and registers
one property per float / color (vec4) / enum / bool / size parameter
plus the seed of a seeded descriptor, with `Enum_info` tables built from
the descriptor labels (pointer-stable process-lifetime storage) and
bridges reaching the node's `Parameter_value` by parameter index.
Gradient and curve parameters have no `Property_value` form and stay in
`imgui()`.

### 4.6 Usage

- Editor. The Properties window draws the generic rows for every selected
  item (D12): source and default in the tooltip, `*` prefix on a local
  value differing from the default, `=` prefix on a driven row, mixed
  multi-selection, and the context menu (Reset to default, Copy / Paste
  Properties, Paste Properties as Style, Clear Style, Edit as expression,
  Remove expression). Every edit is one undoable operation.
- MCP tools: `get_item_properties`, `set_item_property` (with `value` or
  `expression`), `set_item_style`, `clear_item_style`; `lock_items` /
  `unlock_items` toggle the seal; `set_item_flags` rejects the derived
  bits (`visible`, `shadow_cast`, `lightmapped`) with a hint to use
  `set_item_property`. Undo is verifiable through `undo` / `redo` and
  `get_undo_redo_stack`.
- Command scripts: `scene.set_property` (`config/editor/commands.json`,
  `doc/command_script.md`) with `item`, `property` and `value` or
  `expression`, using the D16 string conversion (enumerations by label).
- Registering a new property: follow the D3 example; `Material`
  (section 4.1, stored and member-backed) and `Camera` (section 4.4,
  hand-written bridge) are the recipes. An enumeration needs its
  `Enum_info` table next to the `c_str`; a member that already exists is
  `register_member` (D18); a reference to another item is an
  `Object_reference` property (D28) with `reference_item_types` set.

### 4.7 Tests

- `src/erhe/property/test/` (gtest): registry and overrides (owner type
  chains included: listing order, shadowing, nearest-ancestor overrides,
  `add_owner` on the chain), every
  value type (the integer vectors included), validate, coerce, change
  notifications and batching, inheritance through a `Test_object` tree,
  observers, enumerations, string conversion, property sets, bridged
  storage, expressions (integer-vector targets and component sources
  included), sealing, styles, computed properties, object references and
  `register_member` (`test_object_reference.cpp`).
- `src/erhe/item/test/`: `test_properties.cpp` (inheritance through
  `Hierarchy`, reparent, clone), `test_item_visibility.cpp` (derived
  bits), `test_item_sealing.cpp` (`lock_edit` seal sync).
- `src/erhe/scene/test/`: `test_node_properties.cpp` (bridged transform),
  `test_attachment_inheritance.cpp` (mesh inherits from its node),
  `test_light_properties.cpp`, `test_camera_properties.cpp`,
  `test_node_computed.cpp` (world properties, `child_count`, mesh bounds).
- `src/erhe/primitive/test/test_material_style.cpp` (the `Brushed metal`
  style), `test_material_textures.cpp` (the texture slot properties).
- `src/erhe/scene/test/test_mesh_primitive_material.cpp` (the primitive
  material property, the sub-object addressing, the owner stamp across
  copies).

### 4.8 Notes and gotchas

- Every binary that initializes `erhe::item` logging must call
  `erhe::property::initialize_logging()` first; the first library log
  call otherwise dereferences a null logger (bit the editor, the example
  and the test mains).
- `Node_attachment::set_node` holds a `shared_ptr` to itself for its
  duration: `Node::~Node` / `Node::detach` reach it through a raw pointer
  and `handle_remove_attachment` can erase the last owning `shared_ptr`
  mid-call, and the inheritance snapshot apply touches the object after
  that point.
- `Material_preview::render_preview(texture, material)` generates the
  mipmap levels below the rendered one after the render; without that a
  thumbnail sampled at a fraction of its size reads uninitialized memory
  (was solid magenta on Metal).
- Behavior changes from the inherited `visible` default (D23): `grid_tool`
  "Add Grid" creates a visible grid (it previously relied on the omitted
  bit); `Quad_view` hides its rendertarget node explicitly at
  construction.
- The camera's glTF `properties` extras object repeats `exposure` /
  `shadow_range` next to the native `ERHE_camera` fields; both import to
  the same value.
- glTF export bakes a light's `temperature` into the exported color
  (KHR_lights_punctual has no temperature), so an export / import round
  trip matches on every light property except `temperature`.

### 4.9 Mesh primitives

`Mesh_primitive` derives from `Dependency_object` with its own owner type
(allocated under the root, named `Mesh_primitive`) and registers `material`
as an object property (D28) with `register_member` over its `material`
member (`reference_item_types` material, no clear button: a primitive keeps
a material). The registration's `after_set` calls the owning mesh's
`notify_primitive_material_changed()`, the `Scene_host::
on_mesh_material_changed` notification `set_primitive_material` ran inline
before; `Mesh::set_primitive_material` writes the property, so it stays the
one writer (draw list material set plan R4) and the generic rows, undo and
MCP reach the same funnel. Each primitive carries an owner link (mesh,
index) that `Mesh::stamp_primitive_owners()` writes after every change of
the primitive list - `add_primitive`, `set_primitives`, the clone
constructor and the move operations - so the link survives the vector
copies. The mesh addresses its primitives as property sub-objects (D29):
the count, `&m_primitives[index]`, and the primitive's name (`Primitive N`
when it has none) as the label; the Properties window's `Primitive N`
groups draw the sub-object rows.

Storage rule: `Mesh::m_primitives` is a `std::vector`, and a reallocation
copy-constructs the `Dependency_object` base, which keeps neither
observers nor expression dependents (D10). A `Mesh_primitive` therefore
registers member-backed properties only (no entries to lose) and nothing
subscribes an observer or an expression to a primitive; `get_primitives()`
is const, so no caller reaches `set_value` on a primitive except through
`Mesh`.

### 4.10 Node_physics

The editor's `Node_physics` attachment registers its authored rigid body
state as member-backed properties (D18, owner type
`Node_physics::property_owner_type()`, UI group `Rigid Body`) over the
`IRigid_body_create_info` it keeps and the intended motion mode: the
members are what the rigid body is (re)created from at scene attach, so
they stay the storage, and the `after_set` hook pushes the change to the
live body. `motion_mode` (`erhe::physics::Motion_mode`, `Enum_info` table
`c_motion_mode_enum_info` next to `c_motion_mode_strings` in
`erhe_physics/irigid_body.hpp`, the four authorable modes) re-derives the
effective mode and sets it on the body; `is_trigger` recreates the body;
`friction` and `restitution` (validated to [0, 1]) set the body's;
`linear_damping` and `angular_damping` ([0, 1], `visible_when` the mode is
not static) set the body's damping while the body is not static;
`gravity_factor` (0..2, `visible_when` not static) likewise;
`wind_receptivity` (0..10 kg/s) and the two initial velocities (world
space, applied at (re)creation) have no live consequence.
`physics_material` and `collision_filter` are object properties (D28,
`reference_item_types` the physics material and collision filter type
bits; `find_item_in_scene` walks the content library's physics materials
and collision filters so a name resolves) whose hook sets the body's
material or filter;
`reapply_physics_material()` / `reapply_collision_filter()` push the
current one again after the referenced item itself was edited (what
`scene/physics_edits` calls), because a write of the pointer the member
already holds is a no-op (R4).

Two properties carry hand-written bridges, each with its own no-op check
because a bridged `set` is reached for every write: `mass` (the create
info's optional mass, read from the live body until set; validated
positive; the set stores it and scales the body's local inertia with the
mass ratio) and `center_of_mass_offset` (the offset-center-of-mass
wrapper around the collision shape; the set rewraps and recreates the
body).

The typed accessors (`set_motion_mode()`, `set_trigger()`, `set_mass()`,
`set_friction()`, ... `set_collision_filter()`) write through the
properties, so the MCP `edit_physics_body` tool, the glTF physics import
overrides, the geometry graph mesh binding and the Properties rows all
notify; the physics tool's drag-time overrides of friction, damping and
gravity go to the rigid body only and are transient. The glTF exporters
read the accessors, not the live body. `Properties::node_physics_properties`
keeps only the diagnostics (body label, position, active state, collision
shape, local center of mass and inertia).

## 5. Out of scope

Kept out deliberately, as they are the WPF parts that serve XAML UI rather
than a scene model: templates and triggers, `UncommonField`, dispatcher
thread affinity, the attached-property browsable attributes, and two-way
bindings (they exist for UI controls writing back to
a model; erhe's ImGui windows are immediate-mode and read the item
directly). Expressions and one-way bindings are D22, sealing is D24, the
style layer is D25.

## 6. Future work

- Animated value layer (WPF `SetAnimatedValue` / `GetAnimationBaseValue`):
  a layer between coerced and local in R3, stored as a second optional in
  the entry (D5), set and cleared by `Animation_sampler::apply` and
  animation stop so playback never overwrites the authored local value and
  keying (`doc/animation-keyframing-plan.md`) reads the local value as the
  authored pose. No prerequisites; the keyframing plan and non-destructive
  playback of generalized animation channels (below) both wait on it.
- Property serialization to glTF: expression text of driven properties
  (D22), material local values (today materials export field by field and
  a round trip bakes effective values into local ones), and one carrier
  per item type in place of the `properties` / `mesh_properties` members
  scattered across `ERHE_node`, `ERHE_light` and `ERHE_camera` (D14,
  D23); the object properties of D28 keep riding their native carriers
  and get that plan's `native_gltf` flag. Future work that is not yet
  fully planned:
  `doc/gltf-properties-extension-plan.md` is the draft, explicitly
  incomplete and not ready to implement; the decisions it records so far
  live there and nowhere else.
- Style users beyond the material library (D25): the graphics presets once
  `Graphics_settings` is an item with registered properties, per-instance
  prefab overrides once `doc/gltf-prefabs-plan.md` phase 6 takes them on,
  and a preset library that owns style names so a style can be written to
  glTF by name.
- Further computed properties (D26) as their consumers appear: a node's
  world bounds over its subtree, a scene's item counts.
- Layout per-child hints as attached properties (WPF `DockPanel.Dock`,
  `Grid.Row`): `Layout` registers alignment and margin as attached
  properties and reads them from each child node, so a child carries its
  hint without `Node` knowing about layouts. First attached-property user
  (R7). The library side exists (`register_attached`, D3); the first user
  also needs the enumeration and UI side, because
  `for_each_property_of_type` deliberately skips attached properties, so
  the generic rows (D12) and MCP `get_item_properties` would not show the
  hint on the child without a way to list the attached properties set on
  an object (`for_each_local_value` reaches them once set).
- Further item migrations, each reusing the Material recipe (section 4.1).
  The Properties window rows still hand-written (untinted, D12) are the
  inventory; in priority order:
  - `Physics_material` (static and dynamic friction, restitution, the two
    combine modes) and `Physics_joint_settings` (limits and drives are
    lists, which have no `Property_value` form and stay hand-written).
  - `Layout` (type, volume min and max, primary / secondary / tertiary
    axes, gap, grid tracks) and `Layout_item` (align x / y / z, margins,
    auto cell, grid cell, grid span) - the latter is the attached-property
    candidate above, and both wait on it.
  - `Light`: flux and blackbody temperature.
  - `Scene`: ambient light; the per-scene overrides stay a settings block.
  - `Rendertarget_mesh`: width, height, pixels per meter.
  - `Animation`: start and end time.
  - `Node_joint`: enable collision, and the connected node as a node-typed
    object reference (D28).
  - `Brush_placement`, and `Grid`.
  The remaining rows are read-only diagnostics (geometry and buffer mesh
  counts, texture dimensions, raytrace state, skin joints), name and id
  rows, and list editors (attachments, samplers, channels), which are not
  properties.
- Shader graph (`src/editor/graph/`) node parameters as properties. The
  oldest graph editor has no parameter serialization and no undo
  operations at all; migrating it starts with adopting the
  `Graph_editor_node` base (or retiring the prototype), not with
  registrations.
- Editor per-item state as attached properties registered by the editor:
  item tree expansion, sheet-window formulas. Shares the attached-property
  enumeration need with the `Layout` item above (whichever lands first
  builds it).
- Animation channels targeting arbitrary properties (not only node TRS),
  which becomes possible once `Animation_channel` stores a
  `Dependency_property` index instead of `Animation_path`. For playback
  that does not overwrite the authored local value it also depends on the
  animated value layer (above); without it a generalized channel would
  clobber local values the way TRS playback clobbers the transform today.

## 7. Verification workflow (macOS, Metal build tree)

- Configure once after adding files: `bash scripts/configure_xcode_metal.sh`
  (or `cmake .` inside `build_xcode_metal` when only CMake lists changed).
  The generated Xcode project lists sources from the CMake files; an
  undefined-symbol link error after adding a file means the project is
  stale, re-run the configure script.
- Build: `cd build_xcode_metal && xcodebuild -project erhe.xcodeproj -target
  <target> -configuration Debug -parallelizeTargets -quiet`, targets
  `erhe_property_tests`, `erhe_item_tests`, `erhe_scene_tests`,
  `erhe_primitive_tests`, `erhe_scene_renderer_tests`,
  `erhe_scene_renderer_gpu_tests`, `editor`. Run the test executables one at
  a time from `build_xcode_metal/src/erhe/<lib>/test/Debug/`.
- Editor: from the repo root `ERHE_MCP_PORT=3743
  build_xcode_metal/src/editor/Debug/editor &`, wait for
  `MCP server: listening` in `logs/log.txt`, then drive it with
  `python3 scripts/mcp_call.py <tool> '<json>'`. Useful id sources:
  `list_scenes`, `get_scene_nodes`, `get_scene_materials`,
  `get_scene_lights`, `get_scene_cameras`, `get_node_details` (attachment
  ids). `reparent_node` builds subtrees; `get_undo_redo_stack` shows the
  operations a check queued.
- `capture_screenshot` works on the Metal swapchain (the same one-frame
  arm-then-collect protocol as the Vulkan windowed build), so the visual
  check of the Properties rows is `capture_screenshot` with a `path` in
  the scratch directory, cropped to the Properties window; system screen
  capture tools are not used (they trip the endpoint security agent).
- Gotchas seen during the phase verifications:
  - Imported lights appear in `get_scene_lights` only after the import
    settles; a query in the same second lists the originals only.
  - The scene builder's insert compound sits low on the undo stack; count
    the `undo` calls a check needs instead of undoing until empty.
  - The generic rows sit below the Mesh section for a mesh-carrying node;
    select an empty node for a Properties-window screenshot.
  - VirtualCity is not used for measurements on Metal: a present-stall
    hang there can wedge the machine.
  - The cube and cuboctahedron of the default scene are outside the
    default view; use the dodecahedron / icosahedron for visibility
    screenshots.
