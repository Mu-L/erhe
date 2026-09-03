# ERHE_node

## Scope

**Node** extension. Optional (`extensionsUsed` only).

## Overview

Carries the erhe Item state of a node that core glTF cannot express:

- `flags`: the node's persistent Item flags as a name list (see
  [flags.md](flags.md)). Migrates the legacy `erhe_flags` node extras
  (which carried only `exclude_from_prefab`); the extras remain parsed for
  older files, this extension wins when both are present.
- `properties`: the node's local property values as a name to text map
  (`doc/property-system.md` D14): the registered properties of `Node`
  by name, and attached properties (D3) by their qualified
  `<owner>.<name>`, such as the `Layout.*` per-child layout hints
  (`ERHE_layout` names the layout itself). Enumerations travel as their
  labels.
- `mesh_flags` (optional): the persistent Item flags of the node's mesh
  attachment. They ride the node because core glTF meshes have no erhe
  payload of their own and erhe `Mesh` attachments are per node while glTF
  meshes are shareable.

## JSON layout

```json
{
    "flags": ["content", "visible", "show_in_ui"],
    "properties": {"Layout.align_y": "Stretch", "Layout.margin_min": "0.1 0 0"},
    "mesh_flags": ["content", "visible", "shadow_cast", "id", "show_in_ui"]
}
```

## Load semantics

The listed set is applied exactly: listed persistent flags are enabled,
unlisted persistent flags are disabled, unknown names are ignored. This
replaces the old fixed default flag sets the loader used to assign.

## Schema

[schema/ERHE_node.schema.json](schema/ERHE_node.schema.json)
