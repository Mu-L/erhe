# ERHE_scene

## Scope

**Scene** extension. Optional (`extensionsUsed` only).

## Overview

Marks a file as an erhe-authored scene and carries per-scene editor state.
Its presence in `extensionsUsed` is the discriminator between "open as a
full erhe scene" and "import as an asset": scene saves always write it,
plain interchange exports never do.

- `ambient_light`: scene ambient light color (RGBA; erhe #237).
- `enable_physics`: whether the scene owns a physics world.
- `settings` (optional): per-scene overrides of editor-global settings
  (erhe #239) as a `Scene_settings` JSON object (the erhe_codegen schema in
  `src/editor/scene/definitions/scene_settings.py`; each absent / null
  field means "use the editor-global default"). Omitted entirely when no
  override is engaged.
- `styles` (optional): the content library's style items
  (`doc/style-library.md` D4): `name` and `properties` (the style's local
  values as a name to text map, the form of `ERHE_node` `properties`,
  keyed by qualified name such as `Material.roughness` or `Light.color`;
  omitted when empty). A `target` member of older files is ignored. Loaded before anything that names a style. Omitted when the
  library has no styles.
- `physics_materials` (optional): one entry per `KHR_physics_rigid_bodies`
  `physicsMaterials` entry, by index (the KHR entries carry no name):
  `name` and `properties` (the material's local property values as a
  name to text map, the form of `ERHE_node` `properties`; the registered
  properties of `Physics_material` by name, so the erhe-only
  `linear_damping`, `angular_damping`, `wind_receptivity` and `density`
  have their carrier here). The map is the material's complete local
  set: on load, a value the KHR entry carried (friction, restitution, a
  combine mode) that the map does not name is cleared again, so a
  material that inherits it from its folder or style still does after a
  reload. Omitted when the file has no physics materials.
- `collision_filter_names` (optional): the names of the
  `KHR_physics_rigid_bodies` `collisionFilters` entries, by index, so the
  content library's collision filters keep their names across a save and
  reload. Omitted when the file has none.
- `library_folders` (optional): the content library's folders below its
  category folders (`doc/content-library-folders.md` D5), parents before
  their subfolders. Each entry has `path` (slash-separated from the
  library root, starting with the category folder's name), `properties`
  (the folder's local property values as a name to text map, the form of
  `ERHE_node` `properties`; omitted when empty) and `items` (the names of
  the entries directly in the folder; omitted when empty) and `style`
  (the name of the style item the folder uses; omitted when none). An entry no
  folder names loads into its category folder. Omitted when the library
  has no folders.

## JSON layout

```json
{
    "ambient_light": [0.1, 0.1, 0.12, 1],
    "enable_physics": true,
    "settings": {
        "post_processing": false,
        "clear_color": [0, 0, 0, 1]
    },
    "styles": [
        {"name": "Brushed metal", "properties": {"Material.roughness": "0.34 0.2", "Material.metallic": "1"}}
    ],
    "physics_materials": [
        {"name": "Rubber", "properties": {"restitution": "0.8", "linear_damping": "0.1"}}
    ],
    "collision_filter_names": ["Debris"],
    "library_folders": [
        {"path": "Materials/Metals", "properties": {"visible": "false"}, "items": ["Gold", "Copper"], "style": "Brushed metal"},
        {"path": "Brushes/Platonic Solids", "items": ["Cube", "Octahedron"]}
    ]
}
```

## Load semantics

Consumed by the Open-Scene path when constructing the `Scene_root`.
Importing a file that carries `ERHE_scene` as an ASSET into an existing
scene ignores everything but `library_folders` - an import must not change
the target scene's settings, while the imported library entries keep their
folders. In both paths the folders are placed after every library entry of
the file exists; a folder that already exists keeps its property values,
and a listed item name the category does not hold is logged and skipped.

## Schema

[schema/ERHE_scene.schema.json](schema/ERHE_scene.schema.json)
(the `settings` object is validated by the erhe_codegen `Scene_settings`
schema, not duplicated here)
