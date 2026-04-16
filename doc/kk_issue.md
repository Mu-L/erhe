# KosmicKrisp Vulkan-to-Metal translation bug: pipeline switch loses depth/stencil state

## TL;DR

On macOS running the Vulkan backend through **KosmicKrisp** (the
Vulkan-to-Metal translator), switching between two `VkPipeline` objects
in the same render pass via `vkCmdBindPipeline` does not always
re-apply the new pipeline's depth/stencil state. The previously-bound
pipeline's state persists through the second pipeline's draw calls.

The failure is consistently reproducible via the `rendering_test`
executable shipped with this repository. The same pipelines are
rendered correctly on:

- OpenGL (reference)
- Native Metal via the erhe Metal backend

The regression is visible only on the KosmicKrisp Vulkan backend.

## Minimal reproduction

```sh
cd src/rendering_test
rendering_test config/rendering_test_wel_then_stencil_wl.json
```

The layout is a 2 x 1 grid (`cols = 2, rows = 1`):

| Tile | Subtest                 | Expected on every other backend                                                                                                                              |
| ---- | ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0    | `cube_with_edge_lines`  | Phong-shaded cube + wide edge lines drawn via the compute-expanded wide-line path. No stencil test.                                                          |
| 1    | `stencil_wide_line`     | Gray cube whose silhouette writes `stencil = 1`, then the sphere's compute-expanded wide-line edges rendered with `stencil_test` set to `not_equal 1`, `keep/keep/keep`. Yellow edges should appear only **outside** the gray cube silhouette. |

On KosmicKrisp the tile-1 wide line is drawn as if the preceding
pipeline's depth/stencil state were still active:

- The wide-line pipeline (`m_compute_edge_lines_stencil_pipeline_state`)
  has `depth_test_enable = false`, `depth_write_enable = false`,
  `stencil_test_enable = true`, `function = not_equal`, `reference = 1`,
  `write_mask = 0`.
- Observed behaviour: wide lines are visible **everywhere outside
  where the gray cube is nearer in depth**. No stencil masking is
  applied (the lines cross the cube silhouette freely except where
  the cube's depth occludes them). Consistent with `depth_test_enable
  = true` + `stencil_test_enable = true` with `function = always,
  replace 1` - i.e. the stencil-writing pipeline's state that was
  bound just before.

The same tile-1 subtest renders correctly when the preceding tile uses
a pipeline with `stencil_test_enable = false`
(`rendering_test_wideline_pipe_switch.json`). The difference between
the two configs is only whether the pipeline bound immediately before
the wide-line stencil draw has `stencil_test_enable = true` or `false`.

## Pipeline sequence on the failing config

All four pipelines are bound in a single swapchain `VkRenderPass`:

| # | Source                                    | Pipeline                     | `depth_test` | `depth_write` | `stencil_test` | Stencil op                                   |
|---|-------------------------------------------|------------------------------|:------------:|:-------------:|:--------------:|-----------------------------------------------|
| 1 | `forward_renderer` (phong cube tile 0)    | standard                     | true         | true          | false          | -                                             |
| 2 | `Content_wide_line_renderer::render()`    | `m_compute_edge_lines_pipeline_state`        | true | true | false         | -                                             |
| 3 | `forward_renderer` (stencil cube tile 1)  | `m_stencil_write_1_pipeline` | true         | true          | true           | `always, replace 1, write_mask = 0xFF`        |
| 4 | `Content_wide_line_renderer::render()`    | `m_compute_edge_lines_stencil_pipeline_state` | **false** | **false** | true    | `not_equal 1, keep, write_mask = 0`           |

The regression manifests on pipeline `#4`. The pipeline object itself
is valid and correct (fullscreen / replicate modes that run only the
tile-1 subtest render correctly on KosmicKrisp). The trigger is
pipeline `#3 -> #4` specifically: both pipelines have
`stencil_test_enable = true`, but `#3`'s function/reference/ops and
depth-state leak into the `#4` draw.

## What was ruled out

A large matrix of rendering_test configs isolates each variable
individually. Full diagnostic history is in
`.claude-personal/plans/binary-finding-moth.md`; summary:

- **Two `forward_renderer` pipelines with the same or a larger
  `depth_stencil` delta** render correctly
  (`rendering_test_pipeline_switch_{AB,BA,AC,CA}.json`).
- **Same `VkPipeline` rebound twice** always renders correctly
  (`rendering_test_{cube,sphere}_sphere_cube{,_masked}.json`). Rebinds
  with `stencil_test_enable = true` work fine.
- **Two `Content_wide_line_renderer::render()` calls using the same
  pipeline** work correctly.
- **Two `Content_wide_line_renderer::render()` calls using two
  different pipelines whose depth/stencil state differs** are required
  for the bug. Even narrowing the delta down to only stencil
  function/reference/mask (keeping `stencil_test_enable = true`,
  `depth_test_enable = false` on both) still reproduces.
- **Disabling Vulkan validation, replacing the stencil-writing cube
  with a polygon-fill sphere, or removing the render-to-texture /
  MSAA-resolve pre-passes** does not hide the bug.

## Attempt at a reproducer without `Content_wide_line_renderer`

A standalone compute-writes-triangle + graphics-reads-vertex-buffer
path was built in `src/rendering_test/cell_minimal_compute_triangle.cpp`
mirroring every structural aspect of `Content_wide_line_renderer` we
could identify, while using only the raw `Render_command_encoder` API
(no wide-line renderer code). Every variant below renders correctly on
KosmicKrisp and therefore does **not** reproduce:

- Two different graphics pipelines with identical shader stages but
  different depth/stencil state
  (`rendering_test_minimal_compute_{AB,BA,CD}.json`).
- `forward_renderer(stencil_write_1)` between the two minimal draws
  (`rendering_test_minimal_stw1_then_D.json`,
  `rendering_test_minimal_C_stw1_D.json`,
  `rendering_test_minimal_C_and_stw1_then_D.json`).
- Fragment shader `discard` + `color_blend_premultiplied` on both
  minimal pipelines, matching the wide-line fragment shader.
- Vertex shader statically referencing `camera.cameras[0].viewport`
  so the graphics pipeline's baked-in layout uses `program_interface.bind_group_layout`
  and statically declares descriptor set 0 (mirroring
  `content_line_after_compute.frag`). Also added a prep
  `render_scene()` call with empty meshes so descriptors are bound
  before the first draw.
- Compute workgroup size switched to `local_size_x = 32` (thread 0
  writes, others return early); SSBO range padded to
  `3 * 32 * stride` bytes.
- Two separate `acquire + bind + dispatch` cycles per compute encoder
  scope, mirroring wide-line's per-input-mesh loop.
- Compute-side `Bind_group_layout` extended from one binding to three
  (2 storage + 1 uniform), matching wide-line's layout shape.
- Layout mismatch at bind time: `set_bind_group_layout(compute_layout)`
  before `set_render_pipeline` on a pipeline whose `VkPipelineLayout`
  was built from `program_interface.bind_group_layout`. This is
  exactly what `Content_wide_line_renderer::render()` does.

None of these trigger the bug. The minimal path therefore does **not**
prove the failure is pure-Vulkan; something more specific to
`Content_wide_line_renderer`'s setup (likely the wide-line shader's
particular vertex-attribute consumption or its interaction with the
compute-produced triangle data) is required.

