# meshoptimizer integration

Live document for the mesh-optimization subsystem built on
[zeux/meshoptimizer](https://github.com/zeux/meshoptimizer) (vanilla
upstream, pinned via CPM): requirements, design, verification, future
work, traps. History lives in the git log, not here.

Status: implemented and verified as described below, including the
real-mouse interactive session -- but the 2026-08-29 requirements 9-11
(base variant never quantized, quantization only in the optimized variant,
edit-blocked background re-optimization) are a planned rework, NOT yet
implemented: today position quantization applies to both variants. See
Future work item 1. `optimize_meshes` defaults to **true** in
`config/editor/mesh_memory.json`; `mesh_optimize_cache` stays **false**
(the user's call).

## Requirements

User-confirmed; the first is a hard requirement.

1. **Source vs optimized asset separation.** Source assets are loaded as-is
   and never mutated. Rendering uses a derived optimized build; export,
   ground truth, geometry reconstruction, CPU raytrace and physics collision
   always use source data at full precision.
2. Optimize **all renderable meshes** -- imported and procedural: vertex
   weld/remap, vertex-cache order, overdraw order, vertex-fetch order, with
   before/after `meshopt_analyze*` statistics. LOD and meshlets are
   explicitly future work -- seams left, nothing implemented.
3. Config: `optimize_meshes` and `mesh_optimize_cache` in the code-generated
   `Mesh_memory_config`. `optimize_meshes` is on by default in the editor
   config; the cache is off. The filesystem cache must work on Android/Quest.
4. **Shared primitives are optimized once, as one** -- glTF import dedups
   `Primitive` slots and brushes share `Primitive` objects; instances reuse
   the shared variant.
5. **Picking and ID rendering stay unconditionally correct**: the `original`
   (source-order, per-corner) build is always present; the optimized build
   cannot serve ID rendering even by mistake (it lacks the facet-id
   attribute entirely).
6. Live mesh edits (paint, weight paint, vertex drag) keep working: the
   optimized build is invalidated on the first edit, never written through.
7. Vertex-position quantization is routed through `meshopt_quantizeSnorm`,
   bit-identical to the previous `float_to_snorm16` path (verified at the
   buffer level, not just the function level).
8. Prerequisite (done first): the ID-buffer edge-line method
   (`content_edge_lines.use_id_buffer`) was **removed end to end** -- after
   that, the per-corner facet-id attribute (`a_custom_0`) has exactly one
   consumer, the ID renderer, which uses the always-built original variant.

Added 2026-08-29, after the real-mouse session exposed the out-of-AABB drag
clamp (the snorm16 encode of the base variant's GPU buffer cannot represent
a position outside the build-time AABB). **NOT yet implemented** -- Future
work item 1:

9. The unoptimized **base** variant never uses position quantization:
   full-float positions. In the editor, every primitive always maintains
   this variant, so it is always renderable -- and in-place GPU edits can
   express any position (the drag clamp disappears with the encoding).
10. The **optimized** variant is the only build that applies quantization.
    Because the base variant is always available, the optimized variant can
    be built lazily on request, in the background.
11. During a mesh edit operation (paint, weight paint, active mesh-component
    move) the optimized variant is invalidated as soon as the edit starts,
    re-optimization is blocked for the duration of the edit, and at edit end
    an optimization task can start on a background thread.

## Design

### Where the variants live: `Primitive` (settled -- do not re-litigate)

`Primitive` holds the source `Primitive_render_shape` plus one
`shared_ptr<Primitive_render_shape>` per extra variant
(`optimized_render_shape`; null when optimization is off). The optimized
variant is an ordinary geometry-less render shape -- a category that already
existed and was in use -- holding its own triangle soup, its own composed
`Element_mappings`, and its own `Buffer_mesh`.

Two alternatives are rejected:
parallel variant arrays inside `Primitive_shape` / `Primitive_render_shape`
(pays for the second slot in every primitive whether or not the feature is
on, and pairs mesh with mappings by matching array index instead of by
ownership) and variant sets on `erhe::scene::Mesh` (duplicates per-instance
state -- `material`, `lightmap_uv_scale_offset` -- for a choice that is
per-*pass*, never per-instance, and puts the draw-list key
`(object_index, mesh_primitive_index)` at risk). `Primitive` is the only
placement where the mappings live in the same object as the mesh they
describe *and* the off-cost is one null pointer *and* `Mesh` /
`Mesh_primitive` / the draw-list key stay untouched. `Primitive` is also
already the sharing/dedup unit, which is what satisfies requirement 4
directly.

Consequences designed against:

- **Cross-variant publication is not one mutex.** Attach/publish the
  optimized shape only *after* the source commit succeeds; on invalidate,
  detach the optimized shape *first*. A renderer never sees a stale
  optimized mesh beside a committed new source one.
- The optimized shape **never builds its own `Geometry` or raytrace** --
  nothing reaches it to try: every geometry, raytrace, collision and export
  site goes through `primitive.render_shape` / `get_shape_for_raytrace()`,
  neither variant-selected. BLAS (scene TLAS, lightmap baker) builds from
  the **source** shape, which is what removes any BLAS-eviction-on-edit
  requirement.
- **First-optimization-wins** publish-once discipline (two loader workers
  may race on the same shared primitive).
- `Primitive::optimized_render_shape` has **no lock of its own**: every
  writer is either the serial import build loop or a main-thread commit
  (the member comment says so).

### The optimized variant's properties

- **Fill triangles only.** Edge lines, corner points, centroid points and
  the expanded solid-wireframe copy exist per-facet precisely to keep
  corners apart -- which is what welding undoes. The original build carries
  them all.
- **Its own vertex format, without the facet id**
  (`Buffer_info::optimized_vertex_format`). The id is per facet and the
  build is welded, so no value describes a merged vertex. Dropping the
  attribute (a) makes the variant unusable for ID rendering by construction
  rather than by convention, (b) takes the id out of the weld compare --
  leaving it in merges *nothing*, which is the whole win -- and (c) saves 4
  bytes per vertex. **Both paths (soup and geometry) build in that format,
  or it is not an invariant.** `Mesh_memory` derives the optimized formats
  from the content ones in its constructor; `get_all_vertex_formats()` is
  the single list that repack, lockstep block sizing and vertex-input
  registration all read.
- **`get_resolved_renderable_mesh()` takes the `Primitive_mode`** and
  prefers the optimized build for fill modes only. An empty index range
  reads to every caller as "nothing to draw", not "ask the other build",
  so preferring the variant for an edge-line pass would drop the primitive
  instead of falling back.
- **Every selection point is explicit**: the no-arg mesh/mappings accessors
  were deleted, so the compiler enumerates the ~21 sites that name a
  variant; `bucket_primitives()` takes a variant preference and its buckets
  carry the chosen `Buffer_mesh*` downstream, so record/draw fill sites
  follow the flowed choice instead of re-deriving a hidden default.
  `Id_renderer`, BLAS sources and glTF export always use `original`.

### The soup path (import time)

`optimize_triangle_soup()` (erhe_primitive `mesh_optimizer.hpp/.cpp`;
meshoptimizer.h stays private to the target) operates on a copy of the
source soup, on float data: analyze-before, `meshopt_generateVertexRemap`
(bitwise weld over the full interleaved vertex -- joints/weights included,
so skinned meshes are automatically safe), `meshopt_optimizeVertexCache`,
`meshopt_optimizeOverdraw` (threshold 1.05, float positions from a staging
copy), `meshopt_optimizeVertexFetch`, analyze-after. The result carries the
optimized soup plus the remap pair -- the triangle permutation and the
**forward source-to-output vertex remap** -- which drives both the
`Element_mappings` composition and the filesystem cache. The **soup path
passes empty (`{}`) source mappings to `compose_element_mappings()`** (its
remaps are not over the vertex buffer the source mappings index; contrast
the geometry path below).

One shared editor helper attaches the optimized soup after parse, per
**unique** render_shape (parse dedups shared `Primitive` slots), called from
all editor parse sites (import operation, `open_scene_gltf`, asset manager,
async load task, prefab library, controller visualization). `erhe_gltf`
itself stays ignorant of optimization; `src/example` stays unoptimized by
design.

The soup path covers the load-time window and soup-only consumers, and
feeds the cache; the durable win is the geometry path.

### The geometry path (the steady-state win)

In the editor, every imported soup mesh is replaced shortly after load by a
geometry-path rebuild (for edge lines) via the deferred finalize, and
procedural meshes only ever build here -- so this path is the steady-state
chokepoint. It emits one vertex per **corner**, i.e. it is unwelded by
construction, which is where the headline numbers come from.

- **Deferred allocation**: writers stage in CPU memory sized from mesh-info
  counts; the whole multi-buffer GPU allocation moves after the build,
  still as one transaction under `buffer_mesh_allocation_mutex()`. A writer
  never handed a destination range **drops** its staged bytes (destructor
  guard), so a failed allocation cannot flush to a default range and
  overwrite another mesh.
- **The meshopt passes run on the staged bytes**, post-position-encode
  (positions already snorm16 when quantization is on -- more merges, and
  GPU data equals staged data exactly). The weld runs in **indexed** mode
  over the fill indices (a corner no triangle references is dropped --
  fine, the variant is fill-only), with **whole-stride compare streams**
  (`size == stride` per sink stream; safe because staging vectors are
  `resize()`d zero-filled). Then vertex-cache + overdraw with permutation
  recovery via a triple-to-triangle-id multimap consumed in emit order
  (deterministic; collisions only for bitwise-identical degenerate
  triangles; `triangle_to_mesh_facet` permuted in lockstep, bijection
  asserted), then `meshopt_optimizeVertexFetchRemap` (the multi-stream
  route). The **geometry path passes the build's own mappings** to
  `compose_element_mappings()` -- same function as the soup path, opposite
  rule.
- **No disk cache on this path** -- deliberate: the passes are fast
  relative to the geometry build itself and finalize is already deferred /
  async. Extend the cache here only if profiling disagrees.
- Both entry points -- synchronous `make_buffer_mesh(Build_info)` and the
  deferred prepare/commit import flow -- build every variant the
  `Build_info` requests; the deferred flow prepares all variants on the
  worker and commits them in the one atomic swap. Tool / preview / hotbar
  builders that discard the shape on return do not build the variant at
  all.

### The filesystem cache (soup path only)

`optimize_triangle_soup_cached()`: key is `erhe::hash::hash` (runtime
FNV-1a -- NOT `erhe_hash/xxhash.hpp`, which is a compile-time string hash)
over source index data + vertex data + vertex-format description + options
+ a format-version constant; entries live in `cache/mesh_optimizer/` under
the cwd (works on Quest via the Android chdir). An entry stores the
**remap pair**, not the optimized soup -- the pair fully determines it
(gather vertices through the derived output-to-source table, rebuild
indices as permutation-ordered triples through the forward remap), so a hit
runs no meshopt calls and the same pair feeds the mappings composition.
Writes are temp-file + rename (concurrent loader workers); any mismatch,
truncation or I/O error degrades to a miss with a warning -- **never a
failed load**. Content addressing dedups identical geometry across distinct
glTF meshes. Unbounded growth is accepted for v1.

### Invalidate-on-edit

GPU vertex edits (paint colors, weight paint, live-drag positions) address
the per-corner original buffer through the mappings. Writing through the
welded variant is not possible -- merged corners share one slot -- so the
first edit calls `Mesh::invalidate_optimized_primitive_variant()` **before
its first GPU write**: frame-safe drop of the optimized shape, and
re-registration of **every mesh sharing that `Primitive`** (draw-list
records bake the drawn variant's base_vertex and index ranges, and
instances share Primitives). Renderers then see the variant missing and
fall back to `original`. This also dissolves any weld-vs-addressability
divergence: only one variant is live during an edit.

Today nothing rebuilds the optimized variant after an invalidate (it stays
gone until the next full primitive rebuild). Requirement 11 extends this:
invalidate at edit start, block re-optimization for the edit's duration,
background re-optimize at edit end -- Future work item 1.

### Statistics

Per-primitive log line (counts, ACMR / overdraw / fetch before-after, weld
delta, elapsed) plus a process-wide `Mesh_optimize_totals`; the MCP
`get_memory_usage` tool's `mesh_optimization` section is the only complete
aggregate -- geometry-path optimizations land asynchronously in deferred
finalize tasks, so no single log point has them all (the
`finalize_imported_meshes` line is the soup-path picture only, and says
so). ACMR and overdraw are averaged **triangle-weighted** (per-triangle
costs; a per-primitive mean would weight a 4-triangle gizmo like a
92k-triangle board). Cache hits contribute vertex counts but no
ACMR/overdraw/fetch (they never ran `meshopt_analyze*`; the log says
"replayed from cache" instead of printing zeroes).
`fetch_bytes_before/_after` are **both in the optimized format's stride**,
isolating what the passes did; the 4 bytes/vertex the format change saves
is a separate, known figure.

### Position quantization via meshoptimizer

The existing affine AABB `(p - center) * inv_scale` encoding stays
(meshoptimizer has no affine helper; its exponent-based filters would
change the shader decode). The quantization step routes through
`meshopt_quantizeSnorm(v, 16)` at the two attribute-aware caller sites --
the `encode_position` branch of the soup buffer build and
`Build_context::write_position()` -- writing snorm16 directly instead of
handing pre-scaled floats to the format-agnostic convert machinery. It is
bit-identical to the old `float_to_snorm16` **only because both encode
sites pre-clamp to [-1, 1]** (the two functions differ in clamp order,
unreachable inside that interval). Shader decode unchanged.

This section describes the current implementation, where quantization
applies to **both** variants. Requirements 9-10 move it: the base variant
returns to full-float positions and the snorm16 encode survives only in
the optimized build -- Future work item 1 carries the consequences.

### Config plumbing

`Mesh_memory` holds a **reference** to the owner's `Mesh_memory_config`,
not a copy -- a copy froze the Settings toggle at its startup value.
`Mesh_optimize_options` flows through `Buffer_info` / `Build_info`,
populated in `make_primitive_buffer_info()`.

## Verification

Rendering identity is verified with A/B screenshot runs per the harness in
"Future work"; figures are differing viewport pixels of 2 198 250, each
against a same-config control pair (always 0).

- **Soup path** (ABeautifulGame): off vs on **0**, with 15 primitives
  genuinely optimized (15 distinct meshes across 49 nodes -- the sharing
  dedup at work). Uncached optimize cost is ~30-190 ms per primitive,
  which is what justifies the cache.
- **Cache**: baseline vs fully-cached **0**; cold vs warm **0**;
  deliberately corrupted entries re-optimize and still compare at **0**.
  Content addressing dedups identical geometry (15 primitives -> 8 entries
  on this asset). Hit cost ~0 ms.
- **Deferred allocation** (not flag-gated, affects every build):
  optimizer-off output compares at **0** against the pre-restructure
  build.
- **Geometry path**: off vs on **0** on imported and procedural scenes,
  and **mutation-checked** -- halving the optimized index buffer moves
  34.0% of viewport pixels, ruling out "the variant is never selected".
  Measured characteristics: the corner-per-vertex geometry build welds
  141 696 -> 25 957 vertices (-81.7%), ACMR 3.000 -> 0.704 on
  ABeautifulGame's chess pieces; the soup path welds +0.0% there (the
  glTF is already welded), and procedural platonic solids weld +0.0%
  (distinct flat corner normals) -- both correct.
