# Plan: Geometry-Shader Fallback for `Content_wide_line_renderer` (OpenGL only)

Status: deferred. Not scheduled for implementation. For now, `rendering_test`
on OpenGL has no wide-line rendering when `force_no_compute_shader: true`
(or any GL context < 4.3).

## Context

`erhe::scene_renderer::Content_wide_line_renderer` was built as a compute-only
renderer so Metal (no geometry shaders) could render mesh edge lines. On
OpenGL builds with `force_no_compute_shader: true` (or any GL context < 4.3)
there is no fallback -- `m_enabled` stays false and nothing renders. This is
visible today in `rendering_test`: with
`src/rendering_test/config/erhe_graphics.json` `"force_no_compute_shader": true`,
the `cube_with_edge_lines` cell loses its wide lines and the
`stencil_wide_line` cell early-returns before even drawing its stencil cube.

Elsewhere in the codebase geometry-shader wide-line expansion already exists:

- `erhe::renderer::Debug_renderer` (try-compile + fall back pattern at
  `src/erhe/renderer/erhe_renderer/debug_renderer.cpp:204-234`)
- Editor's `wide_lines_{draw,vertex}_color` programs used by
  `forward_renderer` (gated at
  `src/editor/renderers/programs.cpp:112-116`)

But `Content_wide_line_renderer` itself is compute-only, so any client that
uses it (including `rendering_test`) has no fallback. This plan adds a
geometry-shader path inside the renderer so all clients get it automatically.

Goal: on OpenGL with `use_compute_shader == false`, `Content_wide_line_renderer`
selects a geometry-shader path that consumes the same edge-line vertex buffer
directly (GL_LINES), expands each line to a triangle strip in the GS, and
feeds the existing content-line fragment shader. Metal/Vulkan are untouched
because `use_compute_shader` is always true there.

## Design

### Path selection

Add a public enum on the renderer:

```cpp
enum class Path { disabled, compute, geometry };
```

Selection (decided inside `set_shader_stages()`):

1. `use_compute_shader == true` + valid compute+graphics stages => `compute`.
2. Else if a valid geometry-shader pipeline was supplied => `geometry`.
3. Else => `disabled`.

Follow `Debug_renderer`'s try-compile-and-fall-back pattern: the caller
compiles the GS `Shader_stages` externally and hands in a pointer. If
`!stages->is_valid()`, the renderer stays in `disabled`. No new `Device_info`
capability flag -- this mirrors existing erhe patterns.

`is_enabled()` returns true for `compute` or `geometry`.

### API additions on `Content_wide_line_renderer`

```cpp
[[nodiscard]] auto get_path                  () const -> Path;
[[nodiscard]] auto get_input_assembly        () const -> erhe::graphics::Input_assembly_state;
[[nodiscard]] auto get_edge_line_vertex_format()       -> erhe::dataformat::Vertex_format&;
```

Path-aware behavior of existing accessors:

- `get_graphics_shader_stages()`: returns VS+FS (compute) or VS+GS+FS
  (geometry) depending on path.
- `get_vertex_input()`: returns triangle-vertex layout (compute) or edge-line
  vertex layout (geometry).
- `get_input_assembly()`: returns `triangle` (compute) or `line` (geometry).
  Callers drop their hardcoded `Input_assembly_state::triangle` and use this
  instead.

Extend `set_shader_stages()` to three arguments:

```cpp
void set_shader_stages(
    erhe::graphics::Shader_stages* compute_shader_stages,              // compute CS
    erhe::graphics::Shader_stages* graphics_shader_stages_compute,     // VS+FS (triangles)
    erhe::graphics::Shader_stages* graphics_shader_stages_geometry     // VS+GS+FS (lines)
);
```

Passing `nullptr` for the geometry argument disables the GS path; callers on
compute builds pass `nullptr`.

### Per-frame params unified across paths

Add:

```cpp
void prepare_frame(
    const erhe::math::Viewport&               viewport,
    const erhe::scene::Camera&                camera,
    bool                                      reverse_depth,
    erhe::math::Depth_range                   depth_range,
    const erhe::math::Coordinate_conventions& conventions = {}
);
```

Caches `clip_from_world`, viewport/fov vec4s, `view_position_in_world`,
`vp_y_sign`, `clip_depth_direction` in a `Frame_params` struct on the
renderer. Callable without a command encoder.

