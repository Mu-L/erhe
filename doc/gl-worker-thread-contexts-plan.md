# OpenGL worker-thread GL contexts -- plan

Status: PLAN ONLY, nothing implemented. Written 2026-08-27; revised the same
day to add phase 0.

## 1. The bug

The OpenGL build cannot load any glTF. `build_vs2026_opengl` (Debug),
`editor.exe --scene res/editor/assets/ABeautifulGame.glb` from `D:\erhe`,
faults inside the driver at `glCreateBuffers`, called from a taskflow worker
that has no GL context:

```
atio6axx.dll  (access violation reading 0x1D38)
gl::create_buffers                                    gl.cpp:1566
Device_impl::create_buffer                            gl_device.cpp:2101
Buffer_impl::Buffer_impl                              gl_buffer.cpp:308
Buffer_pool::create_new_block                         buffer_pool.cpp:238
Buffer_pool::allocate_internal / allocate             buffer_pool.cpp:140, 272
Mesh_memory::allocate_vertex_buffer_range             mesh_memory.cpp:543
Build_context_root::allocate_edge_line_vertex_buffer  primitive_builder.cpp:222
Build_context_root::allocate_buffers                  primitive_builder.cpp:64
Build_context::allocate_and_bind_writers              primitive_builder.cpp:1123
Primitive_builder::build                              primitive_builder.cpp:525
build_buffer_mesh                                     primitive_builder.cpp:1722
Primitive_render_shape::prepare_geometry_buffer_mesh  primitive.cpp:831
deferred_finalize_mesh_items                          async_raytrace_kickoff_operation.cpp:132
... tf::Executor worker
```

Vulkan is unaffected; the procedural default scene is fine. Reproduced on a
deleted + reconfigured + fully rebuilt tree, so it is not staleness. Also
reproduces with `controller_left.glb`.

## 2. The design (settled)

Worker threads get GL contexts, but **resource-only** ones:

1. A worker may create GL objects and upload buffer contents, **through DSA
   entry points only**. It may never change a binding or prepare anything for
   a draw call.
2. Obtaining a worker context goes through an **API explicitly for that
   purpose**, not the existing draw-capable `Scoped_gl_context`.
3. Anything a worker context may not do **asserts**, loudly, at the call site.

### Why resource-only is sufficient (verified, do not re-derive)

- The worker's GL work is already DSA-clean when `use_direct_state_access` is
  true: `glCreateBuffers` (`Device_impl::create_buffer`),
  `glNamedBufferStorage` (`gl_buffer.cpp:269`), `glMapNamedBufferRange`
  (`:402`, `:461`, `:536`), `glObjectLabel`. None of those touch a binding
  point.
- Every one of them has a non-DSA fallback right beside it that does
  `push_buffer(copy_write_buffer, ...)` through the shared `Gl_binding_state`
  (`gl_buffer.cpp:271`, `:404`, `:468`, `:543`, `:576`). **Phase 0 deletes
  those fallbacks outright**, so the "DSA-clean" property becomes structural
  rather than conditional -- see section 3.
- The vertex/index **data** upload is not on the worker at all: the worker
  allocates, and `Mesh_memory::flush` records the transfer on the main thread
  once per frame.
- Contexts created by `Context_window(share)` are GL **share contexts**
  (`sdl_window.cpp:719` `SDL_GL_SHARE_WITH_CURRENT_CONTEXT`,
  `glfw_window.cpp:459` share window), so buffers/textures/programs created on
  a worker are visible to the main context. Container objects -- VAOs,
  framebuffers -- are *not* shared, which is precisely why the rule forbids
  them rather than merely discouraging them.

### Why the naive fix is worse (do not repeat)

Wiring plain `Scoped_gl_context` into `deferred_finalize_mesh_items` fixes the
`glCreateBuffers` fault -- the scene loads and renders -- and then corrupts
main-thread rendering, failing `ERHE_VERIFY(vao != 0)` in
`Vertex_input_state_tracker::set_index_buffer` (`gl_state_tracker.cpp:522`).

`acquire_gl_context` / `release_gl_context` call
`OpenGL_state_tracker::on_thread_enter` / `on_thread_exit`, which is exactly
the draw-enabling machinery: `Vertex_input_state_impl::on_thread_exit` does
`gl::bind_vertex_array(0)`, and `OpenGL_state_tracker::on_thread_exit` does
`vertex_input.reset()` + `shader_stages.reset()`. Those write **shared**
caches -- `Device_impl::m_gl_binding_state` (one `Gl_binding_state` caching the
bound VAO / buffers / textures / sampler / program; its
`get_bound_vertex_array()` is what the failing verify reads) and
`Device_impl::m_gl_state_tracker` -- which describe **per-context** state.
That was harmless when worker contexts existed only during parallel init,
before the render loop. It is not harmless now that the finalize runs
concurrently with rendering.

A resource-only context never runs those hooks, so no per-thread VAO and no
thread-local state caches are needed.

## 3. Phase 0 -- remove macOS OpenGL support, and make DSA mandatory

**This phase comes first and the rest of the plan depends on it.**

macOS caps OpenGL at 4.1: no `ARB_direct_state_access`, no
`ARB_clip_control`, no compute shaders, no SSBOs, no `ARB_debug_output`, no
`ARB_internalformat_query2`. The whole non-DSA / pre-4.3 emulation layer in
the GL backend exists for that one platform. The resource-only worker-context
design cannot work there -- every non-DSA fallback binds through the shared
`Gl_binding_state`, which is exactly the cross-thread corruption the design
exists to prevent -- and there is no reason to keep emulating it now that
macOS has both Metal and Vulkan backends.

So: **drop macOS OpenGL, and require GL 4.5.**

The requirement is stated as **GL 4.5 minimum**, not as "DSA available".
That is deliberately slightly stronger: `use_direct_state_access` can also be
satisfied by `ARB_direct_state_access` on a 4.3/4.4 driver, and a
DSA-but-not-4.5 device would leave `use_clip_control` (4.5 core) still
conditional. Requiring 4.5 collapses several capability gates at once and
makes the implications below sound. Fail device creation with a clear message
naming the reported version.

There is an in-tree document for the layer being deleted --
`doc/opengl41_compatibility.md`, 314 lines -- referenced from
`src/erhe/graphics/notes.md:228`. Read it before starting; it is the best
inventory of what the layer covers, and it is itself one of the things phase 0
removes (section 3e).

### 3a. Remove the macOS OpenGL configuration

- `CMakeLists.txt:455` -- in the `opengl` branch, mirror the existing Metal
  guard at `:506` and hard-error on Apple:
  `if (APPLE) message(FATAL_ERROR "OpenGL backend is not supported on Apple platforms; use metal or vulkan") endif()`.
- Delete `scripts/configure_xcode_opengl.sh` and
  `scripts/configure_xcode_opengl_asan.sh`.
- `.github/workflows/build.yml:70-76` -- delete the "macOS (Xcode / OpenGL)"
  matrix entry. Verified sufficient: no `needs:` dependencies, no per-matrix
  artifact upload, and `matrix.api` is used only in the CPM cache key
  (`:124`, `:126`). macOS Metal (`:62-68`) and macOS Vulkan (`:78-84`) are
  unaffected.
- `scripts/configure_ninja_osx.sh` passes no `ERHE_GRAPHICS_API` and takes the
  `vulkan` default (`CMakeLists.txt:54`). Verified: no change needed.

### 3b. Remove the macOS OpenGL workarounds

**There are TWO `ERHE_OS_MACOS` GL-version blocks, not one.** Missing the
second is the half-removal failure mode:

- `src/erhe/window/erhe_window/window_configuration.hpp:36-44` -- the
  defaults themselves:
  ```cpp
  int gl_major {4};
  # if defined(ERHE_OS_MACOS)
  int gl_minor {1};
  # else
  int gl_minor {6};
  # endif
  ```
  This is in the shared window library and applies to **every** app that is
  not the editor (`config/hextiles/window.json`, `config/hello_swap/window.json`,
  `config/example/window.json`). Collapse to an unconditional `gl_minor {6}`.
- `src/editor/editor.cpp:1323-1326` -- the `#if defined(ERHE_OS_MACOS)` block
  forcing `gl_major = 4, gl_minor = 1` over the config value. Delete.

  No raise is needed anywhere: the configured default is already **4.6** --
  `window_config.py:60-78` (`gl_major` `"4"`, `gl_minor` `"6"`),
  `config/editor/window.json:7-8`, same in `config/example`,
  `config/hello_swap`, `config/hextiles`. The macOS branch above is the only
  value below 4.5 in the tree.
- `src/erhe/graphics/erhe_graphics/gl/gl_blit_command_encoder.cpp:224` and
  `:447` -- the two `#if defined(__APPLE__)` blocks that avoid PBO texture
  uploads by reading back to CPU. Both inject a
  `push_buffer(copy_read_buffer, ...)` binding mutation into an otherwise
  DSA-clean upload path, so deleting them helps the invariant directly. Note
  the `:447` arm also hard-codes `const bool use_dsa = false;` at `:464` --
  see the ordering note in 3c.
- `src/erhe/imgui/erhe_imgui/imgui_renderer.cpp:999` and `:1062` -- the
  `#if defined(ERHE_OS_MACOS) && defined(ERHE_GRAPHICS_API_OPENGL)` branches
  working around the macOS driver's broken `glCopyBufferSubData`. With the
  combination unbuildable, both are dead.
- `src/erhe/gl/templates/wrapper_functions.cpp:19` -- the macOS-debug default
  for `ERHE_GL_CHECK_ERRORS`. **This is a codegen template**: regenerate, and
  build twice, or the binary is stale.
- `gl_device.cpp:311` -- the "TODO: Add ARB_debug_output support for macOS GL
  4.1" comment.

### 3c. Make GL 4.5 mandatory and delete the emulation

**Decide the version-override question first.** `gl_device.cpp:215-228` reads
`force_gl_version` / `force_glsl_version` from config and overwrites
`m_info.gl_version` **before every capability probe** (`:384-457`). So
`force_gl_version: 410` (`config/editor/erhe_graphics.json:20`) is a single
switch that turns off DSA, clip control, compute, SSBO, debug output, texture
views and MDI at once -- a strictly bigger lever into the 4.1 path than
`force_no_direct_state_access`, which this plan does delete. Phase 0 must not
remove the emulation while leaving the switch that requests it.

Decide, and write the decision into the code:
- the hard 4.5 check tests the **raw reported version**, before any override;
  and
- `force_gl_version` is either deleted (same codegen retirement as below), or
  clamped to `>= 450` with a logged warning when a lower value is requested.
Deleting it is the simpler and more honest option now that nothing below 4.5
is supported.

