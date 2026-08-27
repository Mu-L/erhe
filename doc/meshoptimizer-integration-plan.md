# Integrate meshoptimizer into erhe

## Context

erhe has no mesh *reordering/welding* optimization today: imported glTF triangle soups keep their authored triangle and vertex order (indices are copied verbatim; vertices are converted per attribute at the sink, including the existing snorm16x3 AABB position quantization — itself already a memory optimization), and the procedural geometry path emits one vertex per facet-corner with an arbitrary triangle order. The goal is to integrate [zeux/meshoptimizer](https://github.com/zeux/meshoptimizer) (local reference checkout at `D:\meshoptimizer`, vanilla upstream) and use it to optimize all renderable meshes: vertex weld/remap, vertex-cache order, overdraw order, and vertex-fetch order, with before/after `meshopt_analyze*` statistics. LOD and meshlets are explicitly **future work** — leave seams, implement nothing.

User-confirmed requirements:
- **Source vs optimized asset separation (hard requirement):** source assets are loaded as-is and never mutated. For rendering, a derived *optimized asset* is built (and optionally cached to disk). Export and ground truth always use source data.
- Config: two booleans in the code-generated `Mesh_memory_config` (`mesh_memory_config.py`): `optimize_meshes` and `mesh_optimize_cache`. Default **off** during bring-up; flip `optimize_meshes` on after verification (same rollout as vertex position quantization). Filesystem cache must also work on Android/Quest.
- Geometry (GEO::Mesh) always operates in at least single-precision float. Soup-path meshopt passes run on float data before any snorm16 position encoding; the geometry-path weld (phase 5) deliberately runs on **post-encode** vertex bytes (more merges; GPU data equals staged data), with overdraw reading float positions from a decode/staging copy.
- **Prerequisite (user decision): remove the ID-buffer edge-line method first** (`content_edge_lines.use_id_buffer`, default false — judged not good enough). After removal, the per-corner facet-id attribute (`custom_attribute_id` / `a_custom_0`) has exactly one consumer: the ID renderer — which uses the always-built `original` mesh variant, so the `optimized` variant can weld cross-facet freely.
- **Position encoding via meshoptimizer (evaluated):** meshoptimizer has **no** affine AABB bias+scale helper (`meshopt_computePositionExponent`/`encodeFilterExp` are a different exponent-based encoding that would change the shader decode), so erhe's affine `(p - center) * inv_scale` stays. For the quantization step, `meshopt_quantizeSnorm(v, 16)` (clamps input to [-1,1], scale 32767, round half away from zero) is bit-identical to erhe's `float_to_snorm16` (`dataformat.cpp:39-49`: no input clamp, scale 32767, same rounding, post-scale clamp to [-32768, 32767]) **because both encode sites pre-clamp the encoded position to [-1,1]** (primitive.cpp:1023-1027, primitive_builder.cpp:601-605). Revision: route the position quantization through `meshopt_quantizeSnorm` at the two **attribute-aware caller sites** — the `encode_position` branch in `build_buffer_mesh_from_triangle_soup()` (primitive.cpp:1010-1029) and `Build_context::write_position()` (primitive_builder.cpp:585-607) — quantizing there and writing the resulting snorm16 values directly, instead of handing pre-scaled floats down to the format-agnostic `dataformat::convert()`/`write_low3()` machinery (those dispatch on `Format` alone and cannot express "position only"; they stay untouched, as do all other snorm attributes). Both sites live in erhe_primitive, so the meshoptimizer.h include stays contained there. Shader decode (`get_position_quantization()`) unchanged; verify bit-identical output against the current path on a test mesh. The optimized soup keeps **float** positions so geometry reconstruction, CPU raytrace, and export stay full precision.
- Geometry build path: **full optimization** (weld + cache + overdraw + fetch) via a CPU-staging restructure of `Primitive_builder` — user decision after review showed the editor replaces every imported soup mesh with a geometry-path rebuild (for edge lines), making the geometry path the steady-state chokepoint (see Phase 5).

Key architecture facts (verified, corrected by independent review):
- glTF import builds `erhe::primitive::Triangle_soup` (uint32 indices + interleaved float vertices) in `get_primitive_geometry()`, finalized just before `Primitive` construction at `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp:2295`.
- Runtime soup consumers: `build_buffer_mesh_from_triangle_soup()` (`src/erhe/primitive/erhe_primitive/primitive.cpp:882`) and lazy `mesh_from_triangle_soup()`/`parse_triangles()` (`triangle_soup.cpp:194`, builds `Element_mappings::triangle_to_mesh_facet` and `mesh_corner_to_vertex_buffer_index`). Physics collision shapes go through `render_shape->get_geometry()` (`collision_shape_from_mesh.cpp:24-30`) — they follow geometry, which stays source-derived and maximum-precision, no change. `Primitive_raytrace(Triangle_soup&)` (primitive.cpp:353) has zero callers today — nothing to switch. glTF export uses `get_triangle_soup()` at `gltf_fastgltf.cpp:4908, 4964` (the `m_erhe_triangle_soup_entries` dedup map at :4375 consumes what those pass).
- Geometry path (`Primitive_builder`/`Build_context`, `primitive_builder.cpp`) allocates GPU ranges up-front, writes 4 lockstep vertex streams one-vertex-per-corner. Fill indices are a **fan triangulation per facet** (`build_triangle_fill_index()`, primitive_builder.cpp:892-903): a quad emits `(n, n+1, n+2), (n, n+2, n+3)` — the facet's first corner is shared by all fan triangles, so index triples are NOT sequential `3t..3t+2` and vertices ARE shared within a facet.
- CPU raytrace does its own second build (independent triangle order — safe). GPU BLAS (`src/editor/renderers/scene_tlas.cpp`) builds from final render buffers after mesh build — no invalidation needed **at import time** (edit-time invalidation needs the explicit eviction described in the architecture section).
- Stale foothold: `#meshoptimizer` comment at `src/editor/CMakeLists.txt:1032`.
- Android cwd is per-app writable internal storage (`SDL_GetAndroidInternalStoragePath` + `chdir` in main.cpp) — cwd-relative cache dir works there.

## Decision (2026-08-27): where the variants live — `Primitive`

**Settled. Do not re-litigate.** Three placements were compared after the
`Mesh_variant` plumbing (84937fee5) landed but before anything constructed an
optimized variant — the cheapest possible moment to change course.

| | **A** — inside the shape (as committed) | **B** — variant sets on `erhe::scene::Mesh` | **C** — variant shapes on `Primitive` (**chosen**) |
|---|---|---|---|
| Storage | `std::array<Buffer_mesh, 2>` in `Primitive_render_shape` + `std::array<Element_mappings, 2>` in `Primitive_shape` | several `std::vector<Mesh_primitive>` on `Mesh` | `Primitive` holds the source `Primitive_render_shape` plus one `shared_ptr<Primitive_render_shape>` per extra variant |
| Shape internals | two parallel arrays; every internal access indexes by variant | revert to pre-meshoptimizer | revert to pre-meshoptimizer — pure deletion, incl. no-arg `get_triangle_soup()` / `get_element_mappings()` |
| Mappings↔mesh pairing | by matching array index — enforced by discipline | per variant primitive | in the same object as the mesh it describes — by construction |
| Cost when optimization is off | a second empty `Buffer_mesh` **and** `Element_mappings` in *every* `Primitive_render_shape` | one null pointer per `Primitive` | one null pointer per `Primitive` |
| Shared-primitive dedup | per `Primitive` (implicit) | per `Primitive` via the variant pointer table | per `Primitive` — the table *is* the dedup, explicitly |
| Per-instance state | untouched | `material` + `lightmap_uv_scale_offset` duplicated per set, and must be kept in sync through `set_primitive_material` / `set_primitive_lightmap_uv_scale_offset` / re-registration | untouched |
| Draw-list key `(object_index, mesh_primitive_index)` | unaffected | one set per LOD level may change primitive count/order — the key stops meaning one thing | unaffected |
| Selection points | ~21 renderable-mesh sites name a variant | same ~21, moved to `Mesh` | the same ~21, unchanged from 84937fee5 |
| Geometry / raytrace / collision / export sites | unaffected | unaffected | unaffected — all 36 `render_shape->get_geometry*()` sites plus `get_shape_for_raytrace()` keep pointing at the source shape |
| Per-instance LOD selection | no | yes | no (stays a later `Mesh`-level index that selects among these shapes) |

**Chosen: C.** It is the only one of the three that makes `Element_mappings`
live in the same object as the `Buffer_mesh` it describes *and* costs nothing
when optimization is off *and* leaves `Mesh` / `Mesh_primitive` / the draw-list
key alone. It reverts `Primitive_shape` and `Primitive_render_shape` to their
pre-meshoptimizer shape by deletion rather than by rewriting them, because the
optimized variant becomes an ordinary geometry-less render shape — a category
that already exists and is already in use (`Primitive_render_shape(Buffer_mesh&&)`,
primitive.cpp:766, for scene_builder's instanced cubes), so the variant is not a
new invalid-state category needing a guard. `Primitive` is also already the
dedup unit for sharing — glTF import dedups `Primitive` slots
(gltf_fastgltf.cpp:2316-2345) and brushes share `Primitive` objects — so hanging
the variants there satisfies "shared primitives are optimized once, as one"
directly, with the originals never mutated, exactly as required. B was rejected
because `Mesh` is per-instance while the optimized variant's selection is
per-*pass*: it would duplicate per-instance primitive state across sets for a
choice no instance ever makes differently, and it is the only option that puts
the draw-list key at risk. A was rejected because it pays for the second variant
in every primitive whether or not optimization is on, and pairs each mesh with
its mappings by matching array indices rather than by ownership.

Consequences to design against under C:

- **Cross-variant publication is no longer one mutex.** Today
  `commit_geometry_buffer_mesh()` swaps mesh + mappings for all variants under a
  single state mutex. Under C each shape carries its own. The discipline is:
  attach/publish the optimized shape only *after* the source commit succeeds,
  and on invalidate detach the optimized shape *first*. A renderer therefore
  never sees a stale optimized mesh beside a committed new source one. (This is
  the one thing A got for free.)
- **`Primitive` owns variant construction.** `make_renderable_mesh()` builds the
  source shape and, when the requested variant set asks for it, creates and
  builds the optimized shape. The requested set stays caller-prepared in
  `Build_info`/`Buffer_info` as already planned.
- **The optimized shape holds the optimized soup and its own composed
  mappings.** That removes `m_optimized_triangle_soup` and the variant-aware
  `get_triangle_soup(Mesh_variant)` from the plan entirely: each shape has one
  soup, one mappings instance, one `Buffer_mesh`. Export and the ERHE_geometry
  round-trip use the source shape's no-arg accessor, unchanged.
- **The optimized shape must never build its own `Geometry` or raytrace.**
  Nothing reaches it to try: every geometry, raytrace, collision and export site
  goes through `primitive.render_shape` / `get_shape_for_raytrace()`, neither of
  which is variant-selected.
- **Invalidate-on-edit** drops the optimized shape pointer (frame-safe release —
  in-flight frames may still reference it) and re-registers all meshes sharing
  the primitive, per the existing `collect_meshes_sharing_primitives` /
  `Scene_commit_queue` precedent. Ray tracing is unaffected (BLAS is built from
  the source shape per the 2026-08-26 amendment), so no BLAS eviction is needed.
- **First-optimization-wins** uses the same publish-once discipline as
  `m_geometry_published`: two loader workers may ask for the same variant at
  once.

Effect on what is committed: 84937fee5 is **not** reverted. Its selection-point
half — the ~21 sites that name a variant, `bucket_primitives()`'s variant
preference, the chosen `Buffer_mesh*` riding on the bucket entry, and
`Draw_list_entry` recording the variant it was baked from — is required by every
design and is kept as-is. Only its storage half is replaced: the two
`std::array`s and the `m_has_optimized` flag come out of `Primitive_shape` /
`Primitive_render_shape`, the no-arg `get_triangle_soup()` /
`get_element_mappings()` come back, and `Primitive` gains the variant table with
`get_resolved_renderable_mesh(preference)`'s resolve-and-fetch-under-one-lock
contract moving up to it.

## Architecture: source / optimized asset split

> The bullets below describe the placement as first implemented (Design A,
> commit 84937fee5). The decision above supersedes where the variants are
> **stored**; the selection points, the `Build_info` variant set, the
> renderer-side choice and the invalidate-on-edit rules they describe are
> unchanged. Read the decision first.

- Explicit variant enum: `Mesh_variant { original, optimized }`. (A dedicated minimal `id_renderer` variant — position+id only, allowing the full original variant to be dropped — is documented **future work**, not v1.)
- `Primitive_shape` keeps the **source** `Triangle_soup` untouched, plus an **optimized** `Triangle_soup` derived from it (`m_optimized_triangle_soup`; null when optimization is off). Single accessor `get_triangle_soup(Mesh_variant)`: `original` → source soup, `optimized` → optimized soup or null (no silent fallback that could blur which data a build used). Export and the ERHE_geometry round-trip use `get_triangle_soup(Mesh_variant::original)`. (Naming rule: avoid "renderable" in new names — `Primitive_render_shape` already carries that meaning.)
- **Geometry is single and maximum-precision:** lazy geometry reconstruction (`make_geometry()`/`mesh_from_triangle_soup`) always builds from the **source** soup — full-precision floats, never from optimized/quantized data. There is exactly one `Geometry` per shape (publish-once, unchanged). Physics collision shapes follow geometry and thus also use maximum precision. Export and the ERHE_geometry round-trip stay on the source soup.
- **Whole `Element_mappings` per variant** (not per-member `_opt` fields — wrong granularity): each built `Buffer_mesh` variant is paired with its own complete `Element_mappings` instance (mesh ↔ vertex-buffer/triangle correspondence is order-dependent, so one instance cannot serve both orders). `parse_triangles()` produces the `original` variant's instance as today; the `optimized` variant's instance is **derived by composing each member** with the optimization remap that `optimize_triangle_soup()` returns (triangle permutation + **forward** source→output vertex remap — the same pair the filesystem cache stores): `triangle_to_mesh_facet` composes through the permutation (degenerate `NO_INDEX` entries pass through correctly); `mesh_corner_to_vertex_buffer_index` composes through the forward remap, under which weld-merged source vertices map to the same output slot by construction. The third member, `mesh_vertex_to_vertex_buffer_index`, is geometry-path-only (`parse_triangles` never fills it; its only consumers are the edge-line build inside primitive_builder.cpp) — no composition needed. Render buffer build (`make_buffer_mesh(Buffer_info)`) reads `get_triangle_soup(variant)` for each variant it is asked to build.
- **Buffer_mesh variants:** `Primitive_render_shape` holds one `Buffer_mesh` + `Element_mappings` pair per built variant:
  - `original` — source-order build (current behavior; carries valid facet ids). **Always built**, so the ID renderer and picking work unconditionally with no dedicated id-variant machinery.
  - `optimized` — welded/reordered build; its facet-id bytes are zeroed (meaningless after welding; excluded from the weld compare). Built in addition to `original` when requested.
  - **Which variants to build is specified in `Build_info`/`Buffer_info`** (a variant set alongside `Mesh_optimize_options`); the `make_buffer_mesh()` **caller is responsible** for preparing it — in practice `mesh_memory.cpp make_primitive_buffer_info()`/`Build_info` construction fills `{original}` or `{original, optimized}` from the `optimize_meshes` config.
  - **No "active variant" designation on the mesh.** Renderers query which variants are present and choose: forward/shadow/draw-list renderers prefer `optimized` when present, else `original`; `Id_renderer` always uses `original` (valid facet ids). **The no-arg accessors are deleted** — both `Primitive_render_shape::get_renderable_mesh()` (primitive.hpp:184) and the `Primitive::get_renderable_mesh()` wrapper (primitive.hpp:249), plus the no-arg `get_element_mappings()` (primitive.hpp:133) which becomes variant-aware — every call site must pass a `Mesh_variant` (or receive an already-chosen `Buffer_mesh*`), so the compiler enumerates all selection points; `bucket_primitives()` (`mesh_memory.cpp:1037/:1073`) takes a variant preference and its buckets **carry the chosen `Buffer_mesh*` downstream**, so the record/draw fill sites that today re-derive the mesh independently — `primitive_buffer.cpp:230,345` (base_vertex/id-ranges/quantization AABB — the `Id_renderer::render_buckets` id-range path), `draw_indirect_buffer.cpp:75,147`, `draw_list_scene.cpp:492`, `content_wide_line_renderer.cpp:148`, `scene_tlas.cpp:241` and lightmap_baker (BLAS source — **`original`**, not the drawn variant, per the 2026-08-26 user amendment: ray tracing is unaffected by the optimizer, which is what removes the BLAS-eviction-on-edit requirement), viewport/properties/MCP/debug sites — all follow the flowed choice instead of a hidden default.
  - **GPU vertex edits invalidate the optimized variant — no dual-write.** `paint_tool.cpp:307/346` (colors), `weight_paint_tool.cpp:574/594` (joint weights), and `mesh_component_transform.cpp:711-860` (live-drag positions/normals; the direct edge-line write at :807 included) write GPU vertex bytes through the mappings. On the first such edit, the `optimized` variant is **invalidated** (dropped — use frame-safe mesh-memory release, in-flight frames may still reference it) and only the `original` mesh is updated, exactly as today. Renderers then see `optimized` missing and (a) fall back to `original` for rendering, and optionally (b) queue an optimized rebuild (natural point: interaction end, through the normal build path). Invalidation must:
    - re-register **all meshes sharing the shape**, not just the edited one — use the existing `collect_meshes_sharing_primitives` + per-sharer `update_rt_primitives` precedent (`async_raytrace_kickoff_operation.cpp:152-161, 171, 190-200`), or route the drop through `Scene_commit_queue` like `commit_geometry_buffer_mesh` (the enqueue-only re-register hook is thread-safe and flushes before rendering, `scene_root.cpp:1433-1438`, `editor.cpp:852`);
    - **explicitly evict BLAS entries keyed by the dropped `Buffer_mesh`** in both `Scene_tlas` and `Lightmap_baker` (or generation-tag the key) — neither cache subscribes to primitives-changed notifications (they evict only on shape liveness), so without explicit eviction the dropped pointer leaks a pinned entry, and a rebuild landing at the same address would silently reuse the **pre-edit** acceleration structure.
    Each variant carries its own full allocation set (edge-line/expanded-fill included — edge-line memory doubles while both are live). Race note: a worker-prepared `Pending_buffer_mesh` committing after a first edit replaces the edited original anyway — commit wins, the edit preview is lost, same as today.
  - This also dissolves the weld-vs-per-corner-addressability problem: edits always address the per-corner `original` buffer; a later optimized rebuild re-welds from the post-edit data. No divergence between forward rendering and ID picking is possible (only one variant is live during an edit).
  - Consistency: today `Element_mappings` is a single per-shape slot (primitive.hpp:154) swapped atomically with the renderable mesh by `commit_geometry_buffer_mesh()` (primitive.cpp:812-826); this refactor makes the swap unit the **set of built (mesh + mappings) pairs**, still one atomic swap under the state mutex, while `m_geometry` stays a single publish-once, maximum-precision instance built from the source soup.
  - **Variant→soup routing:** each requested variant builds from its own data — `original` from `get_triangle_soup(original)` / unoptimized order with `parse_triangles()` mappings, `optimized` from `get_triangle_soup(optimized)` with composed mappings.
  - The deferred `prepare_geometry_buffer_mesh()`/`commit_geometry_buffer_mesh()` async-import flow (async_raytrace_kickoff_operation.cpp:132/183) prepares **all requested variants** on the worker and commits them in the one atomic swap, so a committed geometry mesh (with edge lines) can never be shadowed by a stale fill-only mesh. The committed meshes are **geometry-path builds** — their optimization is governed by the same `optimize_meshes` flag through phase 5. This is why phase 5 exists: it is the steady-state optimization for every finalized editor mesh.
  - Changing the built-variant set after draw-list registration requires re-registration (`Mesh::update_rt_primitives` → `notify_primitives_changed` — records cache base_vertex/index ranges/affine at registration time). In v1 the *requested* set is fixed from config at build time; the *live* set can lose `optimized` via invalidate-on-edit (and optionally regain it at interaction end).
- **Threading/publication contract**: the optimized soup is attached after parse completes and strictly before buffer meshes are built. On the async import path (`gltf_load_task.cpp:129-205`) the meshopt pass runs on the taskflow worker as part of the load task, before `start_build`. The synchronous parse sites (scene open, asset manager, prefabs, controller models) run it inline on the calling thread — they already block on parse. The slot is published once under the state mutex, following the `m_geometry_published` pattern (primitive.hpp:165, primitive.cpp:492-552). Parse dedups shared `Primitive` slots (`gltf_fastgltf.cpp:2316-2345`), so the attach loop must dedup by render_shape/soup pointer to avoid optimizing and hashing the same soup N times.
- Optimization runs at import time via **one shared editor helper**, producing the optimized soup; the filesystem cache stores the optimized derivation keyed by source-content hash.
- Memory note: both soups stay resident; source soup payload participates in the existing reloadable-asset-loads behavior. With `optimize_meshes` on, both `original` and `optimized` `Buffer_mesh` variants are resident — roughly **2× mesh GPU memory** — the accepted v1 trade-off for unconditional picking correctness and zero id-variant machinery. **Future work** to reclaim it: a dedicated minimal `id_renderer` variant (position + facet id + joints/weights for skinned) that lets the full `original` variant be skipped. Note in `notes.md`.

## Phase 0 — Remove the ID-buffer edge-line method (separate commit, before any meshoptimizer work)

- Delete the method end to end. Verified footprint:
  - Config: `use_id_buffer` field in `src/editor/config/definitions/content_edge_lines_config.py` — mark `removed_in=2` and bump struct version (codegen supports field removal first-class: `schema.py:21`, version-guarded deserialize, `[[deprecated]]` member); update both `config/editor/editor_settings.json` **and** `config/editor/openxr_editor_settings.json:135`.
  - Shaders: `ERHE_EDGE_LINES_FROM_ID` / `ERHE_VARIANT_FACE_ID_SEED` branches in `res/shaders/standard.vert`/`.frag`; the id-mode branches **embedded in the shared** `res/shaders/compute_before_content_line.comp` (lines ~218, 326, 501-503); `res/shaders/erhe_standard_variant.glsl:37`; `shader_key.hpp:132,134` bool-mask entries.
  - Content wide-line id mode (the method's largest component): `content_wide_line_renderer.hpp/.cpp` (`set_id_mode`/`m_id_mode`, `add_mesh`'s `Face_id_base_provider*` parameter), `content_wide_line_compute_renderer.cpp` (per-dispatch `id_base`), `content_wide_line_interface.hpp/.cpp` (`id_mode`/`id_base` view-UBO block fields — keep removal in sync with the shader block layout).
  - Renderers: `id_renderer.cpp` (`render_content_edge_id`, `render_content_seed`, seed/edge-id framebuffers, the `VARIANT_FACE_ID_SEED` pipeline, ~540-548) + `id_renderer.hpp`; `primitive_buffer.hpp:135`/`.cpp:386-395,493` (`Primitive_interface_settings::face_id_base_provider` + cached-draw-record eligibility condition); `app_rendering.cpp` (`edge_lines_from_id_capable`), `composition_pass.cpp/.hpp`, `viewport_scene_view.cpp/.hpp`, `render_context.hpp`, `forward_renderer.cpp/.hpp`, `camera_buffer.hpp`.
  - `Face_id_base_provider` has no other consumer (verified) — delete it. `Id_renderer::render_buckets` and `Primitive_color_source::id_offset`/id-ranges are shared with the picking path and **stay**.
- After this commit the ID renderer is the **only** consumer of `a_custom_0` (verified: every other read in standard.vert/.frag is under the deleted defines).
- **Verify:** desktop builds; wide-line edge methods (simple quad + surface tent) still work; settings migrate. Commit.

## Phase 1 — Dependency + skeleton wrapper (no behavior change)

- `CMakeLists.txt` (top level, near fmt/fastgltf blocks): add
  ```cmake
  CPMAddPackage(
      NAME              meshoptimizer
      GIT_TAG           <newest upstream release tag, e.g. v1.2>
      VERSION           1.2
      GIT_SHALLOW       TRUE
      GITHUB_REPOSITORY zeux/meshoptimizer
      OPTIONS "MESHOPT_BUILD_DEMO OFF" "MESHOPT_BUILD_GLTFPACK OFF" "MESHOPT_INSTALL OFF"
  )
  ```
- `src/erhe/primitive/CMakeLists.txt`: add `meshoptimizer` to the PRIVATE link list; add new sources.
- New `src/erhe/primitive/erhe_primitive/mesh_optimizer.hpp/.cpp`:
  - `class Mesh_optimize_options` (bools: weld, vertex_cache, overdraw, vertex_fetch; `float overdraw_threshold{1.05f}`).
  - `class Mesh_optimize_statistics` (before/after ACMR, overdraw, fetch bytes, vertex counts, elapsed).
  - `class Mesh_optimize_result` (optimized `std::shared_ptr<Triangle_soup>`, the remap needed for `Element_mappings` composition — the triangle permutation plus the **forward source→output** composite vertex remap (size `vertex_count_in`; weld-merged duplicates all map to the same output slot, which is exactly what corner composition needs — the output→source gather table is derivable from it, not vice versa) — and `Mesh_optimize_statistics`). The filesystem cache stores this same remap pair.
  - `auto optimize_triangle_soup(const Triangle_soup& source, const Mesh_optimize_options&) -> Mesh_optimize_result` — stub for now. meshoptimizer.h stays PRIVATE to the erhe_primitive target (mesh_optimizer.cpp now; primitive.cpp/primitive_builder.cpp gain it in phase 3).
- Delete stale `#meshoptimizer` at `src/editor/CMakeLists.txt:1032`; add tag entry to `scripts/dependencies_tags.sh`.
- **Verify:** `scripts/build_ninja_win_vulkan.bat`, VS vulkan-headless build, `scripts/build_and_run_tests.bat`, `scripts/build_android.bat quest` (NDK CMake 3.22). Commit.

## Phase 2 — Config fields (codegen)

- `src/erhe/scene_renderer/definitions/mesh_memory_config.py`: bump `version=3`; add `optimize_meshes` (Bool, `added_in=3`, default `"false"`, visible, developer=True) and `mesh_optimize_cache` (same), modeled on `quantize_vertex_positions`.
- `config/editor/mesh_memory.json`: `"_version": 3`, both keys `false`. Check the OpenXR settings variant for a mesh_memory section.
- No CMake changes (mesh_memory_config outputs already listed in `src/erhe/scene_renderer/CMakeLists.txt:81-109`; serialization .cpp compiled in editor at `src/editor/CMakeLists.txt:936-975`). **Build twice** (codegen double-build gotcha).
- **Verify:** build ×2, run editor, toggles appear in Settings window, old `_version:2` json migrates. Commit.

## Phase 3 — Soup-path optimization with source/optimized split

- Implement `optimize_triangle_soup()` in `mesh_optimizer.cpp`, operating on a **copy** of the source soup (float data — before any quantization):
  1. Analyze "before" stats (`meshopt_analyzeVertexCache/Overdraw/VertexFetch`).
  2. `meshopt_generateVertexRemap` over the full interleaved vertex (stride from `vertex_format.streams.front().stride`) — bitwise weld, lossless (joints/weights included in the compare, so skinned meshes are automatically safe); `meshopt_remapIndexBuffer` + `meshopt_remapVertexBuffer`, shrink `vertex_data`.
  3. `meshopt_optimizeVertexCache` in place on indices.
  4. `meshopt_optimizeOverdraw` with a temp `std::vector<float>` of positions (via `erhe::dataformat::convert` if the position attribute is not float3), threshold 1.05f.
  5. `meshopt_optimizeVertexFetch`.
  6. Analyze "after" stats; return optimized soup + stats.
- `Primitive_shape` (primitive.hpp/.cpp): add `m_optimized_triangle_soup` **plus its `Mesh_optimize_result` remap** (retained together on the shape — from fresh optimization or a cache hit alike — because the composition can only run later, when `mesh_from_triangle_soup` produces the source mappings inside `make_geometry_build_locked`, primitive.cpp:520-549; the composed optimized mappings are produced and published at that same state-mutex publish point, and the `ERHE_VERIFY` empty-mappings preconditions at primitive.cpp:513-514 are reworked for per-variant instances) + `set_optimized_triangle_soup()` (publish-once under the state mutex, `m_geometry_published` pattern) + the variant-aware `get_triangle_soup(Mesh_variant)` accessor. `make_geometry()`/`mesh_from_triangle_soup` stay on the source soup (single maximum-precision geometry). Add the `Mesh_variant { original, optimized }` slots in `Primitive_render_shape` per the architecture above: variant **set** carried in `Build_info`/`Buffer_info` (caller-prepared), whole `Element_mappings` instance paired with each built mesh (optimized instance by remap composition), variant-aware `get_renderable_mesh(Mesh_variant)` + presence query, renderer-side selection (forward/shadow/draw-list prefer `optimized` when present; `Id_renderer` uses `original`) plumbed through `bucket_primitives()` and draw-list registration, deferred commit swaps the whole built-variant set atomically.
- Sink position encode: route the quantization step through `meshopt_quantizeSnorm` at the two attribute-aware caller sites named in the evaluation above (`encode_position` branch in `build_buffer_mesh_from_triangle_soup()`, `Build_context::write_position()`), keeping erhe's AABB affine and the unchanged shader decode; verify bit-identical output vs the current path on a test mesh. Note: no existing typed `Vertex_buffer_writer::write` overload accepts int16 for snorm formats (`write_low3(int32_t*)` FATALs on `format_16_vec3_snorm`) — write through the writer's public span members or add a small raw-write helper; do not reach for the vec3i overload. Export code (`gltf_fastgltf.cpp:4908, 4964`) switches to `get_triangle_soup(Mesh_variant::original)` (source data, unchanged content).
- Wire import via **one shared editor helper** (e.g. `optimize_imported_primitives(...)` in `src/editor/parsers/gltf.cpp`): after parse, when `mesh_memory_config->optimize_meshes`, compute and attach the optimized soup per **unique** render_shape (dedup — parse shares `Primitive` slots). Call it from ALL parse sites: `parsers/gltf.cpp:791` (import op — attach after the `prepared_parse`/`adopted_parse` branch merge at ~line 808, so undo/redo re-import is covered), `parsers/gltf.cpp:1530` (`open_scene_gltf` — the primary scene-open flow, e.g. Bistro), `assets/asset_manager.cpp:1162`, `assets/gltf_load_task.cpp:191` (on the taskflow worker, before `start_build`), `prefabs/prefab_library.cpp:264`, `xr/controller_visualization.cpp:224`. These are all `parse_gltf` callers in src/editor; `src/example/example.cpp:153` also parses glTF but stays unoptimized by design (outside the editor). Keep `erhe_gltf` itself ignorant of optimization (source-as-is loading preserved; no `Gltf_parse_arguments` change needed).
- Per-primitive log line via `log_primitive` (info): counts, ACMR/overdraw/fetch before→after, weld delta, elapsed.
- **Verify:** all desktop builds; load a glTF scene with flag forced on; identical rendering; picking/paint/weight-paint on imported meshes **during the pre-finalize window or on a soup-only consumer** (steady state replaces the composed mappings with builder-produced ones at finalize, so testing after finalize passes vacuously); physics; glTF re-export produces **source** data byte-equivalent to a no-optimization export. Commit.

## Phase 3 verification (2026-08-27, ABeautifulGame.glb)

Controlled A/B on `res/editor/assets/ABeautifulGame.glb` — a scene chosen for
having no cameras, no animations and no skins, at a camera framing pinned over
MCP with DDGI off and the simulation clock paused (the harness is
`shot.py`; see `feedback_ab_screenshot_control_run`):

| comparison | viewport pixels differing | |
|---|---|---|
| off vs off (control) | 0 / 2 198 250 | runs are bit-reproducible |
| off vs **on** | **0 / 2 198 250** | optimized variant renders identically |

Not vacuous: the `on` run optimized **15 primitives**, e.g. `Bishop_Shared[0]`
74 768 triangles, ACMR 0.769 → 0.720, overdraw 1.152 → 1.035.

Two observations to carry forward, neither a correctness problem:

- **Sharing dedup is doing real work.** The asset has 15 distinct meshes
  referenced by 49 mesh-carrying nodes, and exactly 15 optimizations ran — the
  other 34 instances reused the shared `Primitive`'s variant, which is the
  behaviour the "optimize shared primitives once" requirement asks for.
- **Vertex fetch got *worse* on this asset** (2 795 008 → 3 064 576 bytes) while
  ACMR and overdraw improved, and the weld merged nothing (+0.0% vertices)
  because the asset is already welded. Overdraw reordering trades fetch
  locality away, and on an already-optimized asset there is nothing for the
  weld to win back. Worth deciding in phase 6/7 whether overdraw earns its
  fetch cost, ideally per-asset rather than globally — do not treat the
  increase as a bug on sight.
- **Cost is not negligible**: ~190 ms for a 74 k-triangle primitive, ~370 ms of
  the load for this small scene. That is the case for the phase 4 cache.

## Phase 4 — Filesystem cache (soup path only)

- New `optimize_triangle_soup_cached(source, options, cache_dir)` in `mesh_optimizer.cpp` (soup path only in v1 — the geometry path runs its passes uncached at build/finalize time, a deliberate cost decision recorded in phase 5b; say so in a comment).
- **Key:** xxhash (`erhe_hash/xxhash.hpp`) over source `index_data` + `vertex_data` + vertex-format description + options + a format-version constant. File `cache/mesh_optimizer/{hex}.emoc` relative to cwd (works on Quest per Android chdir; `erhe::file::ensure_directory_exists`).
- **Entry format (remap-based):** header `{magic 'EMOC', uint32 version, uint64 settings_hash, uint64 source_hash, uint32 vertex_count_in, uint32 vertex_count_out, uint32 triangle_count}` + triangle permutation (uint32 per output triangle) + **forward source→output** vertex remap (uint32 per source vertex). This pair fully determines the optimized soup: gather vertices by the (derived) output→source table, and rebuild indices as `perm`-ordered source triples mapped through the forward remap — no meshopt calls on hit, and the same pair drives the `Element_mappings` composition. Any mismatch or I/O error → miss → re-optimize, rewrite; corrupt/unwritable cache degrades to direct optimization with a warning, never fails a load.
- **Concurrent-safe writes:** loader workers may race on the same entry — write to a temp file and rename into place (the corrupt→miss fallback covers the remaining window).
- Add `erhe::hash` and `erhe::file` to `src/erhe/primitive/CMakeLists.txt` link list (neither is currently linked).
- Wire `mesh_optimize_cache` boolean at the import helper. Unbounded growth is accepted for v1 (note future LRU).
- **Verify:** load twice → second load logs hits; delete cache mid-session; change options → invalidation; Quest smoke test. Commit.

## Phase 4 verification (2026-08-27, ABeautifulGame.glb)

Same controlled A/B harness as phase 3, plus the window layout pinned - the
editor rewrites `config/editor/desktop_windows.json` on exit, and a different
layout moves the viewport, which reads as a ~96% pixel difference that has
nothing to do with rendering. Pin the layout for any run being compared.

- **Replay is correct.** Baseline (`optimize_meshes` off) vs a run served
  entirely from cache: **0 differing viewport pixels of 2 198 250**. Cold
  (optimize + store) vs warm (pure replay) likewise 0.
- **Corruption degrades to a miss, never to a failed load.** With one entry
  truncated, one filled with garbage and one emptied, the load logged 12
  primitives replayed and 3 freshly optimized, rewrote all three entries, and
  rendered 0 differing pixels against the baseline. No errors.
- **Content addressing dedups across primitives**: 15 primitives produced 8
  entries, because the black/white piece pairs are distinct glTF meshes with
  identical geometry and therefore hash the same.
- Per-primitive optimize time goes from ~30-190 ms to ~0 on a hit.

Deviation from the plan text: the key uses `erhe::hash::hash` (FNV-1a over the
bytes), not `erhe_hash/xxhash.hpp` - that header is a **compile-time** XXH32 for
constexpr strings and is not a runtime bulk hasher.

Note for phase 6: a cache hit does not run `meshopt_analyze*()`, so it has no
before/after figures. `Mesh_optimize_statistics::measured` says so, and the log
line reads "replayed from cache" rather than printing zeroes that look like an
optimization that achieved nothing.

## Phase 5 — Geometry-path full optimization (the steady-state win)

**Why full optimization here (user decision, reversing the earlier weld deferral):** in the editor, every imported glTF mesh's soup-built buffer mesh is replaced shortly after load by a geometry-path rebuild to gain edge lines — `deferred_finalize_mesh_items` (`async_raytrace_kickoff_operation.cpp:131-138`, gate fires for every fill-only soup mesh) or eager `finalize_imported_meshes` (`parsers/gltf.cpp:670-681`). The geometry path is therefore the steady-state chokepoint for BOTH imported and procedural meshes; the soup-path optimization (phase 3) covers the load-time window and soup-only consumers, and provides the cache. Split into two independently committed steps:

### Phase 5a — Deferred allocation (smaller than it sounds)
- The writers **already stage in CPU memory** (`Vertex_buffer_writer::vertex_data` / `Index_buffer_writer::index_data` are CPU vectors flushed once via `vertex_writer_ready`/`index_writer_ready` in the destructors, buffer_writer.cpp:500-537, 592-596). The actual work: move the GPU allocation **after** the build — the whole 5-allocation block (`allocate_vertex_buffers` + edge-line vertex + edge-line joint + expanded-fill + index, primitive_builder.cpp:48-58) moves **together, still under `buffer_mesh_allocation_mutex()`**, preserving the atomic-transaction guarantee; size writers from `mesh_info` counts instead of `buffer_range.count`; rework `is_ready()` (primitive_builder.cpp:905-911) and the lockstep base_vertex check; accept that allocation failure now surfaces after build work. Byte-identical output with optimization off (buffer-dump verification). This is also the seam LOD/meshlets will need.

**Phase 5a done (2026-08-27).** Two things the plan text did not account for,
both of which would have silently broken rendering:

- **`build_expanded_polygon_fill()` created its writers function-locally**, so
  they flushed mid-build - before the deferred allocation exists. They are now
  members of `Build_context` (`expanded_vertex_writers`), created in its
  constructor and flushed with the rest.
- **`build_edge_lines()` gated on `edge_line_vertex_buffer_range.count > 0`**,
  which is empty during the build once allocation is deferred - edge-line data
  would simply never have been staged, silently. Those gates now mirror the
  conditions `allocate_edge_line_*_buffer()` allocates under, the bytes stage
  into `Build_context` members, and `allocate_and_bind_writers()` enqueues them
  once the ranges exist.

Also: writers take a vertex/index COUNT instead of reading an allocated range,
`Index_buffer_writer` takes its element size from the index format rather than
from the range, and both writers gained `set_buffer_range()` plus a destructor
guard - without a destination a writer DROPS its staged bytes, so a failed
allocation can no longer flush at a default (pool 0, offset 0) range and
overwrite another mesh. `is_ready()` now asks the mesh counts rather than the
ranges. `allocate_index_range()`'s bounds check moved to `allocate_buffers()`
as a post-condition, since there is nothing to check against at call time.

Verified on ABeautifulGame.glb with layout, camera, DDGI and clock pinned:
**pre-5a vs post-5a with the optimizer off: 0 differing viewport pixels of
2 198 250** - the true before/after for this refactor, since it changes every
mesh build whether or not optimization is on. Optimizer off vs on after 5a is
also 0, and the procedural startup scene (platonic solids, geometry path) builds
and renders with no errors.

### Phase 5b — meshopt passes on the staged build
Pass order (weld **first** — post-Phase-0 the id attribute no longer blocks cross-facet merging; running the cache pass before weld would optimize a share-free index graph and forfeit the ACMR win):
1. **Weld:** `meshopt_generateVertexRemapMulti` over the **corner-vertex prefix only** (centroid-point vertices are appended to the same lockstep streams after the corners — `total_vertex_count = corners + centroids`, primitive_builder.cpp:66-70 — and must not enter the weld), called in **unindexed mode** (`indices = NULL`, `index_count == vertex_count`) so every corner gets a remap entry — a remap generated from fill indices alone would drop corners referenced only by corner-point indices (`build_corner_point_index()` emits for every corner unconditionally, primitive_builder.cpp:885-890, and fill can be disabled per primitive type). Compare streams are per-**attribute** subranges (ptr/stride/size triplets — `generateVertexRemapMulti` supports `stride > size` subranges by design; ≤16 streams, erhe's ~12 fit), covering every attribute **except** facet id, whose bytes are zeroed in the optimized output. Apply the forward remap to all streams' corner prefix, the fill indices, and the corner-point indices; compose `mesh_corner_to_vertex_buffer_index` and `mesh_vertex_to_vertex_buffer_index` through it; shrink counts.
2. **`meshopt_optimizeVertexCache`** then **`meshopt_optimizeOverdraw`** on the welded fill indices (overdraw reads float positions from a decode/staging copy). **Permutation recovery post-weld:** triples are no longer unique by third index, so build a triple→triangle-id hash multimap and consume matches in emit order (deterministic; collisions only for bitwise-identical — degenerate-duplicate — triangles). Permute `Element_mappings::triangle_to_mesh_facet` in lockstep; assert bijection.
3. **`meshopt_optimizeVertexFetchRemap`** (the multi-stream route — `optimizeVertexFetch` proper is single-stream by contract), generated from the **concatenated fill + corner-point index streams** (corner-point indices alone cover every corner, so nothing is dropped; `optimizeVertexFetchRemap` builds its remap solely from the indices passed) + one remap application across all corner-prefix streams, fill + corner-point indices, and both corner/vertex mappings.
4. **Centroid tail:** copy centroid vertices after the compacted corner prefix and rebase centroid-point indices by the shrink delta.
- Edge lines: indices consume `mesh_vertex_to_vertex_buffer_index` (primitive_builder.cpp:1303-1305) — correct via the composed mapping; the dedicated edge-line vertex buffer is written from GEO mesh data independent of main-buffer order — untouched. Expanded-fill (solid wireframe) is self-contained — untouched.
- Consumers of `mesh_corner_to_vertex_buffer_index` (paint_tool.cpp:308, weight_paint_tool.cpp:577, mesh_component_transform.cpp:719+) follow the **invalidate-on-edit contract** from the architecture section: the first edit drops the `optimized` variant (frame-safe release + re-registration) and writes only the `original` mesh, as today.
- Plumb `Mesh_optimize_options` via `Buffer_info`/`Build_info`, populated from config in `mesh_memory.cpp make_primitive_buffer_info()`. **Which config instance:** `Mesh_memory` holds a *copy* (`mesh_memory.hpp:289`), frozen at startup, while the Settings window mutates the editor's own instance that `AppContext::mesh_memory_config` points at (`app_context.hpp:158`, `editor.cpp:3032`). Read the `AppContext` instance if the toggle is to take effect on later builds without a restart; reading `Mesh_memory`'s copy freezes it at the startup value.
- Weld runs on post-encode vertex bytes (positions already snorm16 when quantization is on) — more merges, and GPU data equals staged data exactly. Hard edges (differing flat corner normals/tangents) correctly stay unwelded.
- Cost decision: geometry-path passes at finalize run **without** a disk cache (cache is soup-path-only in v1) — meshopt passes are fast relative to the geometry build itself and finalize is already deferred/async; extending the cache to geometry builds is future work if profiling disagrees.

- ID-render picking stays correct throughout: the `original` variant (valid facet ids) is always built, and `Id_renderer` selects it. Hook the shared `build_buffer_mesh` (primitive.cpp:794) so both entry points — synchronous `make_buffer_mesh(Build_info)` and the deferred `prepare_geometry_buffer_mesh` — build every variant the `Build_info` requests; on the deferred import flow all requested variants ride the same `Pending_buffer_mesh` and the **same `commit_geometry_buffer_mesh()` atomic swap** (worker prepares all, main thread commits all).
- During the pre-finalize async-load window imported meshes carry no `a_custom_0` attribute at all (no `custom_attribute_id` writes in gltf_fastgltf.cpp) — window picking behavior unchanged from today.
- **Verify (whole phase):** desktop builds; brush/procedural scenes and finalized imported scenes render identically (A/B per protocol); face/edge/vertex selection, paint tools, skinned picking, edge lines correct; log per-mesh vertex-count reduction + ACMR deltas (now expected to be materially nonzero on smooth surfaces). Commit per sub-phase.

**Phase 5b done (2026-08-27).** Built as four commits: config plumbing through
`Buffer_info`, a multi-stream refactor of the optimizer core, primitive-mode-aware
variant resolution, and the geometry-path variant itself.

Deviations from the recipe above, all deliberate:

- **The optimized variant is FILL TRIANGLES ONLY**, so the corner-point,
  edge-line and centroid-point index streams and the centroid vertex tail are
  simply not carried, and steps 1 and 4's handling of them is moot. Welding
  merges corners across facets, which is precisely what those streams exist
  per-facet to avoid; the original build has them all. The weld therefore runs
  in INDEXED mode over the fill indices (not the unindexed mode the plan text
  specifies), so a corner no triangle references is dropped rather than kept.
- **Whole-stride compare streams, not per-attribute subranges.** One
  `meshopt_Stream` per sink stream with `size == stride` covers every byte
  including padding, which is safe because the staging vectors are `resize()`d
  (zero-filled) and only attribute bytes are ever written. Enumerating
  attributes would have been more code for the same compare.
- **The facet id bytes are zeroed BEFORE the weld**, not after. This is not
  cosmetic: the id is per-facet, so leaving it in the compare makes every
  corner unique across facet boundaries and the weld merges *nothing*.
- **`Mesh_memory` holds a reference to the owner's `Mesh_memory_config`**
  rather than reading one of two copies. That removes the "which config
  instance" trap the plan flagged instead of documenting around it.

Also fixed on the way, both latent before this phase:

- `get_resolved_renderable_mesh()` now takes the `Primitive_mode`. It used to
  prefer the optimized build for any mode; an empty index range reads to every
  caller as "nothing to draw", so an edge-line pass would have dropped the
  primitive instead of falling back. Masked today only because the edge-line
  composition passes short-circuit into `Content_wide_line_renderer`, which
  pins itself to the original.
- `build_buffer_mesh()` builds the variant only when the caller passes an out
  pointer, so the tool / preview / hotbar builders no longer run a full meshopt
  pass and allocate a second index range plus one vertex range per stream for a
  shape that is discarded on return.

**Phase 5b verification (ABeautifulGame.glb and the procedural startup scene).**
Layout, camera, DDGI and clock pinned per the A/B protocol:

- Control pair (both optimizer off): **0 differing viewport pixels of 2 198 250**.
- Optimizer off vs on, imported scene: **0**. Procedural startup scene: **0**.
- **The variant is what renders**, mutation-checked: halving the optimized
  index buffer moves 34.0% of viewport pixels. Without that check a
  0-pixel result would equally have meant "the variant is never selected".
- Weld on the geometry path: 141 696 -> 25 957 vertices (-81.7%), ACMR
  3.000 -> 0.704 on the chess pieces. The soup path on the same asset welds
  nothing (+0.0%) because the glTF is already welded - the geometry path is
  where the win is, exactly as the phase-5 rationale predicted.
- Procedural brushes weld nothing (+0.0%): platonic solid corners carry
  distinct facet normals, so there is nothing to merge. Correct, not a failure.
- Builds green: ninja vulkan, VS opengl, null backend, vulkan-headless, Quest
  APK. Tests 641/641 (638 + 3 new multi-stream optimizer tests).

Recorded risks, neither introduced here:

- `make_optimized_render_shape_from_staged_build()` abandons a partial
  multi-pool allocation transaction when one stream's allocation fails - the
  successful ranges are released by `~Buffer_mesh` after the lock is dropped,
  into the retired list. `build_buffer_mesh_from_triangle_soup()` and
  `allocate_vertex_buffers()` already do the same. OOM-only.
- `Primitive::optimized_render_shape` has no lock of its own. Every writer
  today is either the serial import build loop or a main-thread commit; the
  member comment now says so.
- The snapshot duplicates the whole corner-vertex staging on top of the
  writers' own copies, and the optimizer allocates weld and fetch temporaries
  on top of that. A transient RSS spike on a Bistro-scale rebuild with several
  finalize workers in flight; measure in phase 7 before deciding it matters.

## Phase 6 — Statistics surfacing (optional polish)

- Aggregate per-import summary log (totals, average ACMR delta, cache hit rate) at the import helper.
- Optionally expose a running `Mesh_optimize_statistics` accumulator via the wrapper for the editor debug UI / MCP `get_memory_usage`-style tool (`src/editor/mcp/mcp_server.cpp`). Logs are the acceptance mechanism; keep this small. Commit.

**Phase 6 done (2026-08-27).** `log_mesh_optimize_statistics()` accumulates into
a process-wide `Mesh_optimize_totals`; `log_mesh_optimize_totals()` prints one
summary line; `get_memory_usage` gained a `mesh_optimization` section. ACMR and
overdraw are averaged TRIANGLE-WEIGHTED - they are per-triangle costs, so a
per-primitive mean would let a 4-triangle gizmo count as much as a
92 000-triangle chessboard. Cache hits contribute vertex counts but no ACMR /
overdraw / fetch figures, since they never ran `meshopt_analyze*()`.

Read the aggregate from `get_memory_usage`, not from the log: the geometry-path
optimizations land asynchronously in the deferred finalize tasks, and there is
no point in the log at which they are all in. The line
`finalize_imported_meshes` prints is the import-time (soup path) picture only.

Geometry-path log lines now carry a name. They were blank because the Geometry
on that path is derived from an imported soup and comes out unnamed;
`prepare_geometry_buffer_mesh()` takes the scene mesh's name instead.

**This settles the open question of whether overdraw earns its vertex-fetch
cost.** On ABeautifulGame the import-time totals show fetch **+5.9%** - the asset
is already welded, so reordering is all that happens, which is the effect the
phase-3 note recorded. The session total after the geometry finalize is fetch
199 387 520 -> 76 520 960 bytes (**-62%**), vertices 2 138 692 -> 833 132,
ACMR 1.956 -> 0.866, overdraw 1.207 -> 1.044. The weld's fetch win on the
corner-per-vertex geometry build dwarfs what overdraw reordering trades away, so
keep overdraw on. Re-check per asset if a scene ever ships pre-welded geometry
that never reaches the geometry path.

## Phase 7 — Verification sweep + flag flip

1. Builds: `scripts/build_ninja_win_vulkan.bat`, `scripts/build_ninja_win_clang.bat`, VS opengl + vulkan-headless, `scripts/build_and_run_tests.bat`, `build_android.bat quest` (Quest launch requires fresh user prompt + confirmation).
2. Visual A/B via editor MCP server (127.0.0.1:8080): reference glTF scene (e.g. Bistro) screenshots with `optimize_meshes` off vs on, **plus a same-config control pair** to bound noise (per A/B screenshot protocol: settling, viewport-crop diffs, DDGI off). Expect optimized-vs-baseline diff ≈ control diff.
3. Interaction sanity: facet picking, paint + weight paint, physics collision, glTF export/import round-trip (export must equal source-data export).
3b. **Position-quantization byte check (carried over from the phase-3 quantization sub-step):** load one glTF with `quantize_vertex_positions` on and dump the position vertex range, confirming the `meshopt_quantizeSnorm` route is byte-identical to a pre-change dump. The unit test (`src/erhe/dataformat/test/test_snorm16_quantization.cpp`) pins the two *functions* on `[-1, 1]`; only this pins the *buffers*. Reviewed FP-flag risk is low (`/fp:fast` global, `/arch:AVX2` off, so no FMA contraction), but the check is the plan's stated acceptance.
4. Perf: logged ACMR/overdraw deltas and vertex-count reductions, frame time on a heavy scene measured in **steady state** (after geometry finalize, where phase 5 is the active optimization; desktop GPU timers, Quest via Perfetto), import wall time with/without cache.
5. Flip `optimize_meshes` to `true` in `config/editor/mesh_memory.json` (cache default stays user's call); update `src/erhe/primitive/erhe_primitive/notes.md`; final commit. **Quest note:** `migrate_android_assets_to_writable()` (`erhe_file/file.cpp:629-636`) never overwrites a config file that already exists in writable storage, so a shipped default flip does not reach an existing install — verifying it on Quest requires an uninstall + clean reinstall, not `install -r`.

## Risks / notes

- Welding is bitwise-only: vertices differing in any attribute bit stay separate — correct by construction, gains vary by asset. `meshopt_generateVertexRemap` compares all `vertex_size` bytes **including padding** — verify soup strides have no non-zeroed padding bytes (assert or memset on construction) before welding.
- The CPU BVH disk cache is keyed by a content hash over triangle positions (`bvh_geometry.cpp:265,289-291`): enabling optimization reorders triangles ⇒ a one-time full BVH cache miss/rebuild sweep on first optimized load. Expected, note in logs.
- Per-step discipline (per project workflow): edit → build → independent review → fix severe → commit; split logically separate changes into separate commits.
- meshoptimizer is CPU-only, backend-agnostic; must keep linking in headless/null and tests configs (covered in Phase 1 verification).
- Future work seams recorded, not implemented: LOD chains (`meshopt_simplify`, natural fit on the phase-5a staging seam), meshlets, cache size cap.
- **`Primitive_render_shape` holds a `std::array<Buffer_mesh, 2>` unconditionally**, so the second slot's CPU-side storage (several empty `std::vector`s and range structs per shape) is paid even with `optimize_meshes` off. The accepted 2× cost in this plan is GPU memory *when the feature is on*; this is a small CPU cost when it is off. Collapsing it would mean an optional/heap slot, traded against an extra indirection on the draw path - not done, recorded.
- **Two position-quantization sites left unconverted** (found by the phase-3 quantization review, both out of that sub-step's scope):
  - `Build_context::build_centroid_position()` (`primitive_builder.cpp:866`) reaches `write_position()` without the `ERHE_VERIFY(std::isfinite(...))` that `build_vertex_position()` (:624) has. A NaN centroid used to be UB (`static_cast<int16_t>(NaN)`); it is now a defined `-32767`. Strictly an improvement, but adding the verify would make a broken mesh fail loudly like the vertex path does - deliberately not done here because it would turn previously-silent scenes into aborts.
  - `mesh_component_transform.cpp:775-777` is a **third** position quantizer (`std::lround(clamped[i] * 32767.0f)`) for live-drag GPU writes. Its comment claims the same rounding as `float_to_snorm16()`, which is not exact: `lround` rounds the product, while `float_to_snorm16` adds `0.5f` and then truncates, and that addition can itself round up. They can differ by one for values just under a tie. Converting it to `meshopt_quantizeSnorm` would make one quantizer for all positions.
- Scope note: soup-path optimization (phase 3) benefits the load-time window (before geometry finalize), soup-only consumers (e.g. controller visualization), and feeds the filesystem cache; the durable editor rendering win comes from phase 5. Phase-7 perf must measure steady state (after finalize), where phase 5 is the active optimization.