`compute(encoder, camera, viewport, ...)` keeps its existing signature for
source compatibility and internally calls `prepare_frame()`. On
`geometry` / `disabled` it early-returns. Callers on the GS path call
`prepare_frame()` instead of `compute()`.

### `render()` branching

- `disabled` or empty `m_dispatches` => return.
- `compute` path: unchanged. Iterate dispatches with matching `group`, bind
  precomputed triangle buffer range, `draw_primitives(triangle, 0, 6 * edge_count)`.
- `geometry` path: for each dispatch with matching `group`:
  1. Acquire `m_view_buffer` range, write the 11-field view UBO using cached
     `Frame_params` + `dispatch.world_from_node`, bind at point 3.
  2. Bind the source edge-line vertex buffer at stream 0 with
     `dispatch.edge_buffer_byte_offset`.
  3. `draw_primitives(line, 0, dispatch.edge_count * 2)`.
  4. Release the view range.

`end_frame()`: skip the triangle-buffer-range release loop when
`m_path == geometry`.

### Bind group layout

Shared blocks (`edge_line_vertex_buffer_block`, `view_block`) are built
unconditionally. `triangle_vertex_buffer_block` is needed only on the
compute path. Build `m_bind_group_layout` after path selection:

- `compute`: edge SSBO (read) + triangle SSBO (write) + view UBO.
- `geometry`: edge SSBO (read, bound as vertex buffer) + view UBO. No
  triangle SSBO.

The edge SSBO binding stays as an SSBO for the compute path and is bound as
a vertex buffer for the geometry path -- the buffer itself is shared; only
the binding kind differs per path.

## Shader files

New files in **both** `src/editor/res/shaders/` and
`src/rendering_test/res/shaders/` (the two trees already duplicate the
compute/vertex/fragment shaders -- staying consistent):

- `content_line_before_geometry.vert`
  - Inputs: `in_position` (vec4 = object-local pos), `in_normal` (vec4 =
    object-local normal) matching `edge_line_vertex_struct`.
  - Reads `view.world_from_node` + `view.clip_from_world` from the view UBO.
  - Outputs to GS: clip-space position, world-space position, world-space
    normal, NdotV (or let GS compute it).
  - Just a pass-through that transforms per-vertex data into world/clip
    space for the GS.

- `content_line.geom`
  - `layout(lines) in; layout(triangle_strip, max_vertices = 4) out;`
  - Uses the exact algorithm from
    `compute_before_content_line.comp:71-215`:
    1. Clip each endpoint's line segment against near/far (`clip_line()`).
    2. Normal-based z-bias (`0.0005 * NdotV^2 * clip_depth_direction`).
    3. `get_line_width()` from `view.line_color.w`.
    4. NDC/screen perpendicular expansion to four corners a/b/c/d.
    5. `plane_z()` to keep the quad in the edge's normal plane.
    6. y-flip for top-left framebuffer origin via `view.vp_y_sign`.
  - Emits four vertices of the triangle strip with outputs matching
    `content_line_after_compute.frag`:
    - location 0: `v_line_width` (float)
    - location 1: `v_color` (vec4)
    - location 2: `v_start_end` (vec4) in viewport-relative pixel coords
  - Reuses the existing `content_line_after_compute.frag` unchanged.

No new fragment shader. The existing one already reads exactly those three
varyings.

## C++ changes

Critical files:

- `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.hpp`
- `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.cpp`

In `content_wide_line_renderer.hpp`:

- Add `Path` enum, `Frame_params` struct, `m_path`, `m_frame_params`.
- Add `m_edge_line_vertex_format`, `m_edge_line_vertex_input` (built
  alongside the existing triangle versions).
- Add pointers for the three shader-stages slots.
- Add `get_path()`, `get_input_assembly()`, `get_edge_line_vertex_format()`,
  `prepare_frame()`.

In `content_wide_line_renderer.cpp`:

- Build both vertex formats + both vertex-input states in constructor.
- Select `m_path` in `set_shader_stages()` using the precedence above.
- Build `m_bind_group_layout` after path selection (different entries per
  path).
- Implement `prepare_frame()` (cache params).
- Make `compute()` forward to `prepare_frame()` and early-return on
  non-compute paths.