**`force_glsl_version` needs the identical decision, and it is a separate
knob.** `gl_device.cpp:225-227` sets `m_info.glsl_version` independently of
`m_info.gl_version`, and it is *glsl_version*, not gl_version, that drives
`shader_stages_create_info.cpp:189` (which emits `#version <glsl_version>
core` verbatim), `shader_resource.cpp:656`, `:803`, `:815`, `:836`, `:870`
(the `layout(binding=)` / std430 emulation),
`gl_shader_stages_prototype.cpp:601` (`< 420`) and `:617` (`< 430`), and
`glyph_buffer.cpp:66`. Those are exactly the sites 3d proposes to delete. So
after phase 0, `force_glsl_version: 410` emits `#version 410 core` shaders
that still contain `layout(binding=)` and std430 blocks -- a compile/link
failure on every shader -- and a hard 4.5 check on the *GL* version does not
catch it. Clamping only `force_gl_version` leaves this door open. **There is
a hard floor: 450, and below 420 it breaks outright.** This is the same trap
3c names above, applied to the other half of the pair.

Note also that the argument above says `force_gl_version: 410`
(`config/editor/erhe_graphics.json:20`) as if that value were set. The key is
on that line, but the value is `0` in all four config files. The argument
stands -- the switch exists and is reachable -- but it is not currently
engaged.

Then:

- `gl_device.cpp:400` -- replace the `use_direct_state_access` probe with the
  hard version check described above.
- Delete the `else` arm of every DSA branch. **The site list below is an
  entry-point list, not a branch list -- do not work it mechanically.** An
  earlier revision of this plan called it a "46-site inventory [that] matches
  the tree exactly". Every line number in it is correct, but the list is a
  grep for the *identifier* `use_direct_state_access`, and in five of the
  eight files that identifier appears only in a local
  `const bool use_dsa = ...` declaration. The branches are on `use_dsa`, and
  there are far more of them. Re-verified in the tree:

  | file | cited sites | what they are | real `use_dsa` branches |
  |---|---|---|---|
  | `gl_render_pass.cpp` | :24, :166, :308, :528, :663, :849 | 6 declarations | **30** |
  | `gl_texture.cpp` | :710, :1047 | 2 declarations | **8** |
  | `gl_blit_command_encoder.cpp` | :38, :468, :637, :729, :841 | declarations | ~9 |
  | `gl_vertex_input_state.cpp` | :393 | declaration | ~3 |

  `gl_texture.cpp:710` is literally
  `const bool use_dsa = device.get_info().use_direct_state_access;` -- it has
  no `else` arm to delete. Working the checklist mechanically deletes a
  declaration and leaves its branches dangling on an undeclared name.

  Also: the stated count was wrong. Summing the list gives 43, not 46, and
  the tree has 48 occurrences of the identifier. **Re-derive the branch list
  from `use_dsa` at implementation time**; treat the entry points below only
  as the set of files to visit.

  Entry points: `gl_buffer.cpp` :267, :401, :460, :535, :565, :601, :653,
  :671, :681, :711, :735, :745, :763, :786;
  `gl_blit_command_encoder.cpp` :38, :315, :468, :637, :710, :729, :841;
  `gl_device.cpp` :1437, :1493, :2073, :2100, :2114, :2128, :2144, :2156,
  :2172;
  `gl_render_pass.cpp` :24, :166, :308, :528, :663, :849;
  `gl_texture.cpp` :710, :1047;
  `gl_texture_heap.cpp` :261, :342, :387;
  `gl_render_command_encoder.cpp` :33;
  `gl_vertex_input_state.cpp` :393.
  (The declaration at `device.hpp:189` and the log at `gl_device.cpp:402` are
  deliberately not in that list.)
- **`Vertex_input_state_tracker::m_use_dsa` has two read sites the plan never
  named**: `gl_state_tracker.cpp:526` and `:546`. The setter
  (`gl_state_tracker.hpp:103`), the member (`:113`) and the call
  (`gl_device.cpp:580`) were listed below; these are the branches they exist
  to feed, and they go with them.
- **Ordering: do 3b before 3c.** `gl_blit_command_encoder.cpp:464` is a
  non-DSA site expressed as a hard-coded `const bool use_dsa = false;` inside
  the `__APPLE__` arm, not as a `use_direct_state_access` branch. Doing 3c
  first would delete the `else` arms that site feeds and leave the hard-coded
  `false` behind.
- **Delete the ~307-line GL < 4.3 format-probe fallback**, `gl_device.cpp:625`
  (`if (m_info.gl_version >= 430) {`) through the `} else {` at `:736` to the
  close at `:1043`. This is the largest single remaining emulation body -- it
  probes formats by creating textures/renderbuffers and reading `glGetError`,
  because GL 4.1 lacks `glGetInternalformativ` -- and it includes
  `probe_sample_counts`. It is not a `use_direct_state_access` branch, so it
  is easy to miss. Keep the `>= 430` arm as the unconditional path.
  This also answers the `gl_device.cpp:742` question an earlier revision of
  this plan left open: `:742`'s comment sits on the
  `gl_helpers::set_error_checking(false)` call at `:745`, which is part of
  *this whole pre-4.3 arm*, not a small standalone workaround. It goes with
  the block.
- `Vertex_input_state_tracker::set_use_dsa` (`gl_state_tracker.hpp:103`) and
  `m_use_dsa` (`:113`) and the call at `gl_device.cpp:580` become dead. Delete.
- Retire `force_no_direct_state_access` **through the codegen's versioned
  retirement path, not by deletion**. `Opengl_config` is `version=1,
  reflect=True` (`opengl_config.py:3-5`), and the generator supports field
  retirement via `removed_in` plus a struct version bump
  (`erhe_codegen/schema.py:282-293`, `emit_cpp.py:216-217`,
  `emit_hpp.py:73-74`, `:260`), with in-tree precedent at
  `src/editor/config/definitions/content_edge_lines_config.py:25`. So:
  `removed_in=2` on the field, `version=2` on the struct. Deleting the field
  outright silently drops the versioned-deserialization path for user-written
  `erhe_graphics.json` files that still carry the key. Then drop the key from
  **all four** config files that carry it -- an earlier revision listed only
  the first two:
  `config/editor/erhe_graphics.json:16`, `config/example/erhe_graphics.json:11`,
  `config/hello_swap/erhe_graphics.json:11`,
  `config/hextiles/erhe_graphics.json:11`.
  The same applies to `force_gl_version` / `force_glsl_version` if they are
  deleted: all four files carry them (`editor:20-21`, `example:15-16`,
  `hello_swap:15-16`, `hextiles:15-16`). These are precisely the apps 3f says
  to re-verify.
  Then the read at `gl_device.cpp:211` (**not** `:210` -- that line is
  `force_no_persistent_buffers`) plus the block at `:560-571` (the `if` is
  `:567-570`; `:572` is blank).
- Delete `ERHE_TEST_OPENGL_NO_DSA` from
  `src/erhe/graphics/test/gpu_test_environment.cpp:71-79` -- the block starts
  at `:71` with its explanatory comment; deleting only `:74-79` orphans three
  comment lines. **Note while there:** that comment claims "the editor's
  config disables direct state access". It does not --
  `config/editor/erhe_graphics.json:16` has `force_no_direct_state_access:
  false`. The comment is stale and the non-DSA path is not exercised by
  default in CI, which is part of why it can go.
- **Keep** `use_persistent_buffers` and `force_no_persistent_buffers`.
  Persistent mapping is `glBufferStorage` (GL 4.4), a separate capability, not
  an Apple artifact. Only its coupling to DSA (`gl_device.cpp:567-570`) goes.
  **But decide the probe itself:** `gl_device.cpp:431` sets
  `use_persistent_buffers` from
  `gl::is_extension_supported(ARB_buffer_storage)` -- an extension-only test
  with no `gl_version >= 440` core alternative. It is part of the pre-4.4
  layer and will still report false on a conformant 4.5 driver that does not
  advertise the (now core) extension string.
- **Keep** `force_no_compute_shader`. Verified: `gl_device.cpp:410-413` clears
  `use_shader_storage_buffers` and `:457-460` clears `use_compute_shader`, and
  `Id_renderer::ensure_scan_compute` returns early on either
  (`id_renderer.cpp:440`), leaving the CPU region-scan path at
  `id_renderer.cpp:1049` reachable. It is a deliberate testing knob.
- **Decide** `force_no_clip_control` (`opengl_config.py:41`,
  `gl_device.cpp:573-578`). It is the exact analogue of
  `force_no_compute_shader`: keep it as a testing knob, or retire it with the
  same `removed_in` treatment. Do not leave it undecided.
- **Emulation in neither 3c nor 3d, found by the phase-0 re-review.** Each is
  a pre-4.5 capability test that a 4.5 requirement makes constant, and none
  is a `use_dsa` branch, so 3c as written does not reach them:
  - `gl_buffer.cpp:239` and `:248` -- `const bool in_core = gl_version >= 440;
    ... ERHE_VERIFY(in_core || has_extension)`. Constant-true.
  - `gl_device.cpp:244` -- `if (gl_version >= 430)` guarding
    `max_framebuffer_samples`. Constant-true.
  - `gl_shader_stages_prototype.cpp:654` -- `gl_version >= 430` gating
    `dump_reflection()`. 3d lists `:601` and `:617` from this file, not this.
  - `gl_texture.cpp:965` -- `gl_version >= 430` choosing
    `tex_storage_2d_multisample` over `tex_image_2d_multisample`. It sits
    inside the non-DSA arm at `:914`, so a correctly-executed 3c takes it --
    but only once 3c is understood as "delete the `else` arms of the
    `use_dsa` branches", per the inventory correction above.
  - `shader_stages_create_info.cpp:287-30x` -- the
    `GL_ARB_shading_language_packing` polyfill (`unpackSnorm2x16` /
    `unpackUnorm2x16` written out in GLSL), whose comment at `:290` names
    *"macOS OpenGL 4.1 where these are missing despite being GLSL 4.00
    core"*. **Caveat: it is gated on extension availability, not on version**,
    so verify before deleting rather than assuming 4.5 implies the extension
    string.

### 3d. Capability gates that become unreachable -- decide separately

Requiring GL 4.5 makes a long list of gates constant-true. They are listed
here because they were motivated by macOS GL 4.1, but each is a real behavior
deletion and **none is required by the worker-context work**. Do them in their
own commit(s), after phase 0 lands and builds. This list is longer than it
first appears -- treat it as a survey to re-derive, not as complete:

**Citation drift, corrected.** Several "consumer" line numbers in an earlier
revision of this list pointed at explanatory *comments* rather than at the
branch. The corrections are folded in below; the pattern is worth knowing
because it repeats -- when re-deriving this survey, confirm each cited line is
a read of the flag, not a sentence about it.

