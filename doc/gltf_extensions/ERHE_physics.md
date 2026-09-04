# ERHE_physics

## Scope

**Node** extension, on a node carrying a `KHR_physics_rigid_bodies` body
(motion, collider or trigger). Optional (`extensionsUsed` only).

## Dependencies

Meaningful only together with `KHR_physics_rigid_bodies`; it refines the
body that extension describes.

## Overview

Carries erhe rigid-body state `KHR_physics_rigid_bodies` has no carrier
for:

- `motion_mode`: erhe's four motion modes. KHR has a single `isKinematic`
  bool, which cannot distinguish `kinematic_non_physical` from
  `kinematic_physical`, and static bodies have no motion object at all.

Friction, restitution, linear and angular damping, wind receptivity and
density are not here: they belong to the shared physics material the
body's `KHR_physics_rigid_bodies` collider references (its erhe-only
values ride `ERHE_scene` `physics_materials`), and a body without a
material behaves like one with the material defaults.

The field is written for every exported body so the round-trip is exact.

## JSON layout

```json
{
    "motion_mode": "dynamic"
}
```

- `motion_mode`: one of `static`, `kinematic_non_physical`,
  `kinematic_physical`, `dynamic`.

## Load semantics

Applied onto the rigid-body create info the `KHR_physics_rigid_bodies`
import produces, before the body is created; the present field overrides
the KHR-derived value.

## Schema

[schema/ERHE_physics.schema.json](schema/ERHE_physics.schema.json)