- Split `render()` into compute/geometry branches. The geometry branch
  performs the per-dispatch UBO write + edge-buffer bind +
  `draw_primitives(line, ...)` described above.
- Guard triangle-buffer-range release in `end_frame()` on path.

## Caller updates

### `src/editor/editor.cpp` (instantiation around lines 872-943)

- After compiling the existing compute + compute-path graphics shader
  stages, if `!use_compute_shader`: compile a second `Shader_stages` from
  VS+GS+FS using `renderer.get_edge_line_vertex_format()`, the shared
  `view_block`, and a GS-appropriate `bind_group_layout`. Use
  `debug_renderer.cpp:204-233` as a template for the try/log/fall-back.
- Call the new 3-arg
  `set_shader_stages(compute_stages, graphics_stages_compute, graphics_stages_geometry)`.
  On compute builds pass `nullptr` for the third.
- Replace any hardcoded `Input_assembly_state::triangle` in pipeline
  construction tied to this renderer with `renderer.get_input_assembly()`.

### `src/rendering_test/cell_scene.cpp` (`make_content_wide_line_renderer`, lines 74-171)

Same three changes as editor:

1. Conditionally compile VS+GS+FS stages when `!use_compute_shader`.
2. Pass both to `set_shader_stages()`.
3. Pipeline builder uses `get_input_assembly()`.

### `src/rendering_test/rendering_test.cpp` (lines 642-651 + wide-line cells)

- Replace per-frame
  `m_content_wide_line_renderer->compute(encoder, camera, viewport, ...)` at
  the compute-dispatch site with:
  - Always call
    `m_content_wide_line_renderer->prepare_frame(viewport, camera, reverse_depth, depth_range, conventions)`.
  - Only open a `Compute_command_encoder` and call `compute(encoder, ...)`
    when `get_path() == Path::compute`.
  - Emit the inter-stage memory barrier only when on the compute path.
- Pipeline creation already goes through `get_vertex_input()` /
  `get_graphics_shader_stages()`; add `get_input_assembly()`.

### `src/rendering_test/cell_stencil_wide_line.cpp`

- Remove the early-return at lines 72-74. Gate only the wide-line section of
  the function on `is_enabled()`; the stencil cube draw at lines 89-115 must
  always run so the right-hand cell renders the cube when the wide-line path
  is unavailable (mirrors the `cube_with_edge_lines` cell behavior). Keep
  the `if (!m_compute_edge_lines_stencil_pipeline_state)` check gating the
  wide-line draw itself.

## Verification

Build the editor and `rendering_test` on Windows
(`scripts\configure_vs2026_opengl.bat`) and run:

1. `src/rendering_test/config/erhe_graphics.json` with
   `"force_no_compute_shader": false`
   - Run `rendering_test.exe` (default config
     `rendering_test_wel_then_stencil_wl.json`).
   - Expect left cell (`cube_with_edge_lines`): phong cube with wide edge
     lines.
   - Expect right cell (`stencil_wide_line`): stencil cube + wide lines on
     the sphere's edges outside the cube's stencil stamp.
   - Confirms compute path unchanged (regression check).

2. Same build, flip to `"force_no_compute_shader": true` and relaunch.
   - Expect identical visuals to case (1): lines rendered via geometry
     shader.
   - Confirms the fallback.

3. Editor smoke test in both config states -- select a mesh with visible
   edge lines; confirm they render.

4. Vulkan build (`scripts\configure_vs2026_vulkan.bat`): should be
   unaffected -- `use_compute_shader` is always true on Vulkan so the
   geometry path is never selected and the GS `Shader_stages` is never
   compiled (Vulkan's `vulkan_shader_stages_prototype.cpp` explicitly
   rejects `Shader_type::geometry_shader`, so never compiling it matters).

5. macOS Metal (`scripts/configure_xcode_metal.sh`): same --
   `use_compute_shader` true, no GS attempted.

Optional: one RenderDoc capture per path to confirm draw calls (triangles
from SSBO vs. GL_LINES from vertex buffer) match expectations.

## Open items (flag at implementation time)

- Whether to consolidate the duplicated shader files under
  `src/erhe/scene_renderer/res/shaders/` as a separate cleanup. Not part of
  this change.
- Whether to surface `get_path()` for rendering-test telemetry / logging
  (useful for debugging which path is live).