- **Overdraw stays on**: import-time fetch is +5.9% on an already-welded
  asset, but the session total after geometry finalize is vertices
  2 138 692 -> 833 132, fetch 199 387 520 -> 76 520 960 bytes (**-62%**),
  ACMR 1.956 -> 0.866, overdraw 1.207 -> 1.044 -- the weld's fetch win
  dwarfs what the reordering trades away.
- **Bistro**: off vs on is **3575 pixels (0.163%)** with control pairs at
  0 -- explained, not a defect: with the two reorder passes off (weld +
  fetch on) the run is **0**, so the welded build rasterizes bit-exactly
  and the residue is triangle order meeting order-dependent shading in
  alpha-blended foliage (one-LSB differences, visually
  indistinguishable). See Traps.
- **Interaction sanity** (headless, run on and off): 52 `pick_at` probes
  hit the same node / mesh / **facet id** in both runs (hit positions may
  differ ~1e-6 m -- the reordered BVH's rounding); a convex-hull drop
  onto a mesh collision shape rests at a **bit-identical** position; glTF
  export/import round-trips to exactly the source
  vertex/edge/facet/corner counts.
- **Position-quantization byte check** (via the `get_mesh_buffer_info` /
  `get_mesh_buffer_data` MCP tools, which read the actual GPU bytes and
  name which build they read): stored int16 position triples match both
  quantizers exactly, and a 0.01% AABB-scale perturbation flips every
  comparison -- the negative control that makes the zeros meaningful. The
  tools also make the variant invariants observable (fill-only index
  ranges, the narrower facet-id-free stride).

- **Real-mouse interactive session** (2026-08-29, `optimize_meshes` on):
  paint, a shared-`Primitive` edit (two instances, one edited, both stay
  correct), and an in-AABB vertex drag all behave correctly against a live
  draw list -- the invalidate path works in a real frame, not just the 4
  unit tests. No shader-compile stutter was noticed. The out-of-AABB drag
  clamped-and-popped exactly as then designed -- which is the observation
  that produced requirements 9-11: the design goal is now that the user
  never sees the clamp.

Unit tests cover the multi-stream optimizer core and live-edit variant
invalidation. What verification remains is listed in "Future work".

## Future work

1. **Move quantization into the optimized variant (requirements 9-11).**
   Base variant builds full-float positions; the optimized variant is the
   only quantized build; edits invalidate at edit start, block
   re-optimization while active, and queue a background re-optimize at edit
   end (lazy/on-request build is allowed because the base variant is always
   renderable). Known consequences to design against:
   - The content vertex format's position returns to
     `format_32_vec3_float`; the optimized format takes the snorm16
     position (it already has its own format and per-primitive
     scale/offset in the record). Both flow through every
     `get_all_vertex_formats()` consumer, and the shader position-encoding
     axis becomes a property of which variant a draw uses.
   - The snorm16 branch of the live-drag write-back
     (`mesh_component_transform.cpp`, clamp + warn-once) goes dead --
     remove it, and with it the third-quantizer seam recorded below.
   - BLAS builds from the source/base shape, so its snorm16-input device
     gate stops being exercised on that path.
   - The disk-cache key covers vertex data + options; the encoding move
     bumps the cache format-version constant.
   - Memory: the always-resident base variant grows back to 12
     bytes/position; the steady-state fill win (welded + quantized
     optimized variant) is what quantization keeps. The minimal
     `id_renderer`-variant seam below is the recorded way to reclaim the
     base cost later.
   - Doc/vertex-position-quantization.md describes the current
     both-variants encoding; update it when this lands.
2. **Perf.** Not measurable headlessly (the frame pacer reports tier "OFF"
   and no MCP surface reports GPU frame time). Use the Frame Pacing window
   or a GPU capture on the RELEASE build; Bistro is ~119 ms/frame in
   Debug. The static side is measured (the -62% fetch figures above).
   Also measure the transient RSS spike of the staging snapshot + optimizer
   temporaries on a Bistro-scale rebuild with several finalize workers.
3. **Quest verification of the flipped default.** The default flip itself is
   done (`optimize_meshes` true in `config/editor/mesh_memory.json`; the
   cache default stays the user's call). Quest verification needs
   UNINSTALL + CLEAN REINSTALL (`migrate_android_assets_to_writable()`
   never overwrites an existing config), and every OpenXR launch needs a
   fresh user prompt + explicit confirmation.
4. **Shader-compile stutter watch.** Nothing was noticed in the 2026-08-29
   session, but the exposure window is real: two content vertex formats
   double content-shader variants while meshes of both formats exist (the
   load window, and any mesh whose variant an edit dropped). Keep watching;
   the format split survives the item-1 rework in mirrored form.

Recorded seams, deliberately not implemented: LOD chains
(`meshopt_simplify`, natural fit on the deferred-allocation staging seam),
meshlets, a cache size cap / LRU, a dedicated minimal `id_renderer` variant
(position + facet id + joints/weights) that would let the full original
variant be dropped and reclaim the accepted 2x GPU mesh memory, a
geometry-path disk cache (only if profiling disagrees), and an
`ERHE_VERIFY(isfinite)` on the centroid position path (deliberately not
added: it would turn previously-silent broken scenes into aborts). Two
former seams are absorbed by Future work item 1: the optimized rebuild
queued at interaction end is now required by requirement 11, and the
live-drag quantizer branch (a third position quantizer whose `lround`
rounding can differ by one from the other two near ties) is removed
outright instead of converted to `meshopt_quantizeSnorm`.

### How to verify: the A/B screenshot harness

`scripts/mesh_ab_capture.py <off|on|cache> <out.png>`; its docstring
carries the full rationale. Every one of these matters:

- MCP port is 3743, NOT 8080.
- `set_ddgi {enabled:false}` -- the single biggest source of run-to-run
  divergence -- and `advance_time {mode:"paused"}`.
- The harness pins the window layout itself
  (`cache/desktop_windows.pinned.json`, snapshotted on first run). The
  editor rewrites `config/editor/desktop_windows.json` on exit, and a
  different layout moves the viewport -- a ~96% pixel difference that is
  not a rendering difference. `desktop_windows.json`,
  `editor_settings.json` and `mesh_memory.json` are session state: restore
  with git checkout after runs.
- Crop the ImGui panels out (viewport starts near x=350, y=75 at
  2304x1200).
- ALWAYS run a same-setting control pair first -- and a passing control
  pair is NECESSARY, NOT SUFFICIENT: a saturated or black viewport
  compares equal to another one (check the saturated-pixel fraction), and
  the code path under test may not be reached at all -- MUTATION-CHECK it
  (halving the optimized index buffer moved 34.0% of viewport pixels).
- Kill leftover editors: the harness refuses to start when something
  already answers on 3743 -- a leftover keeps the port, the new editor
  fails to bind, and every RPC then drives the OLD process with the OLD
  config.

Bistro, specifically:

    ERHE_SHOT_SCENE=res/editor/assets/niagara_bistro/bistro.gltf
    ERHE_SHOT_EXPOSURE=0.001
    ERHE_SHOT_FRAME=0
    ERHE_SHOT_EXE=build_ninja_win_vulkan_release/src/editor/editor.exe

Bistro saturates the viewport to near-white at default exposure (still
almost entirely clipped at 0.02, measured); 0.001 makes it readable.
`ERHE_SHOT_FRAME=0` keeps the asset's authored camera: `frame_scene()`
costs one RPC per node -- thousands of round trips on Bistro, minutes per
run, looks like a hang -- and the authored camera is just as deterministic.
Both sides of one comparison must use the same executable.

Reference baselines for the current vertex format (all 0 differing
viewport pixels; untracked, in `logs/` of the machine the runs were made
on): `logs/p7b_off_a.png` / `p7b_off_b.png` (control), `p7b_on.png`,
`p7c_on.png`, `p7c_proc_off.png`, `p7c_proc_on.png`.

## Traps

Each of these cost a review or debugging round. Do not rediscover them.

- **Attach the optimized variant BEFORE `mesh->update_rt_primitives()`
  registers the mesh with the draw list.** Registration bakes `base_vertex`
  and the index ranges of whichever variant is live at that moment.
- **An empty index range reads as "nothing to draw", not "fall back".**
  Variant resolution must be `Primitive_mode`-aware, or an edge-line pass
  drops the primitive instead of using the original.
- **The facet id must be OUT of the weld compare** -- leaving it in merges
  nothing. The mechanism is the separate optimized vertex format; both
  paths must build in it, and a format missing from any consumer of
  `get_all_vertex_formats()` (repack, lockstep sizing, vertex-input
  registration) fails silently and differently.
- **`compose_element_mappings()`: the soup path passes `{}` for source
  mappings, the geometry path passes the build's own.** Same function,
  opposite rule.
- **`Mesh_memory` must hold a REFERENCE to the owner's config** -- a copy
  freezes the Settings toggle at its startup value.
- **Invalidate before the first GPU write, and re-register every mesh
  sharing the Primitive** -- not just the edited one.
- **Welding is bitwise over every byte including padding** -- safe only
  because staging is `resize()`d (zero-filled) before attribute writes.
  Keep that property or the weld silently degrades.
- **The two snorm16 quantizers agree only inside [-1, 1]** -- their clamp
  orders differ; the encode sites' pre-clamp is what makes them
  bit-identical.
- **A cache hit has no analyze statistics** -- do not read its zeroes as
  "optimization achieved nothing"; `Mesh_optimize_statistics::measured`
  and the log wording exist for this.
- **Read the optimization aggregate from `get_memory_usage`, not the
  log** -- geometry-path optimizations land asynchronously; no log point
  has them all.
- **Bistro's off-vs-on residue (0.163%, one-LSB alpha-foliage pixels) is
  the triangle reordering, not a weld defect** -- do not chase it again;
  the weld+fetch-only run is pixel-exact.
- **Enabling optimization reorders triangles, so the CPU BVH disk cache
  (keyed on triangle positions) takes a one-time full miss/rebuild sweep
  on first optimized load.** Expected; noted in logs.
- **`get_mesh_attribute_values` reads SOURCE geometry, not GPU bytes** --
  buffer-level checks need `get_mesh_buffer_info` / `get_mesh_buffer_data`.
- **OOM only, known**: a failed mid-transaction multi-pool allocation in
  the optimized-build path abandons the partial transaction (successful
  ranges released via `~Buffer_mesh` into the retired list) -- the same
  behavior as the pre-existing allocation sites.
