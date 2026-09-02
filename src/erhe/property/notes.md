# erhe::property

## Purpose

A port of the WPF dependency-property system (`DependencyProperty`,
`PropertyMetadata`, `DependencyObject`, `EffectiveValueEntry`,
`DependencyPropertyKey`, the `Inherits` metadata flag) restricted to the value
types erhe items need. `erhe::Item_base` derives from `Dependency_object`, so
every scene item carries a property store. Plan and design record:
`doc/property-system-plan.md`.

## Key types

- **`Property_value`** - `std::variant<bool, int, float, glm::vec2, glm::vec3,
  glm::vec4, glm::quat, std::string, Enum_value>`; `Property_type` enumerators
  are the variant indices. `Enum_value` wraps the integer of a C++ enumeration
  so generic code can tell an enumeration from an `int`.
- **`Enum_info`** - immutable enumerator table (label, value) referenced by
  every enumeration property of that C++ type; one `static const` table per
  enumeration, next to its `c_str()`.
- **`Dependency_property`** - one registration record: global index, name,
  type, owner type (`Item_type` bits), read-only / attached flags, validate
  callback, default `Property_metadata` and per-owner-type overrides.
- **`Property_metadata`** - default value, property-changed and coerce
  callbacks, `inherits`, `Property_flags` (what a change affects),
  `Property_ui` (how the Properties window draws the row) and an optional
  `Property_bridge`. Flags and UI are data the editor reads; the library
  never acts on them.
- **`Property_bridge`** - `get` / `set` callbacks that store a property in
  the object's own member instead of the entry store. A bridged property is
  always `Value_source::local`, never inherits, is coerced on every read,
  and clearing it writes the default. `Node`'s translation / rotation /
  scale are bridged onto its `Trs_transform`.
- **`Property_registry`** - function-local static registry; registration
  happens from static members of owning classes, lookups by index, by
  (owner type, name) or by (type bits, name) (`find_for_type`, what an
  object of that type means by the name), enumeration of the non-attached
  properties of a type.
- **`Property<T>` / `Property_key<T>`** - typed handles. `T` is a variant
  alternative or any C++ enumeration. `Property_key<T>` is the write
  permission for a read-only property.
- **`Dependency_object`** - the per-object store: sparse vector of entries
  sorted by property index, binary-searched; an entry exists only for a
  property with a local value and holds that value plus its coerced value
  when the coerce callback changed it.
- **`Observer_token`** - RAII subscription to one property on one object,
  or to every property of it (`add_observer` without a property).
- **`Expression`** (`expression.hpp`) - a compiled formula driving one
  property (`doc/property-system-plan.md` D22): comma-separated tinyexpr
  expressions, one per component, with `{[object/]property[.x|.y|.z|.w]}`
  references; `set_expression` installs it as the local layer,
  `Value_source::expression` reports it, `Local_state` (value or
  `Expression_text`) is the exact local layer for undo. References resolve
  lazily through `resolve_expression_object` (Item_base: `""` self, `..`
  parent, a name via `Item_host::find_hosted_item`); a resolved source keeps
  a dependent list and re-evaluates its targets from `deliver`, or from
  `invalidate_dependents` when its storage changed outside `set_value`
  (`Node::handle_transform_update`). Beyond tinyexpr's own functions:
  `min`, `max`, `clamp`, `lerp`, `step`, `smoothstep`, `sign`, `round`,
  `frac`, `deg`, `rad`, and the logic set `lt le gt ge eq ne not and or
  select`. Depends on `tinyexpr` (private).
- **`Property_set`** - sorted bag of (property, value): local values of an
  object, clipboard payload, diff.
- **`property_string.hpp`** - `to_string` / `parse_value` for every type.

## Value precedence and callbacks

Effective value = coerced(base), base = local > inherited > default. The
coerced value of a local value is stored in the entry and refreshed by
`set_value` and `coerce_value`; a property without a local value is coerced
on every read. `set_value` runs validate (type check, enumeration table,
callback), stores, coerces, then notifies when the effective value or its
source changed: metadata `property_changed`, the virtual
`on_property_changed`, then observers. `Change_batch` queues notifications on
an object and delivers one per property when the outermost batch ends, with
the value before the batch and after it.

## Inheritance

`inherits` metadata makes a property without a local value read the closest
ancestor's effective value through two virtuals the object provides
(`get_inheritance_parent`, `for_each_inheritance_child`; `erhe::Hierarchy`
implements them, `erhe::scene::Node` adds its attachments as children and
`Node_attachment` names its node as parent). Inherited values are not cached: a read walks up until an
ancestor with a local value. A set or clear on an inherits property notifies
every descendant without a local value, stopping at descendants that have
one. A tree change uses `capture_inheritance_snapshot` before and
`apply_inheritance_snapshot` after so the subtree's notifications carry the
right old values.

## Metadata resolution

`Dependency_property::get_metadata(owner_type_bits)`: an override whose owner
mask equals the bits wins, else the last-registered override sharing a bit,
else the default metadata. Override lists are short so this is a linear scan,
no cache.

## Copy semantics

Copying a `Dependency_object` copies its entries (local and coerced values).
Observers, pending batches and inherited state are not copied.

## Threading

The registry is written during static initialization and read lock-free
afterwards. Property values are item state guarded by the item's host mutex,
like the rest of the item.

## Tests

`test/` (gtest): registry and overrides, every value type, validate, coerce,
change notifications and batching, inheritance through a `Test_object` tree,
observers, enumerations, string conversion, property sets, bridged storage.
