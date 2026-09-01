# Draw list material set - planning context

Background for `doc/draw_list_material_set_plan.md`: why the work exists, what
has already landed, and how much of the plan has been reviewed. None of it is
needed once implementation starts - the plan is self-contained for that.

## Status and handoff (2026-08-31)

**Phases 1 and 2 have landed (`b653c5fdb`, `64984f81f`); phase 3 is next.** The
V3 regression test exists and is **red**, reproducing the reported asymmetry
over cached records: with mesh A assigned first the two records read A=14 B=0,
and with the order reversed A=0 B=15. `Multi_copy_buffer`, `Material_slot` and
`Material_set`'s membership half exist and are unit-tested, but nothing uses
them: `Material::material_buffer_index` is still the shared mutable field and
no frame behaves differently. Phase 3 gives `Material_set` its GPU half and
gives the sets their owners.

**Four things this plan depended on have already landed**, so read the tree
before trusting an older reading of the sections below:

| Commit | What it changed for this plan |
| --- | --- |
| `fe78df850`..`f81e01e63` | The tools scene root, its content library, the tool composition pass and the ID renderer's tool pass are **gone** - nothing had put a mesh in the tool layer since the gizmo became debug-rendered (`a32105a1d`). D6 no longer has a tools clause; D0/D1c/D3 no longer have a tools case. `Hover_entry::tool_slot` was kept at the user's request and has no producer. |
| `fbaaa33a4` | `Mesh::get_mutable_primitives()` removed - **the second half of R4 is satisfied ahead of the plan**. No caller needed it: all eleven read, or mutate the `Primitive` pointee through the shared_ptr, which a `const Mesh_primitive&` allows. |
| `c6ab99db4` | `Scene_pass_resources` extracted from `Forward_renderer` (`scene_pass_resources.{hpp,cpp}`): camera / glyph / joint / light / material buffers, texture heap, lightmap+DDGI state, `begin_pass` / `end_pass`, and `Base_render_parameters` at namespace scope. **D2 and D5 now edit `Scene_pass_resources`, not `Forward_renderer`.** |

**Review state, by part.** The core membership analysis (section 1 below, and
D1, D1a, D1d in the plan) has seven independent review rounds behind it, and
the seventh found no blocking or major defect. Everything else is newer and
**unreviewed**: the two-set separation (D0, D1c, D3, D4, D6, D11) and the
single-type `Material_set` (D0, D1, D2), both of which postdate that review; the
persistence design (D9, D10); the standalone-`Material_set` shape (D0, D2,
D5); and the phase and test lists as rewritten around all of it.

**Citation state.** The membership premises and their file:line citations were
re-checked against the tree on 2026-08-31, after the tool-mesh removal
(`fe78df850..f81e01e63`) invalidated part of them, and the `editor.cpp` and
`id_renderer.cpp` line numbers those commits moved were corrected across the
plan. A consistency review on 2026-09-01 then spot-checked the rest against the
tree: every citation it looked at resolved, with one exception now fixed
(`forward_renderer.hpp:221` for the R8a comment, which is at `:173`; a fourth
citing comment at `draw_list_scene.hpp:58` was also missing from the inventory).
That review fixed eighteen internal inconsistencies - among them a V3 assertion
against the wrong set, `Material_watch` being both kept and deleted, D10 naming
fields D2 does not declare, `Material_set`'s library API being described two
ways, and D6's schedule presenting global steps as per-root ones - and left the
design itself unchanged. Citations it did not touch are still unverified - if one
looks wrong, check it.

**The tool / gizmo path holds no material state**, which is why the plan has no
tools case anywhere. The transform gizmo is drawn entirely with the debug
primitive renderer and hit-tested analytically
(`handle_visualizations.hpp:28-33`), so it owns no mesh, no material and no
slot; `f81e01e63` removed the tool scene root, its content library, the tool
composition pass and the ID renderer's tool pass once that held for everything
in the tool layer. `Hover_entry::tool_slot` survives with no producer.

**Where to resume.** Either start phase 1, or continue improving the plan. Open
questions worth settling before implementation, roughly in order of risk:

1. **The two-set split (D0) is new and unreviewed.** It replaced a single
   per-root set after review; D0, D1c, D3, D4, D5, D6, D11, phase 3 and the
   risk list were rewritten around it and have had no independent pass. The
   specific things to check are that every invalidation reaches both sets
   (V4.5a), and the footprint doubling on main roots (phase 3 sizing, V5).
2. **D10's content hash is the top *correctness* risk.** It must cover exactly
   the bytes `Material_buffer`'s record writer reads, or a material silently
   stops updating - the same symptom as the bug being fixed. Worth an explicit
   field-by-field cross-check against the writer, and a decision on whether the
   hash and the writer should be generated from one list rather than kept in
   sync by comment. With two sets it now also has to fire for both.
3. **D9 needs `Device::is_frame_completed()` on GL, Metal and null** (it exists
   only on Vulkan). GL keeps `m_pending_frames` / `m_completed_frames`
   (`gl_device.hpp:334-335`) and Metal has `frame_completed()`
   (`metal_device.hpp:167`), so a highest-completed watermark maintained in each
   backend's existing sink answers it conservatively - never optimistically,
   which is the only unsafe direction.
4. **Phase 4 is still one large commit.** Check whether the R8a amendment and
   the `Primitive_buffer` signature change can be split out ahead of it.
5. **Amending R8a** in `doc/draw_list_renderer_requirements.md` is a phase 4
   item (D5). Confirm the wording covers only materials + heap and leaves the
   camera / light / joint clauses standing.
