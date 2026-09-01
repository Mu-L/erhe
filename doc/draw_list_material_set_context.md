# Draw list material set - planning context

Background for `doc/draw_list_material_set_plan.md`: where the work stands, how
much of the plan has been reviewed, and why the work exists at all. The plan
itself is self-contained for implementing; this is what to read before picking
it up.

## Status: what exists, and what phase 3 must do

**The reported bug still reproduces**, and its regression test says so.
`Material::material_buffer_index` is still the single mutable field every
`Material_buffer::update()` rewrites, and no frame behaves differently from
before the work started.

**What exists and is unit-tested, but has no callers:**

- `erhe_graphics/multi_copy_buffer.{hpp,cpp}` - the persistent storage of D9;
- `Device::is_frame_completed()` / `get_number_of_frames_in_flight()` on all
  four backends (the Metal implementation compiles nowhere on Windows and is
  unverified until the sweep);
- `Texture_heap`'s defaulted `max_textures` (D2a);
- `erhe_scene_renderer/material_set.{hpp,cpp}` - `Material_slot_id`,
  `Material_slot`, and the **membership half** of `Material_set` (D1);
- `erhe_gpu_test_support`, the device bring-up and frame / readback helpers as
  a library, for the GPU tests V2 adds.

**The regression test, V3**, is
`mcp_server_tests.cpp` `material_drag_to_second_mesh_uses_same_record_slot`,
and it is **red**: mesh A assigned first reads records A=14 B=0, and with the
order reversed A=0 B=15 - the reported asymmetry, in the cached records. It
turns green in phase 4. Its MCP surface is `assign_mesh_material` and
`get_draw_lists`'s per-mesh `entries` array.

**Phase 3 is next**: `Material_set`'s GPU half (D2, D10), the two kinds of
owner (D3, D4), the schedule (D6), and the Vulkan descriptor-set use-stamping
fix (D2b). Two things about it that the plan states but are easy to miss:

- the class currently has no GPU data members at all, which is what lets V1 run
  deviceless. Phase 3 adds them, so it also decides `Material_set_create_info`
  and every construction site;
- `Material_buffer` keeps its existing ring-based `update()` until phase 6.
  Phase 3 adds the span-writing record writer alongside it.

**The prerequisite commit has not been made.** The plan's section 3 asks for
`Draw_list_renderer` to be split out of `Forward_renderer` before the material
work; phases 1 and 2 did not need it, and it is still outstanding. D5 is
written against `Draw_list_renderer` throughout, so it has to land before
phase 4.

**Confidence, by part.** The membership analysis (section 1 below, and D1, D1a,
D1d in the plan) has seven independent review rounds behind it, and the seventh
found no blocking or major defect. Newer and **unreviewed**: the two-set
separation (D0, D1c, D3, D4, D6, D11), the single-type `Material_set` (D0, D1,
D2), the persistence design (D9, D10), the standalone-`Material_set` shape (D0,
D2, D5), and the phase and test lists as rewritten around all of them. The
citations were re-checked against the tree twice; the second pass also fixed
eighteen internal inconsistencies in the plan and left the design unchanged.
Citations neither pass touched are still unverified - if one looks wrong, check
it.

**Open questions, roughly in order of risk:**

1. **The two-set split (D0) is unreviewed.** It replaced a single per-root set
   after the review rounds above. The specific things to check are that every
   invalidation reaches both sets (V4.5a) and the footprint doubling on main
   roots (phase 3 sizing, V5).
2. **D10's content hash is the top *correctness* risk**, and phase 3 is where
   it is written. It must cover exactly the bytes `Material_buffer`'s record
   writer reads, or a material silently stops updating - the same symptom as
   the bug being fixed, with a new cause. Worth an explicit field-by-field
   cross-check against the writer, and a decision on whether the hash and the
   writer should be generated from one list rather than kept in step by
   comment. With two sets it has to fire for both.
3. **Phase 4 is one large commit.** Check whether the R8a amendment and the
   `Primitive_buffer` signature change can be split out ahead of it.
4. **Amending R8a** in `doc/draw_list_renderer_requirements.md` is a phase 4
   item (D5). Confirm the wording covers only materials + heap and leaves the
   camera / light / joint clauses standing.
5. **Two follow-ups the `Scene_pass_resources` extraction unblocked**, neither
   of which the plan depends on: stop `Shadow_renderer` duplicating the same
   six buffers, and stop `Id_renderer` borrowing a joint buffer through
   `Forward_renderer::get_joint_buffer()`.

**Verification recipe.** Build `editor` and the test targets in `build_tests`
(configured **OpenGL**, so the Vulkan path needs `scripts/build_ninja_win_vulkan.bat`
as well). For V3: launch `build_tests/src/editor/Debug/editor.exe`, wait on
`127.0.0.1:3743/health`, then run `mcp_server_tests.exe` with
`ERHE_MCP_TEST_TIMEOUT_S=1` (`scripts/run_mcp_tests.ps1` does not forward
`--gtest_filter`). Baseline: 41 pass, 5 pre-existing skips, V3 red.
`erhe_scene_renderer_tests` 20 pass, `erhe_graphics_gpu_tests` 75 pass + 1
pre-existing skip. Running the editor rewrites
`config/editor/editor_settings.json` - revert it before committing.

**Traps found while implementing, which the next session would otherwise hit
again:**

- `Buffer::begin_write(offset, count)` **ignores its offset** on the
  persistently mapped path and returns the whole-buffer map. Write at an offset
  through `get_map().subspan(offset, count)` plus `flush_bytes()`, or through
  `upload_sub_data()` - the two paths `Ring_buffer` takes.
- The GL backend plants a frame fence only when something requested a sync, so
  anything derived from fence completion alone stalls for a consumer that
  requests none. `wait_idle()` advances the watermark for that reason.
- `erhe_graphics_tests` is the **deviceless** target; anything needing a
  `Device` belongs in `erhe_graphics_gpu_tests`, which is not built for
  `ERHE_GRAPHICS_API=none`.
- The material preview's churn pattern settles at **two** slots, not one,
  because the new material is referenced before the old one is released.

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