- `use_solid_wireframe = (gl_version > 410)` (`gl_device.cpp:397`;
  declaration is `device.hpp:300`, **not** `:295`, which is a comment). Real
  reads: `app_rendering.cpp:205` and `brush_preview.cpp:43`. The previously
  cited `app_rendering.cpp:199`, `brush_preview.cpp:176` and
  `viewport_scene_view.cpp:426` are comments -- and `viewport_scene_view.cpp`
  contains no read at all.
- `use_texture_view` (`gl_device.cpp:387`, GL 4.3) -- has real consumer
  fallbacks at `src/editor/graphics/thumbnails.cpp:71` and `:122`
  (`:66` is a comment).
- `use_clear_texture` (`:384`, 4.4), `use_base_instance` (`:454`, 4.2),
  `use_debug_output` / `use_debug_groups` (`:312-313`, 4.3),
  `use_multi_draw_indirect_core` (`:432`).
- `use_clip_control` (4.5 core) -- the cited `gl_device.cpp:1071` is a
  comment line inside the "Hardware-capability note" block. The two
  functional sites are `:1057` (`gl::clip_control`) and `:1065` (the
  `native_depth_range` ternary); the whole reverse-Z warning block
  `:1069-1082` becomes dead with them.
- `gl_device.cpp:1050-1055` -- `primitive_restart_fixed_index` vs.
  `primitive_restart` + `primitive_restart_index`.
- **GLSL-version emulation**, which the worker-context work never touches but
  which is part of the same layer: `shader_resource.cpp:656`, `:803`, `:815`,
  `:836`, `:870` (`layout(binding=)` and std430 emulation for
  `glsl_version < 420 / 430`), `gl_shader_stages_prototype.cpp:601`, `:617`,
  `glyph_buffer.cpp:66`, and `shader_stages_create_info.cpp:272`
  (`use_shader_storage_buffers && gl_version < 430` -- dead on both halves
  once 4.5 is required).
- **Compute fallbacks outside the id-renderer**: `sky_renderer.cpp:92-96` +
  `sky_renderer.hpp:43`, and `imgui_renderer.cpp:166`, `:186` (SSBO to UBO).
  For the debug renderer, `debug_renderer.cpp:29` is correct but `:380` is a
  comment; the reads are `debug_renderer.cpp:159`, `:384`, `:420`, `:541`,
  plus `debug_renderer_bucket.cpp:47` and `:152`.
- `use_compute_shader` / `use_shader_storage_buffers` (4.3 core) and the
  id-renderer CPU scan fallback (`id_renderer.cpp:437`, `:1049`,
  `id_renderer.hpp:307`). **This one stays reachable via
  `force_no_compute_shader`** -- the capability probe may become
  unconditional, but the fallback code must survive. Do not delete it.
- `gl_texture_heap.cpp:356`'s macOS-4.1 comment (the code may still be correct
  for other reasons -- read it, do not pattern-match the comment).

Bindless textures stay conditional: `ARB_bindless_texture` is an extension,
not 4.5 core.

### 3e. Documentation and tooling remnants

Phase 0 is not done while the project's own instructions point at deleted
files:

- `AGENTS.md:86-102` -- the "macOS (Xcode)" section tells agents to run
  `scripts/configure_xcode_opengl.sh` / `_asan.sh` and to build
  `build_xcode_opengl`. Update to Metal / Vulkan.
- `AGENTS.md:125` and `doc/building.md:131` list `opengl` as an unrestricted
  `ERHE_GRAPHICS_API` value. Note the Apple restriction.
- `doc/opengl41_compatibility.md` -- delete (314 lines documenting the deleted
  layer), and remove the pointer at `src/erhe/graphics/notes.md:228`.
- `doc/graphics_test_nonheadless_port.md:9`, `:64`, `:65`, `:88`, `:97` --
  documents `ERHE_TEST_OPENGL_NO_DSA` and records a "40 passed" result under
  it. `:64` repeats the same stale "the editor's `force_no_direct_state_access`"
  claim, and `:97` documents the `__APPLE__` cube-map upload branch that 3b
  deletes. Update or strike; the variable is gone.
- `scripts/run_circular_ring_buffer_smoke_test.py:38` and `.gitignore:59-60`
  reference `build_xcode_opengl`.
- **A codegen definition, which therefore ships into the generated config
  UI**: `src/editor/config/definitions/preview_edge_lines_config.py:8`, whose
  `long_desc` justifies the field by "macOS OpenGL 4.1". This one is
  user-visible, not just a comment, and it needs the double-build treatment
  that every codegen edit needs.
- **Docs still describing the GL 4.1 path as live** -- outside the list
  above, all found by the phase-0 re-review:
  `doc/shader_workarounds.md:33`, `doc/editor_rendering.md:131` and `:333`,
  `doc/mesh_component_selection.md:237`, `doc/esoterica_rendering.md:241`,
  `doc/debug_renderer_multiview.md:276`, `doc/async-asset-loading-plan.md:426`,
  `src/erhe/renderer/notes.md:37`.
- **In-source comments justifying behavior by "macOS GL 4.1"**, in neither 3b
  nor 3d: `src/erhe/scene_renderer/erhe_scene_renderer/program_interface.cpp:269`,
  `src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.hpp:196`,
  `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.hpp:87`,
  `src/erhe/graphics/erhe_graphics/shader_stages_create_info.cpp:290`,
  `src/erhe/math/erhe_math/math_util.hpp:243`.

### 3f. Verification for phase 0

- `build_vs2026_opengl` Debug and the Linux ninja OpenGL configure both build
  and run; the editor renders the procedural scene. (The glTF load still
  crashes -- that is the bug the rest of the plan fixes.)
- `ninja` Vulkan Debug + Release, VS null backend, VS vulkan-headless, Quest
  APK all still build: phase 0 touches `imgui_renderer.cpp`, `editor.cpp` and
  `window_configuration.hpp`, all shared.
- The non-editor apps still build and run: `hextiles`, `hello_swap`, the
  example -- they are the ones affected by the
  `window_configuration.hpp` default change.
- macOS Metal and macOS Vulkan CI jobs still pass; the macOS OpenGL job is
  gone.
- A config carrying a retired `force_no_direct_state_access` key still loads
  without error (that is what the `removed_in=2` treatment buys).
- Codegen ran and the tree was built **twice** after the `opengl_config.py`
  and `wrapper_functions.cpp` template edits.
## 4. Phase 1 -- the thread-role guard

New GL-backend-internal header, `erhe_graphics/gl/gl_thread_role.hpp`:

```cpp
enum class Gl_thread_role { none, main, worker_resource_limited, worker_resource_full };

[[nodiscard]] auto get_gl_thread_role() -> Gl_thread_role;   // thread_local
void set_gl_thread_role(Gl_thread_role role);
```

An **explicit per-thread role**, not a "is this the main thread" test. The
role test is harder to defeat by accident: a future non-executor thread that
makes a context current without going through the resource API gets `none` and
trips the first guard rather than silently passing an `is_main_thread()`
negation.

Three macros, all `ERHE_VERIFY`-backed (always on, Debug and Release):

- `ERHE_VERIFY_GL_THREAD_HAS_CONTEXT()` -- role must not be `none`. This one
  alone converts the section 1 access violation into a legible assert at the
  call site.
- `ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()` -- role must be `main`.
- `ERHE_VERIFY_GL_THREAD_CAN_CREATE_VERTEX_INPUT()` -- role must be `main`
  or `worker_resource_full`. This is the guard that makes the two tiers of
  section 6 mean something: it is the *only* permission the full tier adds.

The role is set in exactly three places: `Device_impl`'s constructor and
`Device_impl::on_thread_enter()` set `main`; the worker context's acquire
sets `worker_resource_limited` or `worker_resource_full` per the requested
tier, and its release restores the *previous* role -- not unconditionally
`none`, see the re-entrancy discussion in section 6.

### Guard placement

`ERHE_VERIFY_GL_THREAD_HAS_CONTEXT()`:
- every `Device_impl::create_*` (`create_buffer`, `create_texture`,
  `create_texture_view`, `create_framebuffer`, `create_renderbuffer`,
  `create_sampler`, `create_query`, `create_program`, `create_shader`),
  `gl_device.cpp:2070-2190`;
- `Buffer_impl::allocate_storage`, `map_bytes`, `map_all_bytes`, `unmap`,
  `flush_bytes`.

`ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()`:
- every `Gl_binding_state` mutator and query: `push_/pop_/bind_ buffer`,
  `texture`, `framebuffer`, `renderbuffer`, `vertex_array`, plus
  `bind_sampler`, `use_program`, and **all seven `on_*_deleted` hooks**;
- `OpenGL_state_tracker::execute_` (both overloads), `reset`,
  `on_thread_enter`, `on_thread_exit`;
- `Vertex_input_state_tracker::execute`, `set_index_buffer`,
  `set_vertex_buffer`;
- `Vertex_input_state_impl::on_thread_enter` / `on_thread_exit` for as long
  as they exist. Section 6 retires the bulk sweep in `on_thread_enter` in
  favour of lazy per-object adoption, and the worker path never calls
  `on_thread_exit`; whatever survives that stays draw-capable-only;
- the top of `Render_command_encoder_impl`, `Compute_command_encoder_impl` and
  `Blit_command_encoder_impl` construction, and `Render_pass_impl` start/end.

`ERHE_VERIFY_GL_THREAD_CAN_CREATE_VERTEX_INPUT()`:
- `Device_impl::create_vertex_array` (`gl_device.cpp:2153`; moved out of the
  `HAS_CONTEXT` list above -- a VAO is a container object, so `HAS_CONTEXT`
  is the wrong question for it);
- `Vertex_input_state_impl::create` and `reset` (both were listed under
  `DRAW_CAPABLE` in the single-tier revision of this plan; the full tier is
  exactly the permission to reach them off the main thread). `create` is
  called from both constructors (`gl_vertex_input_state.cpp:321`, `:332`),
  so this is where a limited-tier worker constructing a
  `Vertex_input_state` is caught.

After phase 0 there are no non-DSA fallbacks left to guard -- the emulation
that would have taken the binding path is gone rather than merely asserted
against. What remains is the by-construction consequence:

- `Gl_binding_state::on_*_deleted` under the draw-capable guard means any
  worker-side destruction of a GL object asserts. `Buffer_pool` blocks are
  never destroyed (capacity only grows), so this is expected to be
  unreachable -- but it must be **verified by running**, not assumed
  (section 9, step 2). If a real worker-side destruction turns up, the fix is
  a main-thread deferred-delete queue drained in `Device_impl::wait_frame()`,
  not a relaxed guard.