6. **Further follow-ups `c6ab99db4` unblocked**: stop `Shadow_renderer`
   duplicating the same six buffers; stop `Id_renderer` borrowing a joint buffer
   through `Forward_renderer::get_joint_buffer()`. The plan does not depend on
   either. (The third, splitting the draw-list draw entry point out of
   `Forward_renderer`, is now the plan's prerequisite commit, section 3.)

## 1. Motivation

### 1.1 The reported bug

With material *Gold* selected in the material panel and its base colour edited
to red, dragging Gold onto a cube turns the cube red, but then dragging the same
Gold onto an icosahedron leaves the icosahedron nearly unchanged. Dragging in
the opposite order moves the failure to the other mesh.

Root cause: `erhe::primitive::Material::material_buffer_index`
(`erhe_primitive/material.hpp:119`) is a single mutable field on the shared
material object, rewritten by whichever `Material_buffer::update()` ran most
recently (`material_buffer.cpp:175` assigns the material's position in the list
it was handed). There are five call sites:

| Call site | Material list source |
| --- | --- |
| `erhe/scene_renderer/.../forward_renderer.cpp:165` (`begin_pass`) | `Base_render_parameters::materials` |
| `erhe/scene_renderer/.../forward_renderer.cpp:461` (`draw_primitives`) | `Base_render_parameters::materials` |
| `erhe/scene_renderer/.../shadow_renderer.cpp:394` | `Shadow_render_parameters::materials` |
| `editor/renderers/ddgi_renderer.cpp:997` | scene root's content library |
| `editor/renderers/ray_trace_renderer.cpp:463` | scene root's content library |

and the lists reaching those parameters differ within one frame: viewport passes
pass the scene root's whole content library
(`editor/renderers/composition_pass.cpp:160`), shadow passes pass the same list
assembled separately by `Shadow_render_node`
(`editor/rendergraph/shadow_render_node.cpp:469-470`, used at `:601`), the
material preview passes its own library holding exactly one material
(`editor/preview/material_preview.cpp:226-230`), the BRDF slice window passes an
ad-hoc one-element list (`editor/content_library/brdf_slice.cpp:116`), and two
passes pass an empty list (`composition_pass.cpp:201`,
`editor/developer/depth_visualization_window.cpp:149`).

`Editor::tick()` renders thumbnails (`editor.cpp:766`) and imgui windows
(`editor.cpp:664`), both of which render the material preview, *before*
`flush_draw_lists()` (`editor.cpp:796`). So when the draw list writes a cached
record it reads a `material_buffer_index` the preview just set to 0
(`draw_list_scene.cpp:458`).

`Draw_list_scene::sync_gpu_slots()` (`draw_list_scene.cpp:593-645`) repairs this
only by *edge detection*. `Material_watch::slot` (`draw_list_scene.hpp:308`) is
initialised only when the watch is new (`use_count == 0`,
`draw_list_scene.cpp:740-744`) and thereafter rewritten by `sync_gpu_slots()`
itself on every detected change (`draw_list_scene.cpp:605-607`). The first mesh
to receive the material is therefore repaired on the next draw, leaving the
watch holding the correct slot. A second mesh registered later writes its record
from the same preview-clobbered index, the watch already holds the post-repair
value, no difference is detected, and that record keeps the wrong slot
indefinitely. Hence the order dependence.

**Scope note.** Only `Draw_list_scene`'s *cached* records are stale today. The
other readers of `material_buffer_index` run inside the pass, after that pass's
own material update, and are correct as things stand:

- `light_buffer.cpp:567` - `Light_buffer::update` is called from
  `forward_renderer.cpp:173`, after the material update at
  `forward_renderer.cpp:165`.
- `scene_tlas.cpp:295` - `Scene_tlas::update` clears and rebuilds
  `m_instance_records` on every call (`scene_tlas.cpp:227,298`), and both
  callers update the material buffer immediately before
  (`ray_trace_renderer.cpp:463` then `:468`; `ddgi_renderer.cpp:997` then
  `:998`).
- `primitive_buffer.cpp:359` (`write_primitive`) - written during the pass. (The
  other reader, `primitive_buffer.cpp:226`, is in the `meshes` overload at
  `:107`, which has no caller anywhere in the repo and is dead code.)

They are converted anyway because the plan removes the field (D0, phase 6), not
because they are broken. The shared mutable field motivates the *scope*; the
draw-list record motivates the *urgency*.

**A second defect the content library does not cover.** The library is
populated at *mesh registration* only, so assigning a material to an
already-registered mesh never reaches it (the plan's R3 and R4; the full chain
is D11). The notification it does raise,
`Scene_root::on_mesh_material_changed` (`scene_root.cpp:1440-1445`), enqueues a
draw-list re-register, which is a no-op for a root with no draw list - which is
why the plan gives *every* root a forward `Material_set` fed by that same hook
(D1c), not a draw list. So "the set contains exactly the library's materials" is
not a safe definition of membership (D1).

### 1.2 The second motivation: the buffer is rewritten far too often

Materials are near-static data. Today the whole material buffer and the whole
texture heap are rebuilt from scratch **once per pass**: `reset_heap()` +
`Material_buffer::update()` at `forward_renderer.cpp:121,165`, again at
`:458,461` for `draw_primitives`, again at `shadow_renderer.cpp:393-394`, again
per DDGI tick and per ray-trace dispatch. For a scene with a few hundred
materials that is a few hundred kilobytes of ring-buffer writes, a few hundred
`Texture_heap::allocate()` linear searches, and one fresh Vulkan descriptor set
(`vulkan_texture_heap.cpp:289-298`) *per pass*, every frame, to reproduce bytes
that are almost always identical to the previous frame's.

Once slots are stable (D1, D9) the buffer contents are a pure function of the
set's membership and of each member's `Material_data`. Both change rarely. So
the buffer becomes persistent: written when it is invalidated, rebound
otherwise (R10).
