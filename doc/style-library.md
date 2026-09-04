# Style library

Styles are items of the content library's Styles category: a style is a
named holder of property values of one item class (a Material style holds
`base_color`, `roughness`, texture slots, ...), selectable, edited in the
Properties window like any item, assigned to an item through the item's
`style` property, and saved with the scene. This document is the design
record; the layer mechanics are `doc/property-system.md` D25 (the style
layer) and D30 (secondary owner types), the library reference is
`src/erhe/property/notes.md`, and the folder counterpart is
`doc/content-library-folders.md`.

## 1. Requirements

- R1 Style item. A `Style` is a library item with a target owner type (the
  item class it styles). Its own local values are the style: adding,
  editing and removing them is the D12 row machinery (Add Property,
  Reset to default, Remove Property, undo), with the same qualified names
  a folder of that category uses (`Material.roughness`).
- R2 Live edit. A change of a style's value reaches every item using the
  style at that moment: each user without a local value of its own for
  that property is notified with the old and new effective value, and the
  editor consequences of D11 run for it.
- R3 Assignment. Every item has a `style` property (a D28 object
  reference) shown as a row with the picker and the drag target; the
  candidates are the scene's styles whose target owner type the item's
  class (or, for a folder, its category) descends from. Clearing the row
  removes the style. "Paste Properties as Style" and the MCP
  `set_item_style` create a style item in the scene's Styles folder from
  the copied values and assign it, in one undo entry.
- R4 Folders. A content-library folder can carry a style like any item;
  the style's values are the folder's effective values and so reach the
  entries below it through inheritance (R4 of the folders record).
- R5 Persistence. A scene save keeps every style (name, target class,
  values) and the style each material and each library folder uses, by
  name. A load recreates the styles before anything references them.
- R6 Defaults. The default metals share one "Brushed metal" style item in
  the scene's Styles folder, so the library shows what the metals have in
  common and a scene keeps it across a save.

## 2. Design

- D1 Style source. The style layer of a `Dependency_object` (D25) reads
  the LOCAL values of another `Dependency_object`, its style source:
  `set_style(std::shared_ptr<const Dependency_object>)`. `Property_style`
  stays as the library's plain named source (a `Dependency_object` filled
  from a `Property_set`, for tests and non-item users). A source keeps the
  list of objects using it (registered by `set_style`, by the copy of a
  user, and dropped by a user's destructor); when a source's local layer
  changes, `notify` forwards the change to every user without a local
  value of that property (R2), computing each user's old value from the
  old style value (or the user's inherited / default value when the
  source held none) and its new value from the user's effective value.
- D2 Style item. `editor::Style` (`content_library/style.{hpp,cpp}`) is an
  `erhe::Item` whose secondary owner type (D30) is its target class, so the
  Add Property picker offers that class's properties by qualified name and
  the values live in the item's own store. `erhe::Item_type::style` is its
  type bit; the library's `styles` category folder carries it. A style is
  clonable (its values copy), and "Copy to Scene" copies it like a
  material.
- D3 The `style` property. `Item_base::style_property` is a bridged (D18)
  object-reference property registered on `Item_base`: `get` is the
  object's style source, `set` is `set_style`, validated so the referenced
  object's secondary owner type (its target) is on the object's own owner
  chain or equals the object's own secondary owner type (a folder taking a
  style of its category). `reference_item_types` is the style bit, so the
  row's picker and drop target take styles only. The property carries the
  draw-list and shader-variant flags, so the D11 hook rebuilds the draw
  lists after an assignment through a `Property_set_operation`;
  `Style_set_operation` (the operation the paste and MCP paths queue)
  keeps its per-property consequence loop over both styles' values.
- D4 Wire format. `ERHE_scene` gains `styles`: an array of `{"name",
  "target", "properties"}`, `target` the owner type name (`"Material"`),
  `properties` the D14 map of the style's local values by qualified name.
  `ERHE_material` gains `style`, the name of the material's style item;
  a `library_folders` entry gains `style` the same way. A load creates the
  styles first (one attach operation per style, before the folders
  operation), then the folders operation and a material style operation
  assign them by name; an unknown name logs a warning and assigns nothing.
- D5 Defaults. `add_default_materials` makes the "Brushed metal" style
  item in the library's Styles folder and assigns it to each metal.

## 3. Verification

Headless, over `scripts/mcp_call.py` on a fresh editor:

1. `get_item_properties` on `Brushed metal` (the Styles folder entry) lists
   `Material.roughness` as local; on `Copper`, `roughness` reads with
   source `style` and `style` reads `Brushed metal`.
2. `set_item_property` `Material.roughness` `0.9 0.9` on the style:
   `Copper`'s `roughness` reads `0.9 0.9`, still source `style`; `undo`
   restores.
3. `set_item_property` `style` `null` on `Copper`: `roughness` reads the
   default; setting `style` back by name restores.
4. `save_scene` and reopen: the style item, its values and `Copper`'s
   `style` are back; `close_scene` is clean.

Interactive: select the style in the Scene Hierarchy's Styles folder,
edit a value and watch the metals change; drag the style onto a folder;
Paste Properties as Style from a material; Ctrl+Z after each.