Phase 1 is independently valuable and independently committable: on its own it
turns the driver crash into a named assert, with no behavior change on any
configuration that passes today.


### One correction to the guard list

`Buffer_impl::map_bytes` / `map_all_bytes` / `unmap` / `flush_bytes` take
`ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()`, **not** `HAS_CONTEXT` -- a worker must
never map a buffer. See section 6's persistent-mapping invariant for why.
`allocate_storage` and the `create_*` functions keep `HAS_CONTEXT`.

**That split is self-contradictory as written, because `allocate_storage`
calls `map_bytes`.** `gl_buffer.cpp:296-300`:

```cpp
const bool map_persistent = erhe::utility::test_bit_set(gl_storage_mask, gl::Buffer_storage_mask::map_persistent_bit);
if (map_persistent) {
    map_bytes(0, m_capacity_byte_count);
}
```

A worker-legal function calls a main-only one. Today the repro path survives
purely by accident: `Mesh_memory`'s pools request `device_local` with
`preferred = none` (`mesh_memory.cpp:536-537`, `:618-619`), so
`map_persistent_bit` is never set. **Any** worker-allocated host-visible
buffer -- including one a future call site adds -- asserts inside
`allocate_storage`.

Resolve it explicitly, do not leave it to luck. The cleaner of the two
options: `allocate_storage` asserts that a worker only ever allocates
**non-persistent** buffers, which makes the accidental invariant a stated
one. The alternative is a documented "except from within `allocate_storage`"
carve-out on the `map_bytes` guard, which is weaker because it is a hole in
the guard rather than a narrowing of the permission.

## 5. Phase 2 -- cross-context publication

**This is a first-class part of the design, not a detail of the API.**

GL share contexts do **not** automatically synchronize. The GL spec's
"Shared Objects and Multiple Contexts" rules require the creating context to
flush before another context in the share group may rely on an object. The
plan's earlier revision was silent on this and that was a real gap: the
window is wide and is **not** covered by the existing main-thread transfer
path.

The dangerous ordering is *use-before-release*, not *release-then-use*:

1. Worker, holding its context: `Buffer_pool::create_new_block` ->
   `Buffer_impl` -> `glCreateBuffers` + `glNamedBufferStorage`
   (`gl_buffer.cpp:269`).
2. Worker, immediately after and **still holding the context**:
   `Graphics_*_buffer_sink::buffer_ready` -> `Buffer_transfer_queue::enqueue`
   (`buffer_transfer_queue.cpp:24`). That entry is visible to the main thread
   the instant `enqueue()` returns -- it is behind a plain `std::mutex` with
   no dependency on the worker's GL context.
3. Main thread, possibly the very next tick: `mesh_memory->flush()`
   (`editor.cpp:801`) -> `Device_impl::upload_to_buffer` (`gl_device.cpp:1425`)
   -> `glCopyNamedBufferSubData` **into the worker-created buffer name**, on
   the main context.

The worker may not release its context for many more seconds -- section 9
holds one context for a whole finalize task. Between the enqueue and the
release there is no flush of any kind: the GL backend has `gl::fence_sync`
only on the main-context per-frame path (`gl_device.cpp:1640`) and
`gl::finish()` at `:1690` / `:2014`.

This looks like it works on Windows and Linux desktop, because WGL and GLX
implicitly flush on `MakeCurrent(NULL)` -- which covers release-then-use and
not the ordering above. Symptoms when it does bite are driver-dependent:
`GL_INVALID_OPERATION` on the copy, a zero-filled or garbage mesh, or an
access violation.

**Decision: flush at publication granularity, not at context release.** A
worker-created GL object must be flushed before its name escapes into
anything the main thread can read. Concretely:

- `gl::flush()` at the end of `Buffer_impl::allocate_storage` when the
  calling thread's role is either worker role; or
- a fence signalled on the worker and `glWaitSync`'d on the main context
  before the first use, if measurement shows the per-buffer flush is too
  costly.

Start with the flush -- it is one call per pool block, and pool blocks are
created rarely (a block is megabytes; `Buffer_pool::create_new_block` runs
only when a pool rolls over). Do **not** put the flush only in the context
release path, and do not record the release contract as sufficient.

**Is the flush sufficient, or is a fence required? The flush is
sufficient.** The GL spec's shared-object rule is that the creating context
flushes after the change and the consuming context binds or attaches the
object after that flush. The CPU-side happens-before is already supplied
here by `Buffer_transfer_queue`'s `std::mutex`
(`buffer_transfer_queue.cpp:25`), so the ordering half is established
independently. A fence is the right escalation only if the flush shows up in
a profile, not on spec grounds.

**Is buffer creation the only publication point?** For buffers, yes -- and
say so explicitly rather than leaving it implicit: this plan forbids workers
from mapping buffers (section 4), and `init_data` is passed at storage time,
so there is no worker-written data that postdates creation.

**For textures: decided (user, 2026-08-27) -- textures stay in the limited
tier, so the publication rule extends to cover them.** It resolves more
simply than it first looks, because of a fact worth stating plainly:

**A worker can allocate texture *storage*, but cannot *upload* pixels.**
`Texture_create_info` (`texture.hpp:17-44`) carries **no initial pixel
data** -- storage is allocated inside the `Texture_impl` constructor
(`gl_texture.cpp:662`, the `texture_storage_*` / `tex_storage_*` calls at
`:925-981`), and every pixel upload goes through
`Blit_command_encoder_impl` (`gl_blit_command_encoder.cpp:329`, `:359`,
`:376`, `:395`, and the compressed variants at `:484`, `:509`, `:528`),
whose construction section 4 places under
`ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()`.

Consequences, all three of which the implementation must respect:

- **The publication point for a texture is its constructor**, exactly
  parallel to `Buffer_impl::allocate_storage`: `gl::flush()` at the end of
  the storage-allocation branch when the calling thread's role is a worker
  role. Same reasoning, same escalation path to a fence if it ever shows up
  in a profile.
- **Pixels raise no publication question at all**, because the upload runs
  on the main thread by construction. That is a consequence of the
  main-only blit encoder, not an accident -- if a future call site wants
  worker-side upload, it needs its own publication rule and its own
  decision. Do not treat this as already answered.
- **A worker-created texture is empty until the main thread fills it.** That
  is a real constraint on any future call site, and it should be in the
  `Scoped_worker_context_limited` doc comment, not just here. Allocation is
  the part worth moving off the main thread anyway; the upload is a queued
  transfer.

Note that nothing creates a texture on a worker today
(`texture_file_loader.cpp:153` is decode-only), so this permission is
currently unexercised -- the same status as the full tier, and it carries
the same obligation: if it is not covered by a test, no run proves anything
about it.

The two call sites in section 9 both publish before their scope ends
(`async_raytrace_kickoff_operation.cpp:174` enqueues to the commit queue
inside the per-item loop, with more items still to build under the same
context; `gltf_load_task.cpp:132-141` does its `finished.store(...,
release)` as the last statement *inside* the lambda, before the scoped
destructor runs). So publication-point flushing is the only placement that
covers them.

## 6. Phase 3 -- the worker resource context API

The call sites (`deferred_finalize_mesh_items`,
`build_imported_buffer_meshes`, the lightmap partitioner) are **backend-shared
code**, so the API must exist on every backend and be a no-op on the ones that
do not need it.

### Two tiers, two types

The user's requirement: *"We do need separate scoped GL contexts - one which
can create vertex input state, and lesser which can create buffers and operate
on them"*. Two distinct scoped **types**, not one type with a flag:

| type | may create |
|---|---|
| `Scoped_worker_context_limited` | buffers, texture *storage*, samplers, and DSA operations on them |
| `Scoped_worker_context_full`    | the above **plus** `Vertex_input_state` (a VAO) |

They are separate types rather than a flag because the difference is not a
quantity, it is a different *lifetime contract*. The limited tier touches only
objects that GL shares across a share group, so an object it creates is the
same object on the drawing thread and its obligations end when the scope ends.
The full tier touches a **container object**, which is not shared, so it
carries an ownership hand-back obligation (below). A `bool` on one type would
let a call site opt into that obligation by accident.

`erhe_graphics/scoped_worker_context.hpp`:

```cpp
namespace erhe::graphics {

// id < 0 is the empty token. Deliberately NOT 0: the existing
// Gl_worker_context uses id 0 for both "main-thread no-op" and "worker
// context #0" (gl_context_provider.cpp:54-55 enqueues {i, context.get()};
// :100 verifies id == 0 for the no-op), so its release-on-wrong-thread
// check passes vacuously for the real context #0. A fresh API should not
// inherit that.
class Worker_context_token { public: int id{-1}; };

enum class Worker_context_tier { limited, full };

// LIMITED TIER.
//
// Grants the calling worker thread permission -- and, on OpenGL, an actual
// share context -- to CREATE GPU resources that GL shares across a share
// group: buffers, texture storage, samplers, and DSA operations on them.
//
// It may NOT create a Vertex_input_state (use the full tier), may not bind
// anything, may not map a buffer, and may not prepare or record a draw.
// Everything it may not do asserts (gl_thread_role.hpp).
//
// TEXTURES: this grants texture STORAGE ALLOCATION, not pixel upload.
// Texture_create_info carries no initial data and every upload goes through
// Blit_command_encoder, which is main-thread-only -- so a texture created
// here is EMPTY until the main thread fills it. That is by construction,
// not an oversight; see section 5.
//
// No-op on the main thread and on every backend that needs no per-thread
// context (Vulkan, Metal, null).
//
// RE-ENTRANT: nested construction on one thread refcounts and does not
// acquire a second context (see "Re-entrancy and taskflow subflows").
class Scoped_worker_context_limited final
{
public:
    explicit Scoped_worker_context_limited(Device& device);
    ~Scoped_worker_context_limited() noexcept;
    Scoped_worker_context_limited (const Scoped_worker_context_limited&) = delete;
    void operator=                (const Scoped_worker_context_limited&) = delete;
    Scoped_worker_context_limited (Scoped_worker_context_limited&&)      = delete;
    void operator=                (Scoped_worker_context_limited&&)      = delete;

private:
    Device&              m_device;
    Worker_context_token m_token;
};

// FULL TIER.
//
// Everything the limited tier grants, PLUS creating a Vertex_input_state.
//
// A VAO is a GL CONTAINER object and is therefore NOT shared across the
// share group: the VAO this scope creates does not exist on the drawing
// thread. On scope exit this type hands every Vertex_input_state created
// inside it back to the unowned pool, and the drawing thread creates its own
// VAO for that state on first use. See "The full tier and VAO ownership".
//
// Still may not bind, map or draw. Same re-entrancy rules.
//
// A nested Scoped_worker_context_limited inside a full scope is legal: it
// refcounts onto the same context and does not narrow the outer permission.
// The reverse -- a full scope nested inside a limited one -- ASSERTS. It
// would silently widen the outer scope's contract, and the outer scope's
// release does not perform the hand-back.
class Scoped_worker_context_full final
{
public:
    explicit Scoped_worker_context_full(Device& device);
    ~Scoped_worker_context_full() noexcept;
    Scoped_worker_context_full (const Scoped_worker_context_full&) = delete;
    void operator=             (const Scoped_worker_context_full&) = delete;
    Scoped_worker_context_full (Scoped_worker_context_full&&)      = delete;
    void operator=             (Scoped_worker_context_full&&)      = delete;

private:
    Device&              m_device;
    Worker_context_token m_token;
};

}
```

