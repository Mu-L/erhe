# Property system (WPF dependency-property port) - plan

Status: phases 0-4 and 6-8 implemented and verified (library, item
integration, editor operation / rows / MCP tools / startup command, Material
migration, Node transform properties, Light and Camera migrations, observer
users, expressions and bindings, inherited flags, sealing, style layer).
Section 7 holds the remaining work.

Reference: the WPF property system in `~/git/tksuoran/wpf`, files under
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
property store, and the migration of `Material` and `Node` onto it so the
editor can edit, undo and inspect item state through one generic
mechanism instead of one hand-written path per field.

Value types in scope: `bool`, `int`, `float`, `glm::vec2`, `glm::vec3`,
`glm::vec4`, `glm::quat`, `std::string`, and C++ enumerations (any
`enum class` with an enumerator table, see D2a).

## 2. Requirements

- R1 Registration. A property is registered once, statically, with a name, a
  value type, an owner item type and default metadata, and gets a stable
  global index. Registration and lookup by (owner type, name) and by index
  are available at runtime for the editor and MCP.
- R2 Typed access. Callers read and write through a typed handle
  (`Property<T>`), so a `glm::vec3` property cannot be written with a `float`.
  An untyped path (`Property_value` variant) exists for generic code: the
  Properties window, undo, serialization, MCP.
- R3 Precedence. The effective value of a property on an object is, in
  decreasing precedence: coerced, local, style (D25), inherited, default.
  Each layer can be present or absent independently; clearing a layer
  exposes the next one.
  The layer order leaves room for an animated layer between coerced and
  local (section 7).
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
  registry is immutable after static initialization and is read lock-free.
- R13 Undo. Every property write made from the editor UI is an `Operation` on
  the operation stack and restores the exact previous state on undo,
  including "no local value" (a clear), not just the previous effective value.
- R14 Performance gate for `Node`. After the `Node` transform migration, the
  per-frame physics transform writeback, animation playback and
  `Scene::update_node_transforms` on Sponza and VirtualCity stay within 10%
  of the pre-migration frame time measured with Tracy on the same machine.
  The migration lands only when this holds.
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

- D1 Library placement. New library `src/erhe/property` (target
  `erhe_property`, alias `erhe::property`, namespace `erhe::property`),
  header prefix `erhe_property/`. Dependencies: `glm::glm-header-only`,
  `erhe::utility`, `erhe::profile` (public); `erhe::log`, `erhe::verify`,
  `fmt` (private). `erhe::item` links it publicly and `Item_base` derives
  from `Dependency_object`. The library holds no scene, item or hierarchy
  knowledge; tree walking reaches it through one virtual on the object (D8).