## Code path on the failing case

- Pipeline #3 is bound through
  `forward_renderer.render(...)` at
  `src/rendering_test/cell_stencil_wide_line.cpp:88-133`. That call
  chain ends in
  `Render_command_encoder::set_render_pipeline_state(...)` in
  `src/erhe/graphics/erhe_graphics/vulkan/vulkan_render_command_encoder.cpp:79-108`,
  which recomputes and rebinds all pipeline state including
  `MTLDepthStencilState` when the translated backend processes the
  `vkCmdBindPipeline`.
- Pipeline #4 is bound through
  `Content_wide_line_renderer::render(...)` at
  `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.cpp:352-391`.
  That path calls the raw
  `Render_command_encoder::set_render_pipeline(...)` in
  `src/erhe/graphics/erhe_graphics/vulkan/vulkan_render_command_encoder.cpp:51-77`,
  which just emits `vkCmdBindPipeline`. On KosmicKrisp this single
  `vkCmdBindPipeline` evidently does not re-issue the new pipeline's
  `MTLDepthStencilState`.

## Rendering_test configs of interest

| Config                                              | Outcome on KosmicKrisp                                    |
|-----------------------------------------------------|-----------------------------------------------------------|
| `rendering_test_wel_then_stencil_wl.json`           | **bug repro** (tightest)                                  |
| `rendering_test_scene_wideline_scene.json`          | bug repro (3-cell)                                         |
| `rendering_test_wideline_scene_wideline.json`       | bug repro (3-cell, reverse order)                          |
| `rendering_test_wideline_pipe_switch.json`          | correct (same sequence but preceding pipeline has `stencil_test_enable = false`) |
| `rendering_test_pipeline_switch_AB.json` / `BA.json` / `AC.json` / `CA.json` | correct (forward_renderer-only pipeline switches) |
| `rendering_test_cube_sphere.json` / `sphere_cube.json`                       | correct (same VkPipeline rebind, different `group` filter)     |
| `rendering_test_cube_sphere_masked.json` / `sphere_cube_masked.json`         | correct (same wide-line stencil pipeline rebind)                |
| `rendering_test_minimal_compute_AB.json` / `BA.json` / `CD.json`             | correct (minimal compute-triangle path, no wide-line)           |
| `rendering_test_minimal_stw1_then_D.json`           | correct (minimal + stw1, single cell)                     |
| `rendering_test_minimal_C_stw1_D.json`              | correct (minimal + stw1 + minimal, single cell)           |
| `rendering_test_minimal_C_and_stw1_then_D.json`     | correct (2-cell split of the above)                       |

## Environment

- macOS (Apple Silicon)
- Vulkan SDK with KosmicKrisp loaded as the ICD
- erhe built from `build_xcode_vulkan` via
  `scripts/configure_xcode_vulkan.sh`
- Validation layers are clean across the failing configs

## Possible root cause (speculation)

KosmicKrisp's `vkCmdBindPipeline` handler appears to skip the Metal
`setDepthStencilState` re-emit when:

1. The previously-bound pipeline also had `stencil_test_enable = true`,
   and
2. The code path binding the new pipeline does not force a full
   pipeline-state refresh (as `set_render_pipeline_state` does).

`Content_wide_line_renderer::render()` meets both conditions. The
workaround in erhe would be to route wide-line draws through
`set_render_pipeline_state` instead of the raw
`set_render_pipeline`, but that does not address the underlying
KosmicKrisp behavior.