backed by `Device` methods forwarding to `Device_impl`:

```cpp
[[nodiscard]] auto supports_worker_contexts() const -> bool;
[[nodiscard]] auto acquire_worker_context(Worker_context_tier tier) -> Worker_context_token;
void               release_worker_context(Worker_context_token token);
```

The tier is a parameter of the *device* call, not a property of the context,
because both tiers draw from the same pool: the tier selects the thread role
installed and the release work performed, not which context is handed out.

- **Vulkan / Metal / null**: `supports_...` returns `true`, acquire/release
  return and ignore an empty token, both tiers identical. No cost, and no
  `#if` at the call sites.
- **GL**: acquire dequeues from the pool, `make_current()`s, and sets the
  thread role to `worker_resource_limited` or `worker_resource_full`. Release
  restores the previous role, `clear_current()`s and re-enqueues. Neither
  tier may call `OpenGL_state_tracker::on_thread_enter` /
  `on_thread_exit` -- that is the entire difference from `Scoped_gl_context`,
  and the reason this is a separate API rather than a flag on the old one.
  The full tier's release does the VAO hand-back below **instead of**, not by
  way of, `on_thread_exit`.

### The full tier and VAO ownership

This is the one hard fact the full tier has to answer, and the existing
machinery does not answer it as written. Decision: **reuse the existing
per-thread migration** (`Vertex_input_state_impl::on_thread_enter` /
`on_thread_exit`, `gl_vertex_input_state.cpp:24` and `:47`), the same path
`m_default_vertex_input_state` already rides (`gl_device.hpp:176-181`) --
but three properties of that machinery have to change first. All three were
verified against the tree; none are hypothetical.

**1. There is no re-adoption point on the main thread.** The main thread
calls `on_thread_enter()` exactly once, at `editor.cpp:2573`, and **never
calls `on_thread_exit()`**. `Vertex_input_state_impl::create()` is reached
only from the two constructors (`:321`, `:332`) and from `on_thread_enter`
(`:43`). So a state that a worker `reset()`s on scope exit goes unowned and
stays unowned forever: the main thread never re-enters to adopt it, and the
next `update()` or `gl_name()` on it trips
`ERHE_VERIFY(m_gl_vertex_array.has_value())` (`:390`).

  One correction to the symptom: `gl_name()` (`:511-518`) does **not** trip
  `ERHE_VERIFY(m_gl_vertex_array.has_value())` -- it returns `0`. What it
  trips first is `ERHE_VERIFY(m_owner_thread == std::this_thread::get_id())`
  at **`:513`**. `update()` trips `:389`, then `:390`. Any lazy-adoption
  design has to remove or invert the `:513` verify, so it is named here.

  Fix: make adoption **lazy and per-object** rather than a once-at-startup
  sweep, and retire the bulk sweep in `on_thread_enter`. **But it cannot be
  done inside `gl_name()`**, for four reasons, all verified:

  1. **Self-deadlock through the constructor.** `s_mutex` is a plain
     `std::mutex` (`gl_vertex_input_state.hpp:52`). Both constructors take it
     (`:317`, `:328`) and then call `create()` (`:321`, `:332`) ->
     `update()` (`:382`) -> `gl_name()` (`:391`, `:400`, ...). A `gl_name()`
     that takes `s_mutex` deadlocks on construction. One that does not takes
     the lock races the registry.
  2. **`gl_name()` is `const`, and is reached through a `const
     Vertex_input_state*`** -- `Vertex_input_state_tracker::execute` holds
     `const Vertex_input_state* const effective_state` and calls
     `...->get_impl().gl_name()` (`gl_state_tracker.cpp:499`). Creating from
     there needs `mutable` state plus a const-qualified mutation.
  3. **It is a per-draw hot path.** `execute()` runs on every pipeline bind;
     a mutex acquisition there is not free.
  4. It would put GPU object creation behind an accessor that every caller
     reasonably reads as a getter.

  **Adopt at a single explicit re-adoption point on the drawing thread
  instead**, called from a place that already runs per frame and already
  holds the right thread. `Vertex_input_state_tracker::execute` is the
  natural site *if* the adoption call is a separate, non-`const`, explicitly
  named step ahead of the `gl_name()` read -- not folded into the getter.
  Settle the exact site when implementing 6b; what is settled here is that
  `gl_name()` is not it.

  Two things that are **not** obstacles, checked so nobody re-opens them:
  `Vertex_input_state_tracker::m_last_state` caching is fine (the early
  return at `gl_state_tracker.cpp:506-508` happens *after*
  `bind_vertex_array(name)`, and the cached vectors are tied to the instance,
  not the name); and a recreated VAO does **not** need its recorded buffer
  bindings to survive, because `set_vertex_buffer` / `set_index_buffer` are
  re-issued per draw from `Render_command_encoder_impl`
  (`gl_render_command_encoder.cpp:118`, `:123`) and `update()` re-establishes
  only attribute formats, enables and divisors. Recreate-on-another-thread is
  sound in principle; only the mechanism had to change.