- D2 Value representation.
  `Property_value = std::variant<bool, int, float, glm::vec2, glm::vec3,
  glm::vec4, glm::quat, std::string, Enum_value>` with a matching
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
  name, `Property_type`, owner type (the `Item_type` bit of the registering
  class, or 0 for types outside `Item_type`), flags (read-only, attached),
  validate callback, default `Property_metadata`, and a small vector of
  (owner type, `Property_metadata`) overrides. `Property_registry` is a
  function-local static (safe against static-initialization order) holding
  records by index and an index by (owner type, name). Registration happens
  from static members of the owning class:

  ```cpp
  // material.hpp
  static const erhe::property::Property<glm::vec3> base_color_property;
  // material.cpp
  const erhe::property::Property<glm::vec3> Material::base_color_property =
      erhe::property::Property<glm::vec3>::register_property(
          "base_color", Item_type::material,
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
  resolution for (property, object) walks the override list for the object's
  `get_type()` bits: an override whose owner mask equals the bits wins, then
  the last-registered override sharing a bit, then the default metadata.
  Override lists are short (usually empty), so this is a linear scan with no
  cache.
- D5 Effective value store. `Dependency_object` owns
  `std::vector<Effective_value_entry>` sorted by property index and searched
  by binary search (WPF `EffectiveValueEntry` array). An entry holds: index,
  the local `Property_value`, and `std::optional<Property_value> coerced`
  set only when the coerce callback changed the local value (the future
  animated layer of section 7 is another optional in the entry). An entry
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
  as `std::optional<Property_value>` (the local value, or nullopt for "no
  local value") and `after` likewise. `execute` applies `after` (set or
  clear), `undo` applies `before`. It calls one editor hook,
  `App_context::on_item_property_changed(item, property)`, after each apply,
  which reads the property's `Property_flags` (D4) and runs the matching
  editor consequence: `affects_draw_list_partition` rebuilds the draw lists
  (the body of `rebuild_draw_lists` in `material_change_operation.cpp`),
  `affects_shader_variant` re-derives the material's shader variant. The
  hook is the only place that maps flags to editor actions.
  `Material_change_operation` stays for the fields that remain in
  `Material_data` (texture samplers). A `Property_set_apply_operation`
  applies a `Property_set` (D17) to a list of items and records one before
  bag per item, for paste and multi-selection edits.
- D12 Editor UI. `Property_editor` gains a generic
  `dependency_properties(item)` section that lists the registered properties
  of the item's type in registration order and draws one widget per
  `Property_type`: checkbox, drag int, drag float, drag float2/3/4, quaternion
  as Euler-degree drag with a raw x/y/z/w readout, input text, and a combo
  filled from the property's `Enum_info` labels. The `Property_ui` block
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
  enumeration values travel as their labels. A `scene.set_property` command
  (`config/editor/commands.json`, `doc/command_script.md`) with args
  `item`, `property`, `value` uses the same conversion, so startup scripts
  can author properties.
- D14 Serialization. glTF import and export continue to read and write the
  fields they already map (`Node` TRS, `Material` PBR fields) through the
  typed accessors; the file format does not change in this plan. Writing
  local values of non-glTF properties to node / material extras is future
  work (section 7).
- D15 Observers (R16). `Dependency_object::add_observer(property, callback)
  -> Observer_token` and `remove_observer(Observer_token)`; the token is a
  move-only RAII object that unsubscribes on destruction, and an object
  being destroyed invalidates its outstanding tokens (they become no-ops).
  Observers are stored in a per-object vector allocated on first
  subscription, and are invoked after the metadata callback and the virtual
  hook, with the same `Property_changed_args`, under the same batch rules as
  D9. The candidate users (the material preview, `Shadow_render_node` on
  a light's `cast_shadow`, the geometry graph transform-from-node) are
  settled in D21.
- D16 String conversion (R18). `to_string(const Property_value&) ->
  std::string` and `parse_value(Property_type, std::string_view, const
  Enum_info*) -> std::optional<Property_value>` in
  `erhe_property/property_string.hpp`. Vectors and quaternions are
  space-separated components, booleans are `true` / `false`, enumerations are
  their `Enum_info` labels, strings are verbatim. Both directions round-trip
  every value exactly for the non-float types and to the shortest float
  representation that round-trips for `float` (`std::to_chars`).
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

- D19 Item-side consequences through metadata callbacks. When a change of
  a property has a consequence inside the item library itself (not in the
  editor, which D11 covers), the owner registers the property with a
  `property_changed` callback (D4) that runs the consequence, so every
  writer - typed accessor, Properties row, `Property_set_operation`, MCP,
  `scene.set_property`, glTF import, `Property_set::apply` - reaches it
  and no call site has to remember a manual "notify" call. `Light` is the
  first user (section 4.3): the light-set re-resolve
  (`Scene_host::on_light_changed`) becomes the changed callback of every
  `Light` property, and `Light::notify_changed()` stops being public API.
  A callback only fires on an effective change (R4), so a write of the
  current value costs nothing downstream, and D9 batching still collapses
  a multi-property write into one callback per property.
- D20 Logarithmic sliders. `Property_ui` gains `bool logarithmic` (data
  only, like the rest of the block); the generic float slider row draws
  with `ImGuiSliderFlags_Logarithmic` and a `%.4f` no-rounding format when
  it is set. The light range / intensity and camera near / far rows are
  logarithmic today and lose usability as linear sliders over 0..20000.
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
    destroyed item deactivates it (D15). Material texture sampler edits
    (texture, offset, scale, wrap) are not properties and do not refresh a
    thumbnail, as before. The Properties window material preview keeps its
    per-frame render: it must also follow those sampler edits and the
    window size.
  - `Shadow_render_node`. The `cast_shadow` consequence already arrives
    through the D19 changed callback: `Scene_host::on_light_changed`
    invalidates the scene's `Light_set` and `Light_set::resolve` recomputes
    lazily. The node has no per-light state of its own; a direct observer
    would have to follow light registration to know which lights to
    subscribe to, which is what the `Scene_host` register / unregister
    hooks and the light set already do. No change.
  - Geometry graph `transform_from_node`. The driver's world transform is
    a derived cache, not a property (section 4.2); the direct
    `Trs_transform` writers (transform tool, physics writeback, animation)
    raise no property notification, and an ancestor move changes the
    driver's world transform with no property change on the driver at all.
    `update_live` keeps polling (the transform serial is the cheap compare).
    Revisit when the world transform is a computed read-only property
    (section 7).
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
    `string` properties are neither sources nor targets. Results convert
    per target type: `bool` is `value != 0`, `int` and enumeration round
    to nearest (an enumeration value outside its table is an error, the
    previous value stays), `float` truncates from `double`.
  - Store. `Effective_value_entry` gains `std::unique_ptr<Expression>`;
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
  - Undo (D11). `Property_set_operation` `before` / `after` become
    `std::optional<Local_state>`, so attaching, editing and removing a
    formula are undoable and undo of a value write restores the formula
    it replaced. `Property_set` (D17) and paste stay value bags: reading
    the local values of a driven property bakes its current result.
  - Rows (D12). A row whose first item is driven draws the formula text
    in place of the widget (commit on Enter or deactivation, one
    operation), a `=` label prefix, the error as a tooltip with a red
    frame, and `Source: expression` in the tooltip. The context menu
    gains `Edit as expression` (starts from the current value in formula
    form) and `Remove expression` (bakes the current result as the local
    value); `Reset to default` clears both.
  - MCP and command (D13). `get_item_properties` adds `expression` (the
    text or `null`) and `expression_error`; `set_item_property` and
    `scene.set_property` accept `expression` instead of `value`.
  - Serialization stays with D14: formulas are session state until the
    extras work lands (section 7).

- D23 Inherited flags (`visible`, `shadow_cast`, `lightmapped`).
  - What exists today. `Item_flags::visible` is a self bit; the only
    propagation is `Node_attachment::handle_node_update` /
    `handle_node_flag_bits_update` copying the node's bit onto each
    attachment, so a mesh is visible exactly when its node is and a hidden
    node does not hide its child nodes. `shadow_cast` and `lightmapped` are
    self bits on meshes with no propagation. `Item_flags::invisible_parent`
    is not visibility propagation: `Scene_root` sets it on the scene root
    node and the item tree skips that row and lists its children in its
    place. It stays as it is; the section 7 sentence that said it goes away
    was wrong.
  - Properties. `Item_base` registers `visible` (default `true`),
    `shadow_cast` (default `false`) and `lightmapped` (default `false`) as
    inherits-flagged `bool` properties with owner type 0, so every item
    type lists them (`for_each_property_of_type` matches owner 0 against
    every type). The semantics are the D8 ones: the closest ancestor with a
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
    baked into a local one. Every construction-site
    `enable_flag_bits(... | visible | ...)` drops the `visible` bit: with
    the default `true` the item is visible, and a local `true` on a mesh
    would stop the node's hide from reaching it (the point of the change).
    A construction site that today relies on the bit being absent to start
    hidden calls `hide()` explicitly. `shadow_cast` and `lightmapped`
    construction writes become `set_value(shadow_cast_property, true)`
    (a local value, the current per-mesh semantics; a group node's local
    value reaches only meshes without one).
  - Editor. The generic property rows (D12) draw the three checkboxes with
    source, reset and undo for free; the `Properties::item_flags` grid
    skips the derived bits. "Enable / Disable Lightmap (Recursive)" in the
    item context menu becomes one `Compound_operation` that sets the local
    value on the selected items and clears it on their descendants, so the
    subtree follows the ancestor afterward; `Item_set_flag_bits_operation`
    keeps serving `no_transform_update`. MCP `set_item_flags` rejects the
    three names with a message pointing at `set_item_property`;
    `get_item_properties` lists them.
  - Serialization (the node / mesh half of the D14 extras work). The
    `ERHE_node` extension gains `"properties"` (the node's) and
    `"mesh_properties"` (its mesh's) objects of local values, written with
    D16 `to_string` for every non-bridged, non-expression local value whose
    property has `serialize` (today: the three flags; tomorrow any
    store-backed node or mesh property), and read back through
    `parse_value` and `set_value` (a failed parse or an unknown name is
    logged and skipped). The exporter stops listing `visible`,
    `shadow_cast` and `lightmapped` in the `flags` arrays; the importer
    keeps reading them from old files (`visible` absent from a listed
    `flags` array sets local `false`; `shadow_cast` / `lightmapped` present
    set local `true`) so existing scenes load as before.
  - Tests. `src/erhe/item/test/test_item_visibility.cpp` (defaults, the
    derived bit follows local, inherited and tree-change values, `show` /
    `hide` / `set_visible` write local values, a derived bit in a
    `set_flag_bits` mask is dropped, copy re-derives the bits) and
    `src/erhe/scene/test/test_attachment_inheritance.cpp` (a mesh inherits
    from its node, follows a node hide and an ancestor hide, a local
    `true` on the mesh survives the node hide, moving the mesh between a
    hidden and a visible node notifies once with the right old value,
    `shadow_cast` set on a group node reaches meshes without a local
    value).

- D24 Sealing (WPF `Freezable.Freeze`, reversible).
  - What exists today. `Item_flags::lock_edit` is the user's edit lock
    (toggled in the Properties window Locks row, MCP `lock_items` /
    `unlock_items`, persisted by name in glTF) and the prefab editing model
    seals an instance subtree with it (`seal_instance_subtree`, together
    with the two viewport locks, never unsealed: a refresh re-clones). It
    is honored by hand-written checks: the Properties window disables its
    typed blocks and the name field, MCP `edit_material` refuses a locked
    material, delete skips locked items, and the transform and selection
    tools read the viewport locks. The generic property rows (D12),
    `Property_set_operation` (D11), MCP `set_item_property` and
    `scene.set_property` do not read it, so every registered property of a
    locked item, prefab interiors included, is editable through them.
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
  - Tests. `src/erhe/property/test/test_sealing.cpp` (every write entry
    rejected with the value unchanged and no notification, reads and
    inherited notifications through a sealed child, an installed
    expression still evaluating, `unseal` restoring writes, copy
    unsealed) and `src/erhe/item/test/test_item_sealing.cpp` (`set_lock_edit`
    / `enable_flag_bits` / `set_flag_bits` seal and unseal, a copy of a
    locked item is sealed, a copy of an unlocked one is not).

- D25 Style layer (WPF `Style` setters, `BaseValueSourceInternal.Style`).
  - What exists today. Neither candidate of section 7 is a property store:
    the graphics presets are a generated POD (`Graphics_preset_entry`
    inside `Graphics_settings`) that the Settings window edits in place,
    with no local-override concept to preserve, and the content library has
    no material preset at all - `add_default_materials` builds twelve
    metals by copying the same roughness, metallic, BxDF model and brushed
    metal traits into each. The graphics presets stay as they are until
    `Graphics_settings` is an item (section 7); the first user is the
    material library, and the layer is generic for every item type.
  - Library. `Property_style` (`erhe_property/property_style.hpp`) is a
    name plus a `Property_set` (D17), immutable after construction and
    shared as `std::shared_ptr<const Property_style>` (WPF seals a style
    once used; a changed preset is a new object re-applied by whoever owns
    the preset list). `Dependency_object::set_style(style)` /
    `get_style()` install one style per object; R3 becomes coerced > local
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
    would own the names.
  - Editor. `Style_set_operation` (item, style before, style after) is the
    undo step. The generic rows show `Source: style (<name>)` in the
    tooltip and the `*` prefix only for a local value, as before; the
    context menu gains `Paste Properties as Style` (the clipboard bag,
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
    override when the style is swapped or cleared. `Material` gains a
    name-only constructor that writes no local values; the
    `Material_create_info` constructor keeps writing every field as a
    local value (a full snapshot, which would shadow a style).
  - Tests. `src/erhe/property/test/test_style.cpp` (style over default and
    inherited, local over style, expression over style, `set_style`
    notifying only the changed non-local properties with the right old and
    new values and sources, swap and clear diffs, a style entry inherited
    by a child and stopping the ancestor's propagation, bridged property
    ignoring the style, copy carrying the style, sealed rejecting) and
    `src/erhe/primitive/test/test_material_style.cpp` (a styled material
    reads the traits, a local override wins, `get_values` bakes them,
    `operator==` sees the style).

## 4. Migration design

### 4.1 Material

The following `Material_data` fields become registered properties of
`Material` (owner type `Item_type::material`), with the current initializers
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

`Material_data` keeps only `texture_samplers`. `Material` exposes typed
getters and setters for every migrated field (`get_base_color()`,
`set_base_color()`, ...) implemented on the property store, so call sites
(`Material_buffer` upload, shader variant derivation, glTF import/export, the
material preview, MCP tools, tests) change from `material->data.base_color`
to the accessor and nothing else. `operator==(Material)` compares the
remaining `Material_data` and the property bags of D17.

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
derived cache, not a property.

`Animation_sampler::apply` keeps writing the `Trs_transform` directly; the
bridged properties read that storage, so playback and the property view
agree. Playback still overwrites the authored transform; the animated layer
that would preserve it is section 7 work.

R14 holds by construction: no per-frame path changed. The transform-update
statistics (`get_transform_update_stats`) after the migration match the
pre-migration run on the default scene.

### 4.3 Light

Every `Light` public field except `layer_id` becomes a registered property
(owner type `Item_type::light`) stored in the entry store, the Material way
(section 4.1), with the current initializers as defaults: `light_type`
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
becomes private and is only called from that callback. The manual
`notify_changed()` calls in the Properties rows and the MCP light tools
disappear with the rows and field writes that carried them.

`Light` exposes `get_light_type()` ... `set_cast_shadow()` accessors on the store
(the same shape as Material's) and the fields are removed, so every call
site (glTF import / export, scene builder, light buffer, shadow renderer,
light set, lightmap baker, light mesh, brush preview, MCP tools, debug
visualizations, icon set, sky renderer, example, tests) changes from
`light->color` to `light->get_color()` and nothing else. The photometric
helpers (`get_solid_angle`, `get_luminous_flux`, `set_luminous_flux`,
`get_effective_color`, `is_active`, `casts_shadow`) read the store.
`Light(const Light&, for_clone)` keeps copying only `layer_id`; the entries
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
and tests write through today; a bridged property reads and writes the
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
rows switch today), `z_near`, `z_far` (logarithmic sliders 0..1000) and
`infinite_z_far` (bool, `visible_when` perspective types). One
`make_projection_bridge<T>(member pointer)` helper in `camera.cpp` produces
each bridge.

`exposure` and `shadow_range` are the camera's own scalars with no
engineered representation, so they move into the entry store as ordinary
properties (logarithmic sliders 0..800000 and 1..1000) and the existing
`get_exposure()` / `set_exposure()` / `get_shadow_range()` /
`set_shadow_range()` accessors read and write the store. The clone
constructor keeps copying `m_projection`; the entries copy through D10.

`Properties::camera_properties` is deleted: every row it drew is a generic
row now.

## 5. Implementation phases

Each phase ends with its tests passing and, from phase 3 on, the editor
built and launched on the Metal build to verify the affected window.

### Phase 0 - library skeleton and core store

- `src/erhe/property/CMakeLists.txt`, `erhe_property/property_value.hpp`
  (D2), `enum_info.hpp/.cpp` (D2a), `dependency_property.hpp/.cpp` and `property_registry.hpp/.cpp` (D3,
  D4), `dependency_object.hpp/.cpp` (D5, D6, D7, D9, D10, D15),
  `property_string.hpp/.cpp` (D16), `property_set.hpp/.cpp` (D17),
  `property_log.hpp/.cpp`, `notes.md`.
- Wire into `src/erhe/CMakeLists.txt` before `item`; add `erhe::property` to
  `erhe_item`'s public links; re-run the Xcode configure script.
- Tests `src/erhe/property/test/` (gtest, same shape as `item/test`):
  registration and lookup, typed get/set/clear/default, read-only key,
  attached property on a foreign object, validate rejection, coerce, changed
  callback and virtual hook fire once per effective change, per-owner
  metadata override, local-value enumeration, copy semantics, change batch
  dedup, enumeration property typed round trip and label lookup with
  rejection of out-of-table integers (R9), observer add / remove / token
  destruction / object destruction ordering (D15), string round trip for
  every `Property_type` including enumeration labels and float edge cases
  (D16), `Property_set` read / apply / diff / equality (D17), and metadata
  flags and UI block surviving per-owner override (D4).

### Phase 1 - item integration and inheritance

- `Item_base : public erhe::property::Dependency_object`; `Hierarchy`
  overrides the two inheritance virtuals and calls invalidation from
  `set_parent` (D8).
- Tests in `src/erhe/item/test/test_properties.cpp`: inherited read through
  three levels, invalidation on ancestor set/clear, stop-at-local, reparent
  re-reads, clone keeps local values only.
- Update `src/erhe/item/notes.md`.

### Phase 2 - editor operation and generic UI

- `Property_set_operation` and `Property_set_apply_operation` (D11), the
  flag-driven `App_context::on_item_property_changed` hook, generic
  `dependency_properties` section in `Property_editor` / `Properties` with
  UI metadata, mixed-value multi-selection and copy / paste (D12), MCP tools
  and the `scene.set_property` command (D13).
- Verify with a temporary attached test property on `Node` registered by the
  editor (dropped at the end of the phase) so the UI and undo can be
  exercised before any item has migrated.

### Phase 3 - Material migration

- Section 4.1. Replace every `data.<field>` access for migrated fields;
  `material_properties` in the Properties window switches those rows to the
  generic section and keeps hand-written rows for the remaining fields.
- `double_sided`, `blending_mode` and `bxdf_model` register with
  `affects_draw_list_partition`, `normalmap_encoding` and `bxdf_model` with
  `affects_shader_variant`, so the D11 hook takes over the rebuilds and
  `changes_draw_list_partitioning` in `material_change_operation.cpp` is
  deleted. Every migrated field gets its `Property_ui` block from the
  hand-written row it replaces (ranges, color presentation for `base_color`
  and `emissive`).
- Verify: material edits in the editor with undo/redo, glTF material
  round trip (`doc/gltf-scene-roundtrip-plan.md` recipe), material set tests
  in `erhe_scene_renderer`.

### Phase 4 - Node migration

- D18 in the library (with tests), then section 4.2, with
  `src/erhe/scene/test/test_node_properties.cpp` covering reads, writes
  (world transform and serial), untyped access and property bags.
- Verify: MCP `set_item_property` on translation / rotation / scale with
  undo restoring the exact previous transform, `get_transform_update_stats`
  unchanged, existing `erhe_scene` tests. (VirtualCity is not used for
  measurements on Metal: a present-stall hang there can wedge the machine.)

### Phase 5 - docs

- Final `src/erhe/property/notes.md`, `src/erhe/item/notes.md`, and the
  editor `notes.md` section for the generic property rows and MCP tools.
  This plan's "Status" line moves to "done" and section 7 keeps only what is
  still open.

### Phase 6 - Light and Camera migrations

- D20 in the library and the generic rows, then section 4.3 (`Light`
  accessors, D19 callback, call-site migration, `light_properties` reduced
  to derived rows) and section 4.4 (`Camera` bridged projection
  properties, store-backed exposure and shadow range, `camera_properties`
  deleted).
- Tests: `src/erhe/scene/test/test_light_properties.cpp` (defaults match
  the previous initializers, typed and untyped access with enumeration
  labels, the changed callback fires once per effective change and not for
  a same-value write, clone copies the values, `Property_set` round trip)
  and `test_camera_properties.cpp` (bridged reads see `projection()`
  writes and bridged writes are visible through `projection()`, enumeration
  label round trip, `clear_value` restores the default, exposure / shadow
  range through the store, clone).
- Verify (section 8): `get_item_properties` / `set_item_property` on a
  light and on `Camera A`'s camera attachment with undo, a light `type`
  change re-resolving the light set (the light switches between the
  directional and spot shadow passes), glTF export / import round trip of a
  light and a camera, and `capture_screenshot` of the Properties window
  for a selected light and camera.

### Phase 7 - observer users

- D21: the any-property `add_observer` overload with a test in
  `test_observers.cpp`, then `Thumbnails` slot subscription and stale
  re-render.
- Verify (section 8): `edit_material` on a material shown in the Hotbar or
  Inventory refreshes its thumbnail without hovering (`capture_screenshot`
  before and after), `undo` refreshes it back, and the existing
  `erhe_property` tests pass.
- Found during verification, fixed alongside: material thumbnails were
  solid magenta on the Metal build. The thumbnail texture is mipmapped and
  sampled with mipmap filtering at a fraction of its size, and
  `Material_preview::render_preview(texture, material)` never generated
  the levels below the rendered one (`Brush_preview` did), so the slot
  sampled uninitialized memory. It now generates them after the render.

### Phase 8 - expressions and bindings

- D22 in the library (`expression.hpp/.cpp`, the entry store and
  notification changes, `Item_host::find_hosted_item`,
  `Item_base::resolve_expression_object`, `Scene_host` name lookup,
  `Node::handle_transform_update` invalidation) with
  `test_expressions.cpp`: syntax rejection, constant and reference
  formulas, component selection and broadcast, every target type
  conversion, push on source change and on `invalidate_dependents`, lazy
  resolution, replace / keep / clear semantics, destruction in both
  orders, self and two-object cycles, copy, bridged target, batches, and
  the `Local_state` round trip.
- Editor: `Property_set_operation` over `Local_state`, the formula row
  and context menu entries, MCP and `scene.set_property` arguments.
- Verify (section 8): `set_item_property` with `expression` on a light's
  `intensity` referencing a node's `translation.y`, `set_node_transform`
  on that node changing the light's value in `get_item_properties`, a
  driven node `translation` following another node, `undo` restoring the
  previous local state, and a `capture_screenshot` of the formula row.
- Found during verification, fixed alongside: the editor (and the example,
  smoke and the graph / assets test mains) never called
  `erhe::property::initialize_logging()`, so the first error the library
  logged in a live session (a rejected formula) dereferenced a null
  logger. Every binary that initializes `erhe::item` logging now
  initializes the property logger first.

### Phase 9 - inherited flags

- D23: the `Item_base` properties, derived bits, `Node_attachment`
  inheritance parent and `Node` attachment snapshots, then the writer
  migration (construction masks, `set_visible` / `show` / `hide`,
  `Node_raytrace`), the editor pieces (flag grid, lightmap context menu,
  MCP `set_item_flags`), and the `ERHE_node` `properties` /
  `mesh_properties` serialization with the old-file flag read path.
- Tests: `test_item_visibility.cpp`, `test_attachment_inheritance.cpp`,
  and the existing item / scene / gltf tests.
- Verify (section 8): `set_item_property` `visible` `false` on a node with
  children hiding the subtree's meshes in a `capture_screenshot`, a child
  node's local `true` showing it again, `undo` in order; `get_item_properties`
  reporting `inherited` on a child; `shadow_cast` `true` on a group node
  reaching a child as `inherited`; `export_gltf` with `editor_state: true`
  followed by `import_gltf` giving identical local values and the same
  inherited sources; a legacy-format file with `visible` missing from a
  node's `flags` loading that node hidden.
- Found during implementation, fixed alongside: `Node_attachment::set_node`
  ran on a destroyed attachment when the old node's attachment list was the
  last owner (`Node::~Node` and `Node::detach` call it through a raw pointer
  and `handle_remove_attachment` erases the owning `shared_ptr` mid-call);
  it was harmless while nothing after the erase touched the object, and
  the snapshot apply did. `set_node` now holds a `shared_ptr` to itself for
  the duration.
- Behavior notes: `grid_tool` "Add Grid" previously created an invisible
  grid by omitting the bit and now creates a visible one (the checkbox in
  the grid window still toggles it); `Quad_view` hides its rendertarget
  node at construction, as the omitted bit did before; the camera
  `properties` object repeats `exposure` / `shadow_range` next to the
  native `ERHE_camera` fields (both import to the same value).

### Phase 10 - sealing

- D24 in the library (`seal` / `unseal` / `is_sealed`, the rejections,
  `bool` returns) with `test_sealing.cpp`, then `Item_base` flag sync with
  `test_item_sealing.cpp`, then the rows, the operations' warning, MCP and
  the command.
- Verify (section 8): `lock_items` on a node followed by
  `set_item_property` refused and the value unchanged, `unlock_items`
  followed by the same write succeeding, `get_item_properties` reporting
  `sealed`, a prefab instance interior node refusing the write while
  `visible` `false` on the instance root still hides it, and
  `capture_screenshot` of the disabled rows for a locked node.
- Found during the design: the generic rows, `Property_set_operation`, MCP
  `set_item_property` and `scene.set_property` never read `lock_edit`, so
  every registered property of a locked item (prefab interiors included)
  was editable through them; the seal at the write funnel closes that for
  every writer at once.

### Phase 11 - style layer

- D25 in the library (`Property_style`, `set_style`, the precedence and
  inheritance changes, `Value_source::style`) with `test_style.cpp`, then
  `Style_set_operation`, the rows, MCP, and the material library's
  `Brushed metal` style with `test_material_style.cpp`.
- Verify (section 8): `get_item_properties` on a default metal reporting
  `style` `Brushed metal` and `roughness` with source `style`,
  `set_item_property` `roughness` on it reporting `local` while
  `metallic` stays `style`, `set_item_style` from another item and
  `clear_item_style` changing the sources with `undo` restoring them, the
  rendered metals unchanged from before the migration
  (`capture_screenshot`), and `capture_screenshot` of the Properties
  window for a styled material showing only the local row with the `*`
  prefix.
- Found during verification, fixed alongside: the first `set_item_style`
  iterated the `entries()` of a temporary `Property_set` (destroyed before
  the loop ran) and reported no usable values; the bag is a local now.

## 6. Out of scope

Kept out deliberately, as they are the WPF parts that serve XAML UI rather
than a scene model: templates and triggers, `UncommonField`, dispatcher
thread affinity, the attached-property browsable attributes, and two-way
bindings (they exist for UI controls writing back to
a model; erhe's ImGui windows are immediate-mode and read the item
directly). Expressions and one-way bindings are D22, sealing is D24, the
style layer is D25.

## 7. Future work

- Animated value layer (WPF `SetAnimatedValue` / `GetAnimationBaseValue`):
  a layer between coerced and local in R3, stored as a second optional in
  `Modified_value` (D5), set and cleared by `Animation_sampler::apply` and
  animation stop so playback never overwrites the authored local value and
  keying (`doc/animation-keyframing-plan.md`) reads the local value as the
  authored pose.
- Expression serialization: the formula text of a driven property (D22)
  written to glTF extras and restored on import, with the D14 extras work;
  until then formulas are session state.
- Style users beyond the material library (D25): the graphics presets once
  `Graphics_settings` is an item with registered properties, per-instance
  prefab overrides once `doc/gltf-prefabs-plan.md` phase 6 takes them on,
  and a preset library that owns style names so a style can be written to
  glTF by name.
- Read-only computed properties through `Property_key<T>` (R6): world
  transform, world bounds, child count, exposed for MCP, the Properties
  window and the expression variables of the value-provider item.
- Layout per-child hints as attached properties (WPF `DockPanel.Dock`,
  `Grid.Row`): `Layout` registers alignment and margin as attached
  properties and reads them from each child node, so a child carries its
  hint without `Node` knowing about layouts. First attached-property user
  (R7).
- Further item migrations, in priority order and each reusing the phase 3
  recipe (`Light` and `Camera` are done, phase 6): `Layout` (type, axis,
  volume), `Grid`, physics settings (mass, friction, restitution),
  `Brush_placement`.
- Graph nodes (geometry, texture and shader graphs) as dependency objects:
  node parameters become properties, their editors reuse the generic rows
  (D12), and the value-provider item can drive them from scene items.
- Editor per-item state as attached properties registered by the editor:
  item tree expansion, sheet-window formulas.
- glTF extras serialization of local values for properties without a native
  glTF field (D14), honoring the `serialize` flag (D4): done for nodes and
  meshes in D23 (`ERHE_node` `properties` / `mesh_properties`); lights,
  cameras and materials still write only their native fields.
- Animation channels targeting arbitrary properties (not only node TRS),
  which becomes possible once `Animation_channel` stores a
  `Dependency_property` index instead of `Animation_path`.

## 8. Verification recipe (macOS, Metal build tree)

- Configure once after adding files: `bash scripts/configure_xcode_metal.sh`
  (or `cmake .` inside `build_xcode_metal` when only CMake lists changed).
- Build: `cd build_xcode_metal && xcodebuild -project erhe.xcodeproj -target
  <target> -configuration Debug -parallelizeTargets -quiet`, targets
  `erhe_property_tests`, `erhe_item_tests`, `erhe_scene_tests`,
  `erhe_primitive_tests`, `erhe_scene_renderer_tests`,
  `erhe_scene_renderer_gpu_tests`, `editor`. Run the test executables one at
  a time from `build_xcode_metal/src/erhe/<lib>/test/Debug/`.
- Editor: from the repo root `ERHE_MCP_PORT=3743
  build_xcode_metal/src/editor/Debug/editor &`, wait for
  `MCP server: listening` in `logs/log.txt`, then drive it with
  `python3 scripts/mcp_call.py <tool> '<json>'`. The checks that passed at
  the end of phase 4: `get_item_properties` / `set_item_property` on
  `Camera A` (node) and `Titanium` (material, `get_scene_materials` gives
  ids), `undo` restoring the exact values, `edit_material` producing a
  `Property_set_apply_operation` on the undo stack (`get_undo_redo_stack`),
  `export_gltf` with `editor_state: true` followed by `import_gltf` giving
  identical property values on the imported copy, and
  `get_transform_update_stats` unchanged after node property writes.
  The checks that passed at the end of phase 6 (`get_scene_lights` /
  `get_scene_cameras` give the attachment ids; `list_scenes` gives the
  scene name the light / camera queries need): `set_item_property` of
  `light_type` to `Spot` on a directional light moving it to the spot
  shadow slot in `get_scene_lights` and `undo` restoring the directional
  slot order, `z_far` / `intensity` writes with `undo`, and `export_gltf`
  with `editor_state: true` followed by `import_gltf` giving identical
  camera properties and identical light properties except `temperature`,
  which the exporter bakes into the exported color (KHR_lights_punctual
  has no temperature). The imported lights appear in `get_scene_lights`
  only after the import settles, a query in the same second lists the
  originals only.
  The check that passed at the end of phase 7: `edit_material` on a
  material listed in the Inventory window (`get_scene_materials` gives the
  ids; the window must be open and showing that material) changes its
  thumbnail in a `capture_screenshot` taken two frames later without the
  mouse over it, and `undo` changes it back.
  The checks that passed at the end of phase 8 (`get_scene_lights` /
  `get_scene_nodes` give the ids and names): `set_item_property` with
  `expression` `{cube/translation.y} * 10` on `Directional light 0`'s
  `intensity` reporting `source` `expression` and the evaluated value in
  `get_item_properties`; `set_node_transform` on `cube` changing that
  value; `{cube/translation} + 1` on the `cuboctahedron` node's
  `translation` following the next `set_node_transform` of `cube`; a
  reference to a missing item reporting `expression_error` and keeping the
  last value; the self reference `{intensity} + 1`, the syntax error
  `1 +` and a string target rejected with a message and nothing queued;
  `undo` restoring the previous local state in order (the node's stored
  translation, the light's stored intensity) and `redo` reinstalling the
  formula; and `capture_screenshot` with the light selected showing the
  `= Intensity` row with the formula text.
  The checks that passed at the end of phase 9 (`reparent_node` builds the
  subtree; the default scene is flat): `set_item_property` `visible`
  `false` on `dodecahedron` with `icosahedron` reparented under it removing
  both from `capture_screenshot` (the cube and cuboctahedron are outside
  the default view, use objects that are in it), `get_item_properties`
  on the child reporting `source` `inherited` on the node and its mesh,
  `visible` `true` on the child bringing it back, two `undo`s restoring the
  states in order, `shadow_cast` `true` on the parent reported `inherited`
  on the child, `export_gltf` with `editor_state: true` writing
  `ERHE_node` `properties` / `mesh_properties` (`{"visible":"false"}`,
  `{"shadow_cast":"true","lightmapped":"true"}`) and `import_gltf` giving
  the same local values and sources, a GLB rewritten without the
  `properties` objects and with `visible` listed in every `flags` array
  except one node importing that node with local `false` and its mesh
  `inherited`, `set_item_flags` with `visible` rejected with the
  `set_item_property` hint, and `capture_screenshot` with a hidden empty
  node selected showing the `* Visible` row unchecked under its transform
  rows. The generic rows sit below the Mesh section for a mesh-carrying
  node, so select an empty node for the screenshot.
  The checks that passed at the end of phase 10: `lock_items` on `cube`
  followed by `set_item_property` `visible` refused with the `unlock_items`
  hint and `get_item_properties` reporting `sealed: true` and the value
  unchanged, `unlock_items` followed by the same write queued and applied,
  `instantiate_prefab` of `res/example/models/Box.gltf` (poll
  `get_scene_nodes` until the instance nodes appear) with a write to an
  interior node refused while `visible` `false` on the instance root is
  reported `inherited` on the interior nodes and mesh, and
  `capture_screenshot` with a locked node selected showing its property
  rows greyed out.
  The checks that passed at the end of phase 11 (`get_scene_materials`
  gives the material ids and names): `get_item_properties` on `Titanium`
  reporting `style` `Brushed metal` with `roughness`, `metallic`,
  `bxdf_model` and `use_aniso_control` sourced `style` and `base_color`
  `local`; `set_item_property` `roughness` making it `local` while the
  others stay `style`; `set_item_style` with `source_item_name` `Gold`
  reporting the `Gold` style with `base_color` as its only entry, the
  traits falling to `default` and Titanium's local base color surviving;
  `clear_item_style`; two `undo`s restoring `Brushed metal` in order (the
  scene builder's insert compound sits right below on the stack: do not
  undo a third time); the rendered metals identical to the phase 9
  screenshot; and `capture_screenshot` with `Titanium` selected showing
  `* Base Color` as the only starred row.
- `capture_screenshot` works on the Metal swapchain (the same one-frame
  arm-then-collect protocol as the Vulkan windowed build), so the visual
  check of the Properties rows is `capture_screenshot` with a `path` in
  the scratch directory, cropped to the Properties window; system screen
  capture tools are not used (they trip the endpoint security agent).
- The generated Xcode project lists sources from the CMake files; an
  undefined-symbol link error after adding a file means the project is
  stale, re-run the configure script.
