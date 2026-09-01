# Draw list material set - planning context

Background for `doc/draw_list_material_set_plan.md`: where the work stands, how
much of the plan has been reviewed, and why the work exists at all. The plan
itself is self-contained for implementing; this is what to read before picking
it up.

## Status: what exists, and what phase 5 must do

**The reported bug is fixed**, and its regression test says so: V3 is green,
in both assignment orders. A material's slot is now a property of the
`Material_set` that issued it, so a cached draw-list record and the buffer
bound when it is drawn agree by construction.

`Material::material_buffer_index` still exists but nothing in the raster path
writes it any more: the compute path (DDGI, ray tracer) is its last writer and
reader until phase 5, and phase 6 removes the field.

**What exists and is unit-tested, but has no callers:**

(This list is phase 2's; phase 3 gave every entry an owner except
`Multi_copy_buffer`, which is now reached through `Material_set` alone.)

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

**Phase 3 has landed.** `Material_set` now has its GPU half - the material
buffer, the texture heap and `update` / `bind` / `unbind` / `invalidate`, with
D10's content-hash invalidation - and both kinds of owner exist and are fed:
`Scene_root` carries the forward set and references its meshes' materials into
it from the four mesh hooks, `Draw_list_scene` carries the draw-list set, and
the library-only owners (BRDF slice, the standalone example, the shared empty
set) each construct their own. `App_scenes::update_material_sets()` runs the D6
schedule after `flush_draw_lists()`; the previews and the BRDF slice run their
own at their render entry points. The Vulkan descriptor-set use-stamping fix
(D2b) is in. `Material_buffer` keeps its ring-based `update()` until phase 6;
the span-writing record writer sits alongside it and both go through one
`Material_record_inputs` gather, so the content hash covers exactly the bytes
the writer reads by construction.

**No frame behaves differently yet.** Nothing reads either set's slots - that
is phase 4 - so the reported bug still reproduces and V3 is still red by
design.

**Phase 4 has landed**, and with it the prerequisite `Draw_list_renderer`
split. `Base_render_parameters` carries a `Material_set* material_source`
instead of a material list; `Scene_pass_resources`, `Forward_renderer` and
`Shadow_renderer` own no material buffer, texture heap or fallback pair and
reset nothing per pass; `Primitive_buffer` looks slots up in the set its own
pass binds; `Draw_list_object` holds `Material_slot_id`s and
`write_slot_fields` resolves through them with a verify; `Material_watch` and
the material half of `sync_gpu_slots()` are gone;
`set_exclude_unlit_from_shadows` enqueues its rebuild (D1d); R8a is amended.

**Phase 5 is next**: `Ddgi_renderer`, `Ray_trace_renderer` and `Scene_tlas`
move to the root's forward set. Until then those three keep their own material
buffers and library-order indices, which is self-consistent - each writes its
TLAS instance records and reads them back within one pass - and they are the
only remaining writers of `Material::material_buffer_index`.

**The R4 cheap path (D11) has landed too**, as its own commit after the switch.
`Scene_root::on_mesh_material_changed` enqueues a material update;
`Draw_list_scene::try_material_slot_update()` re-classifies and takes the cheap
path when every entry still belongs in the list it occupies. It compares the
whole draw list key plus the entry's baked variant rather than the three fields
D11 names - a mis-predicted "unchanged" key is a wrong pipeline, and
re-classifying costs what comparing those three fields would.

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
2. ~~**D10's content hash is the top *correctness* risk.**~~ **Settled in
   phase 3, by construction rather than by cross-check.** The hash and the
   record writer both read `Material_record_inputs` and nothing else
   (`material_buffer.hpp`), built by one `gather_material_record_inputs()`, so
   a field the writer reads is a field the hash covers. Both sets get it: the
   hash pass is in `Material_set::update()`, which every set runs.
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
`erhe_scene_renderer_tests` 20 pass (V1, deviceless),
`erhe_scene_renderer_gpu_tests` 10 pass (V2, device-backed; built only where
`erhe::gpu_test_support` exists), `erhe_graphics_gpu_tests` 75 pass + 1
pre-existing skip. Running the editor rewrites
`config/editor/editor_settings.json` - revert it before committing. For a quick
smoke run of the whole frame, set `quit_after_frames` in that file and revert
it afterwards.

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
- **`App_rendering` cannot own anything a scene root or a preview needs from
  its constructor.** It is built inside `Editor`'s construction taskflow, and
  so are they, with no edge ordering them; `App_context` is filled after the
  whole graph runs. Anything in that position goes before the taskflow and is
  published into `App_context` on the spot - which is what
  `Material_set_factory` does.
- `erhe::graphics::Texture` **is itself a `Texture_reference`** that returns
  itself, so a plain texture needs no wrapper; `Texture_reference` is abstract
  and cannot be constructed directly.
- The MCP test harness's readiness probe is **`/health`, which answers before
  the default scene exists**. A test run started the moment health goes green
  fails with `Scene not found: ` (empty name). Give the editor a few more
  seconds, or re-run.
- `nlohmann::json{value}` builds an **array**, not a scalar. Assign the value
  directly (`e["k"] = v;`) when writing a number into an MCP payload.

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