**2. `on_thread_enter` / `on_thread_exit` are whole-set, not per-object.**
(`on_thread_exit`'s `bind_vertex_array(0)` is at `:51`.)
`on_thread_enter` claims *every* state whose `m_owner_thread` is default
(`:41-43`); `on_thread_exit` `reset()`s *every* state this thread owns
(`:65-67`) and additionally does `gl::bind_vertex_array(0)` (`:50`). A
full-tier worker calling them would adopt every incidentally-unowned VAO in
the process -- including the pre-registered format VAOs and
`m_default_vertex_input_state` -- and destroy them on scope exit.

  Fix: the full tier's release must hand back **only the states created
  inside that scope**. Track them per-scope -- the thread-local depth record
  already needed for re-entrancy is the natural place -- and `reset()`
  exactly those. Do not call the static `on_thread_exit` from the worker
  path at all.

  **`Render_pass_impl` has the identical whole-set pattern** over
  `s_all_framebuffers` (`gl_render_pass.cpp:249-276`) -- framebuffers being
  the other non-shared container object -- and
  `OpenGL_state_tracker::on_thread_enter` / `on_thread_exit` fan out to it
  and to `Gpu_timer_impl` as well as to `Vertex_input_state_impl`
  (`gl_state_tracker.cpp:729-744`). Commit 6b retires only the
  `Vertex_input_state` sweep. That is correct **only so long as nothing ever
  creates a `Render_pass` on a worker** -- which is the tier table's own
  logic, applied to the other container object. Say so, and put a
  `Render_pass_impl` construction under the draw-capable guard so the
  assumption is enforced rather than assumed.

**2b. The hand-back itself would trip this plan's own draw-capable guard,
and race the shared binding cache.** `reset()` (`:355-366`) does
`m_gl_vertex_array.reset()`, running `~Gl_vertex_array`
(`gl_objects.cpp:315-323`), which calls
`m_binding_state->on_vertex_array_deleted(m_gl_name)` whenever
`m_binding_state` is non-null -- and `Device_impl::create_vertex_array`
always passes `&m_gl_binding_state` (`gl_device.cpp:2163`). Section 4 puts
all seven `on_*_deleted` hooks under `ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()`,
so **every hand-back asserts** -- on the one path verification item 7 exists
to exercise. The two sections contradict each other as written.

  The assert is the lesser problem. `Gl_binding_state::on_vertex_array_deleted`
  (`gl_binding_state.cpp:533-539`) writes `m_bound_vertex_array` and walks
  the `m_vertex_array_stack` vector with no synchronization, from a worker,
  while the main thread renders -- a real data race on a `std::vector`, not a
  stale cache. And because **VAO names are per-context**, the worker's VAO
  can legitimately carry the same numeric name as one the main context has
  bound, so the scrub is semantically wrong even when it does not race.

  Fix: **worker-created GL objects carry a null `binding_state`.**
  `~Gl_vertex_array` already guards on it, and the move constructor already
  supports null (`gl_objects.cpp:325-328`), so this is a constructor
  argument, not new machinery. Do **not** relax the guard. The same question
  applies to every `Gl_*` object a worker can create -- answer it once, for
  all of them, when implementing.

**3. The destructor leaks a dangling pointer into the static registry.**
`~Vertex_input_state_impl` (`:335-347`) early-returns when
`!m_gl_vertex_array.has_value()` **before** erasing `this` from
`s_all_vertex_input_states`. Today an unowned state is a startup-only
transient, so this never bites. Once hand-back makes "unowned" a normal
steady state, destroying a handed-back state leaves a dangling pointer that
the next sweep dereferences.

  Fix: erase from the registry unconditionally. It belongs in its own commit
  landing **before** the full tier -- it is a pre-existing latent bug, not
  one this plan introduces. **It is not quite a one-line change:** the early
  return also skips *taking* `s_mutex` (the lock is at `:341`, after the
  return at `:337-339`), so removing it changes the locking of every
  destruction -- including those during `Device_impl` teardown, when
  `m_default_vertex_input_state` is destroyed (`gl_device.hpp:176-181`).
  Verify no destructor can run with `s_mutex` already held on that thread.

Consequence for the thread-role guard (section 4): `Vertex_input_state_impl`
construction is legal under role `worker_resource_full`, and asserts under
`worker_resource_limited` and `none`.

### Does the full tier have a call site?

**Today, no.** Verified: the `Mesh_memory` constructor pre-registers an entry
for every format up front on the main thread (`mesh_memory.cpp:404-413`,
the call itself at `:412`), looping `get_all_vertex_formats()` -- which
returns all nine member formats, `:416-429` -- and calling
`get_vertex_input_from_vertex_format`. Every caller in
the tree passes one of those members (`mesh_memory.cpp:800`, `:804`, `:809`,
`:826`, `:830`, `:835`; `lightmap_baker.cpp:1417`, `:3268`), the lookup at
`:726-731` compares by value, so it always hits and the `emplace_back` at
`:734` is unreachable from a worker. Section 9 records the same correction.

**While asserting that invariant, fix the other half of it.**
`get_vertex_input_from_vertex_format` is also **not thread-safe on its read
path**: it walks the `m_vertex_input_entries` vector from workers with
nothing preventing a main-thread `emplace_back`. Nothing appends after
construction today -- which is exactly the unenforced coupling the section 9
hoist exists to remove. So the assert should be twofold: the miss path is
only ever taken on the main thread, **and** the vector is never grown after
construction.

The user's decision is to **specify and implement the full tier now anyway**.
That is a deliberate call, so the plan states the consequence plainly rather
than pretending a user exists:

- The full tier ships with **no production call site**, therefore no
  end-to-end coverage from the repro or from any asset load.
- It must therefore carry its own test. Verification (section 11) gains a
  targeted case: a worker task under `Scoped_worker_context_full` that
  constructs a `Vertex_input_state`, exits the scope, and then has the
  drawing thread bind and draw with it. That test is the *only* thing
  exercising the hand-back path, so it is not optional.
- The hygiene hoist in section 9 removes the latent dependency on
  constructor pre-registration; it does not create a full-tier user, and
  must not be recorded as one.

### The old provider is deleted -- do not reproduce its acquire bug

**Decided (user, 2026-08-27): the new API REPLACES `Gl_context_provider`, it
does not reuse it.** `Gl_context_provider`, `Gl_worker_context`,
`Scoped_gl_context`, `acquire_gl_context` / `release_gl_context` and
`provide_worker_contexts` are all **deleted**; the pool lives in `Device_impl`
behind the API above. That settles the question section 12 flagged: there is
no separate "fix the provider" commit, because the provider does not survive.
The analysis below is retained as a **specification for the replacement**, not
as a description of code to repair.

The deletion is file-scoped, and the reference list is short -- verified:
`erhe_graphics/gl/gl_context_provider.cpp` and `.hpp` go entirely, along
with their two lines in `src/erhe/graphics/CMakeLists.txt:130-131`. The only
other referents are `gl_device.hpp` / `gl_device.cpp` (the `m_gl_context_provider`
member and its uses, which the new pool takes over) and
`src/editor/transform/trs_tool.cpp` -- whose use sits inside the `#if 0`
block section 7 already deletes, so it needs no separate handling.

`Gl_context_provider::acquire_gl_context` does not block. `gl_context_provider.cpp:78-83`:

```cpp
while (!m_worker_context_pool.try_dequeue(context)) {
    std::unique_lock<...> lock(m_mutex);
    m_condition_variable.wait(lock, []{ return true; });   // predicate always true
}
```

The always-true predicate makes `wait` return immediately, so this is a
**busy spin**, not a wait. Today it is unreachable (the provider is dead
code). The moment the pool can be exhausted it becomes a livelock burning
every core -- and one that a profiler shows as *busy*, not idle, so it does
not look like a deadlock.

**Do not "just add a real predicate" -- that converts the spin into a
lost-wakeup deadlock.** Two facts make the obvious fix wrong:

- `m_worker_context_pool` is a `moodycamel::ConcurrentQueue<Gl_worker_context>`
  (`gl_context_provider.hpp:54`). It has no exact `empty()`, only
  `size_approx()`. There is nothing to write a correct predicate against.
- The producer enqueues and calls `notify_one()` **outside** `m_mutex`
  (`gl_context_provider.cpp:112-113`), and the consumer's `try_dequeue`
  (`:78`) is outside it too. A waiter that fails `try_dequeue` and is then
  preempted before `wait()` misses the notify and sleeps forever. The
  always-true predicate is precisely what hides this today.

**So the replacement pool must be written with a `std::counting_semaphore`
from the start** (or a plain `std::deque` under a mutex -- the lock-free queue
buys nothing for a 4-entry pool acquired around a globally serialized
critical section). Note the irony worth recording: a half-fix here is the same
invisible-to-a-profiler hang the spin already risks, except it then looks
*idle* rather than busy.

Because the provider is deleted rather than fixed, this is **not**
independently committable ahead of the new API. It lands inside commit 6.

### Re-entrancy and taskflow subflows

`Lightmap_partitioner` runs one region task per region and fans per-piece
work into `subflow->emplace(...)` followed by `subflow->join()`
(`lightmap_partitioner.cpp:262-275`); the GPU allocation is in
`process_piece` (`make_renderable_mesh`, `:490`), **not** in
`process_region`. That breaks the naive "scope at the top of the worker
lambda" rule in both directions:

- Scope in `process_region`: child piece tasks are stolen by *other* workers
  holding no context. Those threads have role `none` -> phase-1 assert, or
  the original AV. The scope does not cover the code that needs it.
- Scope in `process_piece`: with a pool of `num_workers`, all region tasks
  can hold a context while parked in `join()`, and every child that reaches
  acquire finds the pool empty.

And taskflow's co-run during `join()` may execute *any* queued task on that
thread -- including another region task, which would `make_current()` a
second context and, on scope exit, `clear_current()` and reset the role,
leaving the outer task holding a live token with **no current context and
role `none`**.

**What that machinery is actually defending against -- state it, or the
reviewer of commit 6 cannot tell.** With the scope in `process_piece` (this
plan's own choice), the region task parks in `join()` holding *no* context,
so the co-run-steals-the-context scenario cannot arise from this call site.
The save/restore design is therefore a **by-construction guard against
future call sites**, not a fix for the lightmap case. It is still worth
having -- section 9 item 5 shows how quickly new worker call sites appear --
but it should not be presented as required by this one.

The part of this case that *is* load-bearing: `process_piece` also runs on
the **main thread** via the serial path at `lightmap_partitioner.cpp:520`, so
the "no-op on the main thread" behaviour is exercised in production, not just
in theory.

So the API must be re-entrant, and acquisition must save and restore:

- a thread-local depth counter; nested construction refcounts and returns
  the same context rather than acquiring another;
- acquire saves the previously-current context and release restores it,
  rather than unconditionally clearing;
- the release path restores the previous *role*, not unconditionally `none`.

### Pool sizing: small and fixed, created eagerly on the main thread

Do **not** size the pool at `num_workers`. The GL work behind these contexts
is globally serialized anyway: `erhe::primitive::buffer_mesh_allocation_mutex()`
(`buffer_mesh.cpp:5`) is taken around every buffer-mesh allocation
transaction (`:35`, `:46`, `mesh_optimizer.cpp:635`, `primitive.cpp:953`), so
at most one thread is ever inside buffer creation. `num_workers` contexts on
a 32-thread box means 32 hidden SDL windows and 32 driver-side context
states created for a mutually-exclusive fraction of the work.

Use a small fixed pool (start at 4) with the real blocking wait from above.
That gives the same throughput at a fraction of the startup and driver cost,
and it surfaces the re-entrancy problem immediately instead of only on
high-core machines.

**With one caveat that section 9 raises**: a pool of 4 gives the same
throughput only while the scope stays *narrow*, around the allocation. If the
scope is hoisted above the per-mesh loop to avoid make-current churn, it also
spans the BVH build and geometry conversion, and 4 contexts then cap the whole
deferred finalize at 4 concurrent meshes. Pool size and scope width are one
decision, not two -- see section 9 item 1.

**Create the contexts eagerly, on the main thread, at startup.** An earlier
revision of this plan said "lazily, on first acquire" and left the conflict
with section 8 as "decide when implementing". That was not a deferred
decision, it was a contradiction, and the source settles it:

- `Context_window::open()` calls `configuration.share->make_current()` at
  `sdl_window.cpp:717` *before* setting
  `SDL_GL_SHARE_WITH_CURRENT_CONTEXT` at `:718`. Creating a share context
  therefore **steals the main context away from whichever thread currently
  holds it**. Doing that from a worker while the main thread is mid-frame is
  immediate corruption, not a portability caveat.
- `open()` also calls `SDL_CreateWindow` (`:724`), mutates the global
  `s_window_count` non-atomically (`:871`, `:911`), and registers an SDL
  event watch (`:877`). SDL window creation is main-thread-only by contract
  on Windows and macOS.

First acquire happens on a worker, so lazy creation is not implementable
without a main-thread request/response hop -- which buys nothing over a fixed
pool of 4. **Strike lazy creation**, and merge commits 4 and 5 accordingly
(section 12).

## 7. Phase 4 -- retire ERHE_PARALLEL_INIT

**Correction to an earlier revision of this section: `ERHE_PARALLEL_INIT` is
not "defined nowhere in the tree".** It is defined at `editor.cpp:5`, inside
`#if !defined(ERHE_SERIAL_INIT)` -- and `ERHE_SERIAL_INIT` is defined
unconditionally at `:2`. So the blocks *are* dead and every deletion below
still stands, but for the stated reason rather than the claimed one. The
distinction matters: someone deleting `ERHE_SERIAL_INIT` would silently
revive all of it.

Its blocks are dead and one of them does not even compile as written:
`editor.cpp:1616` reads `m_graphics_device->context_provider`, a member the
abstraction `Device` does not have.

Delete the `ERHE_SERIAL_INIT` / `ERHE_PARALLEL_INIT` defines themselves
(`editor.cpp:1-6`) along with the blocks, and update the two docs that
describe them: `src/editor/notes.md:160`, which says *"Currently serial init
is the default due to GL context sharing issues"* -- a sentence this entire
plan invalidates -- and `doc/init_status_display_phase_ii.md:13`.

Line numbers in this section drifted and are corrected here: the macro
`#endif` is at `:1457`, and `m_executor->run(taskflow)` is a **separate**
block at `:1459-1461` (an earlier revision merged them as "1449-1460"). The
`#if` arm called `2522-2560` actually closes at `:2563`. The `#if 0` in
`trs_tool.cpp` closes at `:1382`, not `:1380`, and the `Scoped_gl_context`
inside it is at `:152`.

Delete: `editor.cpp:1434-1436`, `1449-1460` (keeping the `#else` arm of the
`ERHE_GET_GL_CONTEXT` / `ERHE_TASK_HEADER` / `ERHE_TASK_FOOTER` macros as the
unconditional definitions), `1615-1621`, and the `#if` arm at `2522-2560`.

`src/editor/transform/trs_tool.cpp` also names `Gl_context_provider` and
`Scoped_gl_context`, but the entire file sits inside `#if 0` (line 1 to line
1380) and is a leftover of the retired Component system. Leave it alone -- but
note it, so a grep hit does not read as a live call site.

## 8. Phase 5 -- context creation and lifetime

### Creation

The creation loop must run on the main thread: SDL's share-context path does
`configuration.share->make_current()` (`sdl_window.cpp:717`, before
`SDL_GL_SHARE_WITH_CURRENT_CONTEXT` at `:718`), so creating a context
**steals the main context away from whichever thread currently holds it**.
It also calls `SDL_CreateWindow` (`:724`), mutates the global
`s_window_count` non-atomically (`:871`, `:911`), and registers an SDL event
watch (`:877`).

**Resolved (this was a contradiction, not a deferred decision): the pool is
created eagerly, on the main thread, at startup.** An earlier revision left
"lazy creation, resolve when implementing" standing against section 6's
"create lazily on first acquire" -- but first acquire happens on a worker, so
lazy creation is not implementable without a main-thread request/response
hop, and that hop buys nothing over a fixed pool of 4. Section 6's lazy
wording is struck, and commits 4 and 5 merge accordingly (section 12).

### Lifetime -- three defects that this plan would otherwise introduce

`Context_window` has two pre-existing leaks that are harmless at one or two
instances and are **not** harmless at pool scale:

1. **The SDL event watch is never removed.** `Context_window::open`
   registers `SDL_AddEventWatch(Context_window_SDL_EventFilter, this)`
   unconditionally, share contexts included (`sdl_window.cpp:877`). There is
   no `SDL_RemoveEventWatch` anywhere in `src/erhe/window/`, and
   `~Context_window` (`sdl_window.cpp:888-914`) destroys the SDL window but
   leaves the watch registered with a dangling `this`. Worker contexts are
   owned by `Device_impl` -- by the new pool that replaces
   `Gl_context_provider` (`gl_device.hpp:172` today) -- and are destroyed
   with the Device, while `m_window` (`editor.cpp:4077`,
   destroyed *after* `m_graphics_device` at `:4078`) is still pumping
   events. That is a shutdown use-after-free **introduced by this plan**.
   Add `SDL_RemoveEventWatch` to the destructor.
2. **`SDL_GL_DestroyContext` is never called.** Absent from
   `sdl_window.cpp` entirely; only `SDL_GL_CreateContext` at `:793`. One
   leaked context today, one per pool entry under this plan. Add it.
3. **Worker contexts have GL debug enabled with no callback.** The share
   constructor requests `SDL_GL_CONTEXT_DEBUG_FLAG` in Debug builds
   (`sdl_window.cpp:710`), but the callback is installed at
   `gl_device.cpp:319` (`gl::debug_message_callback(erhe_opengl_callback,
   nullptr)`) -- once, on the Device's own context. An earlier revision cited
   `sdl_window.cpp:808-810`, which is the primary-window make-current and
   `get_extensions()`, not the callback. The conclusion is unchanged and is
   the important part: `glDebugMessageCallback` is **per-context** GL state,
   so every GL error a worker raises is **silently discarded** -- exactly the
   class of failure the cross-context and re-entrancy problems above produce.
   Install the callback on each worker context at creation, or this plan's
   own verification is blind.

Fix all three in their own commit, before the pool exists.

### When contexts cannot be created

After phase 0, "no DSA" is no longer one of the cases -- a GL device without
DSA fails at creation. What remains is a GL device whose window cannot
produce a share context (headless / null window), and any failure in the
creation loop. Then no contexts exist and
`Device::supports_worker_contexts()` returns false, and every
GPU-touching worker call site reads as:

```cpp
if (device.supports_worker_contexts()) { /* worker path */ }
else                                            { /* main-thread path */ }
```

**The fallback must be budgeted, not inline.** Routing
`prepare_geometry_buffer_mesh` into the `Scene_commit_queue` lambda, or
`build_imported_buffer_meshes` inline into `tick()`, puts unbounded work on
the frame: `Scene_commit_queue::flush()` runs first thing in `Editor::tick`
(`editor.cpp:610`), before rendering, so a Bistro-sized load becomes a
multi-second stall per tick -- an apparent hang, on exactly the headless /
null-window configurations that take the fallback. Use the existing
per-frame budget machinery (`flush_budgeted`,
`Asset_load_tick_context::budget`) instead.

For `Lightmap_partitioner`, note that the serial path is chosen at launch
time (`lightmap_partitioner.cpp:514`), not inside `process_region`, so the
predicate has to be evaluated in `request_prepare`.

## 9. Phase 6 -- the call sites

Confirmed GPU-allocating worker tasks:

1. `deferred_finalize_mesh_items`, `async_raytrace_kickoff_operation.cpp:85`
   -- the phase-A prepare loop. **This is the one with the repro.** Note it
   is dispatched one task per mesh (`items.cpp:170`
   `silent_dependent_async`), so on Bistro that is thousands of
   acquire/release pairs and thousands of `SDL_GL_MakeCurrent` calls, each a
   heavyweight driver operation.

   **But do not simply hoist the scope above the per-mesh loop.** The loop
   body (`async_raytrace_kickoff_operation.cpp:85`, dispatched per mesh from
   `Async_raytrace_kickoff_operation::execute`, `:231-240`) contains the
   *expensive CPU work*: `prepare_real_raytrace`, the BVH build, at `:126`,
   and geometry conversion inside `prepare_geometry_buffer_mesh` at `:132`.
   Holding a context across all of that against a fixed pool of 4 caps the
   whole deferred finalize at 4 concurrent tasks on a 32-thread box -- a
   large parallelism regression on exactly the Bistro-scale load this plan
   uses as its stress test. The `buffer_mesh_allocation_mutex` argument for a
   small pool applies to the *allocation*, not to the surrounding geometry
   work. So: either keep the scope narrow (around allocation only) and accept
   the make-current cost, or size the pool to the concurrency actually
   wanted. Measure before choosing.
2. `Gltf_load_task::start_build`'s `silent_async` lambda,
   `gltf_load_task.cpp:132` -- `build_imported_buffer_meshes` runs the soup
   path's `Buffer_pool` allocation on a worker: the same crash, one code path
   over. It has simply not been reached yet, because the deferred finalize
   crashes first.
3. `Lightmap_partitioner::process_piece` (**not** `process_region`) -- see
   the subflow discussion in section 6. It reaches `make_renderable_mesh` at
   `lightmap_partitioner.cpp:194` (an earlier revision cited `:490`, which is
   unrelated task-construction code). The serial-versus-parallel decision is
   at `:514`, inside `request_prepare`.

**An earlier revision of this section listed exactly those three sites and
cleared everything else as CPU-only. That audit was wrong.** At least two
more worker paths allocate GPU buffers, both verified in the tree, and both
were on the "confirmed CPU-only" list:

4. **The geometry graph evaluates on a worker and builds renderable meshes.**
   `geometry_graph_window.cpp:720` starts a `silent_async` whose body calls
   `run->shadow_graph.evaluate_if_dirty()` at `:729`. That reaches
   `Geometry_graph::evaluate` (`geometry_graph.cpp:106`, `:114`), which calls
   `evaluate` on each node at `:142` -- reaching
   `Geometry_output_node::evaluate` and `make_renderable_mesh` at
   `geometry_output_node.cpp:189` and the ghost variant at `:239` -- and
   `build_preview_primitive` at `:145`, reaching `make_renderable_mesh` at
   `geometry_graph_node.cpp:325`.

5. **Mesh operations are constructed on a worker, and their constructors
   build renderable meshes.** `Operations::async_mesh_operation`
   (`operations_window.cpp:658`) does
   `std::make_shared<T>(std::move(mesh_operation_parameters))` inside the
   worker lambda -- the comment at `:662` says so explicitly. Those
   constructors call `make_entries` (e.g.
   `Catmull_clark_subdivision_operation`, `geometry_operations.cpp:50-55`),
   which reaches `make_renderable_mesh` at `mesh_operation.cpp:341`, and the
   CSG path reaches it at `geometry_operations.cpp:689`. Section 9 cited
   `items.cpp:170` only as the dispatcher for `deferred_finalize_mesh_items`;
   every *other* consumer of `async_for_nodes_with_mesh` was unaccounted for.

**Failure mode if this is not fixed:** after the plan lands, glTF loads work
and the first subdivide, CSG or geometry-graph evaluation still faults in the
driver -- or, post-phase-1, trips `ERHE_VERIFY_GL_THREAD_HAS_CONTEXT`. Section
11's verification would **not** catch it: every item there loads a scene and
renders, and none performs a mesh edit.

**Still open, needs an explicit check before implementing:**
`gltf_fastgltf.cpp:1109`, `:1165` and `:1571` run nested taskflows inside
`parse_gltf`, which itself already runs on a worker
(`gltf_load_task.cpp:187`). The plan asserts parse is CPU-only without
addressing the nested flows. Note that if a scope is taken on the parse
thread and a nested taskflow steals work to *other* threads, the re-entrancy
design does not help -- it is the same shape as the lightmap subflow problem.

Audited and confirmed CPU-only -- no context, no change: `Asset_browser` glTF
scan (`asset_browser.cpp:350`), `Texture_file_loader` decode
(`texture_file_loader.cpp:153`; pixels only, the upload is on main),
`Lightmap_streamer` tile read (`lightmap_streamer.cpp:299`), the BVH TLAS
build (`bvh_scene.cpp:363`), and `Gltf_load_task`'s scan
(`gltf_load_task.cpp:91`).

**Treat this list as re-derived once and still not proven complete.** It has
now been wrong once; the cheap structural check is that every
`silent_async` / `silent_dependent_async` / `subflow->emplace` in
`src/editor` is accounted for, not that each looked CPU-only on inspection.

### The Scene_builder Vertex_input_state item -- corrected

An earlier revision of this plan claimed `Scene_builder::make_brushes`
workers create a `Vertex_input_state` (a VAO) and that "only luck keeps it
from faulting". **That mechanism is wrong.** The `Mesh_memory` constructor
pre-registers an entry for *every* format up front, on the main thread:
`mesh_memory.cpp:404-413` loops `get_all_vertex_formats()` calling
`get_vertex_input_from_vertex_format(*format)`. By the time any worker runs,
the lookup at `:724-731` always hits an existing entry and never reaches the
`emplace_back` at `:734`.

Consequences:

- The hoist is still worth doing, as hygiene: it removes a live dependency
  on constructor pre-registration that nothing states or enforces. Keep it
  as its own commit, but describe it as removing a latent coupling, not as
  fixing a live crash.
- **It cannot be verified by the phase-1 guard.** The guard on
  `Vertex_input_state_impl::create` is unreachable from `Scene_builder`
  whether or not the hoist lands, so "no assert fired" is a guard that was
  never reached. Do not record it as evidence. If the invariant is worth
  enforcing, assert it where it actually holds -- e.g. verify in
  `get_vertex_input_from_vertex_format` that the miss path is only ever
  taken on the main thread.

`Brush` itself is safe: `Scene_builder::make_brush` only constructs the
`Brush`, and `Brush::late_initialize()` -- which calls `make_renderable_mesh`
-- is lazy and reached from main-thread paths.

`Programs`' shader compile / link taskflow (`programs.cpp:106`, `:127`) is
commented out. If revived it needs a worker context; record that in
`src/erhe/primitive/erhe_primitive/notes.md`.

## 10. Known pre-existing race this plan makes reachable

`Mesh_memory::allocate_vertex_buffer_range` does
`m_vertex_pools.emplace_back(...)` on the worker (`mesh_memory.cpp:525`;
index pools `:612`) -- a `std::vector` reallocation -- under
`buffer_mesh_allocation_mutex()`. The main thread's `Mesh_memory::flush`
iterates `for (Buffer_pool& pool : m_vertex_pools)` at `:925` **without**
taking that mutex (it is taken only inside `apply_ready_pending_frees`,
`:904`). `Mesh_memory::get_vertex_buffer` (`:752`) does `m_vertex_pools.at(...)`
on the main thread, also unguarded.

This is pre-existing, but the plan converts it from unreachable -- the GL
build crashes before it gets there -- into the steady state, and section 9
widens it to three concurrent sites. `Pool_block` being `unique_ptr`-owned
means the `Buffer*` handed to `Buffer_transfer_queue` stays stable; the race
is on the pool **vector**, not the buffers.

**The "take the mutex in `flush` and `get_vertex_buffer`" option is a
partial fix that looks complete.** Those are two of many unguarded
main-thread readers of `m_vertex_pools` / `m_index_pools`. The rest, all in
`mesh_memory.cpp`: `:478-480`, `:559`, `:581`, `:600-602`, `:641`, `:663`,
`:757` (`get_vertex_buffer(Pool_buffer_identity)`), `:763` and `:769`
(**both `get_index_buffer` overloads**, never mentioned), `:777`, `:783`,
and `:842-850` (`get_memory_usage`). `flush` also iterates the index pools at
`:928`, not only the vertex pools at `:925`.

**Decide explicitly**, and given that count the **stable-address container is
the better option** -- the mutex option needs the full list above or it
ships as a fix that is not one. The third option, deferring with a written
reason, remains open. Do not leave it unstated.

## 11. Verification

Phase 0 has its own gate in section 3f; run it first. Then:

1. **The repro.** `build_vs2026_opengl` Debug, `editor.exe --scene
   res/editor/assets/ABeautifulGame.glb` from `D:\erhe`. Loads, renders, no
   assert, and specifically no `ERHE_VERIFY(vao != 0)` in
   `Vertex_input_state_tracker::set_index_buffer`. Then `controller_left.glb`,
   the procedural default scene, then Bistro
   (`res/editor/assets/niagara_bistro/bistro.gltf`) -- large enough to
   exhaust a 4-context pool and exercise the blocking wait.

2. **GL errors are actually observable.** Before trusting any run below,
   confirm the worker-context debug callback from section 8 is installed and
   fires: provoke a deliberate GL error on a worker and see it logged. Every
   other check here is worthless while worker GL errors are discarded.

3. **Cross-context publication.** The flush from section 5 is on the
   buffer-creation path. Mutation-check it: remove the flush and confirm
   something observably breaks under a driver that does not implicitly flush
   (or, failing that, note explicitly that the check could not be run and
   the flush is retained on spec grounds).

4. **Guards fire, and nothing legitimate trips them.** After a full glTF load
   plus a few hundred frames, no `on_*_deleted` and no binding-state assert
   has fired on a worker. Mutation-check the guard by temporarily placing an
   `ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()` inside the worker prepare loop.

   **Scope of this evidence:** a happy-path load exercises no error paths.
   `Gl_buffer::~Gl_buffer` calls `on_buffer_deleted` unconditionally, so
   worker-side handle destruction on a failure path (`create_new_block`
   returning false, `buffer_pool.cpp:210`; a throwing `make_unique<Buffer>`)
   trips the guard and a successful run proves nothing about it. Either
   force one of those paths, or record the limit rather than claiming the
   class is clear.

5. **The other backends stay unaffected.** `ninja` Vulkan Debug + Release, VS
   null backend, VS vulkan-headless, Quest APK. Load `ABeautifulGame.glb` on
   Vulkan and confirm pixel-identity to a pre-change capture via
   `scripts/mesh_ab_capture.py` (MCP port 3743, DDGI off, paused time,
   control pair first).

6. **Shutdown is clean.** Run under ASan with the pool populated, load a
   glTF, exit. No use-after-free from the SDL event watch, no leaked
   contexts.

7. **The full tier's hand-back, which nothing else exercises.** The full
   tier has no production call site (section 6), so items 1-6 above prove
   nothing about it. A dedicated test is mandatory, not optional: a worker
   task under `Scoped_worker_context_full` constructs a
   `Vertex_input_state`, the scope exits, and the drawing thread then binds
   and draws with that state.

   **Do not test this by comparing VAO names.** VAOs are container objects
   with a **per-context name space**: both contexts allocate names from 1
   upward, so a correctly re-created VAO on the drawing thread will very
   often carry the *same* numeric name as the worker's. A
   names-must-differ criterion produces false failures, and passes for the
   wrong reason when it passes. Test the mechanism: assert the state is
   observably **unowned** between scope exit and first drawing-thread use,
   and that `m_owner_thread` equals the drawing thread's id afterwards.

   Mutation-check the tier boundary too: construct a `Vertex_input_state`
   under `Scoped_worker_context_limited` and confirm
   `ERHE_VERIFY_GL_THREAD_CAN_CREATE_VERTEX_INPUT()` fires. Without this,
   the two types are indistinguishable at runtime and the split is
   documentation only.

8. **The worker call sites section 9 originally missed.** Items 1-6 load
   scenes and render; none performs a mesh edit, so none would have caught
   the geometry-graph and mesh-operation paths. Explicitly exercise, in the
   GL build:
   - a Catmull-Clark subdivide and a CSG operation on a selected mesh
     (`operations_window.cpp:658` -> `mesh_operation.cpp:341` /
     `geometry_operations.cpp:689`);
   - a geometry-graph evaluation that produces output and preview geometry
     (`geometry_graph_window.cpp:720` -> `geometry_output_node.cpp:189`,
     `:239`, `geometry_graph_node.cpp:325`);
   - a lightmap partition run, taking the parallel path
     (`lightmap_partitioner.cpp:514`), and separately the serial path so the
     main-thread no-op is covered.

   These can be driven through the editor MCP server (127.0.0.1:8080) rather
   than by hand.

9. **The limited tier's texture permission, which nothing else exercises
   either.** Textures are in the limited tier by decision, but no production
   call site creates a texture on a worker, so items 1-8 prove nothing about
   it -- the same situation as the full tier, and it earns the same
   treatment. A targeted case: a worker task under
   `Scoped_worker_context_limited` constructs a `Texture`, the scope exits,
   and the main thread then uploads to it through a blit encoder and samples
   it. Assert the texture is legible from the drawing thread, i.e. that the
   constructor's publication flush actually published the storage.

   Mutation-check the boundary the same way: attempt a pixel upload from
   inside the worker scope and confirm the blit encoder's
   `ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()` fires. Storage-not-upload is the
   whole content of this permission; if it is not enforced it is not real.

10. **Tests.** `build_tests`, once, at the end of the phase, with
    `ERHE_MCP_TEST_TIMEOUT_S=1`.

## 12. Commit split

0. `graphics: drop macOS OpenGL support` -- section 3a + 3b.
0b. `graphics: require OpenGL 4.5 and delete the non-DSA emulation` --
    section 3c. Separate so a bisect distinguishes "Apple config removed"
    from "emulation removed".
0c. *(optional, separate)* `graphics: collapse gates that GL 4.5 makes
    constant` -- section 3d. Not required by anything below.
1. `graphics: GL thread-role guards` -- section 4. Turns the crash into a
   named assert. Safe in isolation: the only GL path reaching
   `create_buffer` off the main thread is the glTF load, which already
   faults.
2. `window: fix Context_window event-watch and GL context teardown` --
   section 8's three lifetime defects, plus the worker debug callback.
   **Must land before any pool exists.**
3. *(removed -- see below.)* An earlier revision had
   `graphics: blocking wait in Gl_context_provider::acquire` here. **The new
   API replaces the provider rather than reusing it (user decision), so that
   commit would fix code commit 6 deletes.** The semaphore requirement moves
   into commit 6, where the replacement pool is written.
4. `editor: retire ERHE_PARALLEL_INIT` -- section 7, mechanical. **Merges
   with commit 5's context creation** per section 8: eager main-thread
   creation is now settled, so these cannot be separated.
5. `scene: build brush Build_info on the main thread` -- section 9, hygiene.
5b. `graphics: erase Vertex_input_state_impl from the registry
    unconditionally` -- section 6's VAO fix 3. A one-line pre-existing
    dangling-pointer fix, standalone and independently correct. **Must land
    before commit 6b**, which is what makes "unowned" a steady state.
6. `graphics: limited GL worker contexts` -- sections 5, 6 and 8: the
   `Scoped_worker_context_limited` half of the API, re-entrancy, the pool,
   the publication flush, the semaphore-based acquire, and the deletion of
   `Gl_context_provider` / `Gl_worker_context` / `Scoped_gl_context` /
   `provide_worker_contexts` entire.
   **Merged with the call sites below.** Lazy creation is no longer an
   option (section 8), so this is the only way to avoid a commit that creates
   contexts nobody uses -- a pure regression: startup cost plus, before
   commit 2, a shutdown crash.
6b. `graphics: lazy per-object VAO adoption` -- section 6's VAO fixes 1 and
    2: `gl_name()` / `update()` create on demand, the `on_thread_enter`
    bulk sweep retires. Behaviour-neutral on its own (the main thread's
    single `on_thread_enter` becomes a no-op because every state is already
    owned), which is exactly why it is separable and bisectable.
6c. `graphics: full GL worker contexts` --
    `Scoped_worker_context_full`, the per-scope created-state tracking, the
    hand-back on release, and
    `ERHE_VERIFY_GL_THREAD_CAN_CREATE_VERTEX_INPUT()`. Depends on 5b and
    6b. Ships with **no production call site**, so it must carry
    verification item 7 in the same commit -- otherwise this commit adds
    untested code to the tree with nothing that would notice it breaking.
7. `editor: take a worker resource context where GPU allocation runs off the
   main thread` -- section 9's three call sites and the budgeted fallbacks.
   All three take the **limited** tier.
8. `editor: print a callstack for structured exceptions` -- see below.

Section 10's `Mesh_memory` pool-vector race gets its own commit or an
explicit written deferral; it is not covered by any of the above.

## 13. Aside: the crash handler

`unhandled_exception_filter` (`src/editor/crash_handler.cpp:73`) writes a
minidump but prints **no callstack** for a structured exception, and this
machine has no cdb / windbg. A temporary `erhe_dump_callstack()` call there is
what produced the stack in section 1. Making it permanent is a two-line change
and belongs in its own commit.
