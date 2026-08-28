# OpenGL worker-thread GL contexts -- plan

Status: PLAN is drafted, needs review, nothing implemented.
Important: Read doc/gl-spec-section-5.md - consider it carefully, apply
it to this plan when you review the plan.

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

## 2. The design

Worker threads get GL contexts, and a worker can reach **every** object --
safely. The invariant that makes it safe:

1. Transitional rule, until the per-context caches land (section 10):
   **A worker may never touch state that is SHARED with the drawing thread.**
   Concretely, the `Gl_binding_state` and `OpenGL_state_tracker` software
   caches: single instances that describe *per-context* GL state, so a worker
   writing them corrupts the main thread's model of its own context. This is
   what section 4's guards enforce. The rule holds only between the guard
   commit and the per-context-caches commits (4 through 11 in section 13),
   and **no worker context exists during that window** -- section 10 makes
   both caches per-context, retiring the rule before any worker call site
   lands (commit 12).
2. **Per-context GL state on the worker's own context is fine.** A worker may
   set its own pixel-store parameters and bind its own pixel-unpack buffer,
   because those belong to its context and nothing else reads them. What it
   may not do is route such a change through a cache belonging to another
   context. Once section 10 lands, the worker's own per-context
   `Gl_binding_state` / `OpenGL_state_tracker` pair is part of "its own"
   state.
3. **Container objects are not shared** -- VAOs, framebuffers (and, per the
   spec's container list, transform-feedback and program pipeline objects as
   well, but neither is used in this tree).
   A worker reaches one through an explicit
   per-object accessor (`Scoped_vertex_input_state`, `Scoped_framebuffer`;
   section 6), which gives it an instance on its *own* context.
4. Obtaining a worker context goes through an **API explicitly for that
   purpose**, either new API different from the existing draw-capable
   `Scoped_gl_context`, or (perhaps preferred) `Scoped_gl_context` API is
   modified so it is always explicit what capabilities each `Scoped_gl_context`
   instance has.
5. Anything a worker may not do (accessing `Scoped_vertex_input_state` or
   `Scoped_framebuffer`, for example) **asserts**, loudly, at the call site,
   when the `Scoped_gl_context` instance misses the matching capability.

Note what rule 1 does *not* forbid, because the stronger reading -- "a worker
may never change a binding" -- is wrong and puts two boundaries in the wrong
place. Texture upload is worker-legal: the upload path binds a pixel-unpack
buffer, which is per-context, not shared. `blit_framebuffer` is worker-legal:
it needs an FBO, so the worker needs *its own*, which is exactly what the
`Scoped_framebuffer` accessor provides.

Section 10 (phase 7) makes `Gl_binding_state` and `OpenGL_state_tracker`
themselves per-context, retiring rule 1 altogether. It is an integral part
of this plan, sequenced **before** the worker contexts go live -- see the
commit split (commits 10-11).

### Why DSA-only object creation is sufficient (verified, do not re-derive)

- The worker's GL work is already DSA-clean when `use_direct_state_access` is
  true: `glCreateBuffers` (`Device_impl::create_buffer`),
  `glNamedBufferStorage` (`gl_buffer.cpp:269`), `glMapNamedBufferRange`
  (`:402`, `:461`, `:536`), `glObjectLabel`. None of those touch a binding
  point.
- Minor correction: Here "DSA-clean" is used a bit loosely. In theory, creating a
  texture view requires use of non-DSA API (`glGenTextures` call insted of
  `glCreateTextures`) - however, this should have no impact on the plan, since
  `Gl_binding_state` and `OpenGL_state_tracker` remain untouched.
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
  framebuffers -- are *not* shared, which is why they need section 6's
  accessors rather than nothing at all.

### Why the naive fix is worse (do not repeat)

Wiring plain `Scoped_gl_context` into `deferred_finalize_mesh_items` fixes the
`glCreateBuffers` fault -- the scene loads and renders -- and then corrupts
main-thread rendering, failing `ERHE_VERIFY(vao != 0)` in
`Vertex_input_state_tracker::set_index_buffer` (`gl_state_tracker.cpp:524`).

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

The worker context API never runs those hooks -- that is the entire
difference from `Scoped_gl_context`. The per-thread VAO machinery those hooks
implement is not merely bypassed but **deleted** by section 6, in favour of
per-context instances.

## 3. Phase 0 -- remove macOS OpenGL support, and make DSA mandatory

**This phase comes first and the rest of the plan depends on it.**

macOS caps OpenGL at 4.1: no `ARB_direct_state_access`, no
`ARB_clip_control`, no compute shaders, no SSBOs, no `ARB_debug_output`, no
`ARB_internalformat_query2`. The whole non-DSA / pre-4.3 emulation layer in
the GL backend exists for that one platform. The worker-context design cannot
work there -- every non-DSA fallback binds through the shared
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

- `src/erhe/window/erhe_window/window_configuration.hpp:38-43` -- the
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
  `window_config.py:60-79` (`gl_major` `"4"`, `gl_minor` `"6"`),
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
`force_gl_version` (`config/editor/erhe_graphics.json:20`) is a single switch
that turns off DSA, clip control, compute, SSBO, debug output, texture views
and MDI at once -- a strictly bigger lever into the 4.1 path than
`force_no_direct_state_access`, which this plan does delete. (The value is
currently `0` in all four config files, so the switch is reachable but not
engaged.) Phase 0 must not remove the emulation while leaving the switch that
requests it.

- `force_gl_version` and `force_glsl_version` are deleted

Then:

- `gl_device.cpp:400` -- replace the `use_direct_state_access` probe with the
  hard version check described above.
- Delete the `else` arm of every DSA branch. **The site list below is an
  entry-point list, not a branch list -- do not work it mechanically.** It is
  a grep for the *identifier* `use_direct_state_access`, and in five of the
  eight files that identifier appears only in a local
  `const bool use_dsa = ...` declaration. The branches are on `use_dsa`, and
  there are far more of them:

  | file | entry points | what they are | real `use_dsa` branches |
  |---|---|---|---|
  | `gl_render_pass.cpp` | :24, :166, :308, :528, :663, :849 | 6 declarations | **30** |
  | `gl_texture.cpp` | :710, :1047 | 2 declarations | **8** |
  | `gl_blit_command_encoder.cpp` | :38, :468, :637, :729, :841 | declarations | ~9 |
  | `gl_vertex_input_state.cpp` | :393 | declaration | ~3 |

  `gl_texture.cpp:710` is literally
  `const bool use_dsa = device.get_info().use_direct_state_access;` -- it has
  no `else` arm to delete. Working the checklist mechanically deletes a
  declaration and leaves its branches dangling on an undeclared name.
  **Re-derive the branch list from `use_dsa` at implementation time**; treat
  the entry points as the set of files to visit and nothing more. (The
  identifier occurs 49 times in the tree; the entry-point list below is 43 of
  them.)

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
  (The declaration at `device.hpp:188` and the log at `gl_device.cpp:402` are
  deliberately not in that list.)
- `Vertex_input_state_tracker::m_use_dsa` is read at
  `gl_state_tracker.cpp:526` and `:546`. Those two branches go with the
  setter (`gl_state_tracker.hpp:103`), the member (`:113`) and the call
  (`gl_device.cpp:580`).
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
  `gl_device.cpp:742`'s comment sits on the
  `gl_helpers::set_error_checking(false)` call at `:745`, which is part of
  *this whole pre-4.3 arm*, not a standalone workaround. It goes with the
  block.
- Retire `force_no_direct_state_access` **through the codegen's versioned
  retirement path, not by deletion**. `Opengl_config` is `version=1,
  reflect=True` (`src/erhe/graphics/definitions/opengl_config.py:3-5`), and the generator supports field
  retirement via `removed_in` plus a struct version bump
  (`erhe_codegen/schema.py:282-293`, `emit_cpp.py:216-217`,
  `emit_hpp.py:73-74`, `:260`), with in-tree precedent at
  `src/editor/config/definitions/content_edge_lines_config.py:25`. So:
  `removed_in=2` on the field, `version=2` on the struct. Deleting the field
  outright silently drops the versioned-deserialization path for user-written
  `erhe_graphics.json` files that still carry the key. Then drop the key from
  **all four** config files that carry it:
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
- **Delete** `force_no_compute_shader`. Compute shaders and SSBO buffers are now required.
- **Delete** `force_no_clip_control` - clip control is now required.
- **Pre-4.5 capability tests that are not `use_dsa` branches**, so the
  inventory above does not reach them. Each becomes constant under a 4.5
  requirement:
  - `gl_buffer.cpp:239` and `:248` -- `const bool in_core = gl_version >= 440;
    ... ERHE_VERIFY(in_core || has_extension)`. Constant-true.
  - `gl_device.cpp:244` -- `if (gl_version >= 430)` guarding
    `max_framebuffer_samples`. Constant-true.
  - `gl_shader_stages_prototype.cpp:654` -- `gl_version >= 430` gating
    `dump_reflection()`. 3d lists `:601` and `:617` from this file, not this.
  - `gl_texture.cpp:965` -- `gl_version >= 430` choosing
    `tex_storage_2d_multisample` over `tex_image_2d_multisample`. It sits
    inside the non-DSA arm at `:950`, so a correctly-executed 3c takes it --
    but only once 3c is understood as "delete the `else` arms of the
    `use_dsa` branches".
  - `shader_stages_create_info.cpp:286`+ (the `else` polyfill arm) -- the
    `GL_ARB_shading_language_packing` polyfill (`unpackSnorm2x16`,
    `unpackUnorm2x16` and `unpackUnorm4x8` written out in GLSL), whose comment at `:289` names
    *"macOS OpenGL 4.1 where these are missing despite being GLSL 4.00
    core"*. **Caveat: it is gated on extension availability, not on version**,
    so verify before deleting rather than assuming 4.5 implies the extension
    string.

### 3d. Capability gates that become unreachable -- decide separately

Requiring GL 4.5 makes a long list of gates constant-true. They are listed
here because they were motivated by macOS GL 4.1, but each is a real behavior
deletion and **none is required by the worker-context work**. Do them in their
own commit(s), after phase 0 lands and builds. This list is longer than it
first appears -- treat it as a survey to re-derive, not as complete. When
re-deriving it, **confirm each cited line is a read of the flag, not a
sentence about it**; several sit next to explanatory comments.

- `use_solid_wireframe = (gl_version > 410)` (`gl_device.cpp:397`;
  declaration `device.hpp:300`). Reads: `app_rendering.cpp:205` and
  `brush_preview.cpp:43`. `viewport_scene_view.cpp` contains no read.
- `use_texture_view` (`gl_device.cpp:387`, GL 4.3) -- consumer fallbacks at
  `src/editor/graphics/thumbnails.cpp:71` and `:122`.
- `use_clear_texture` (`:384`, 4.4), `use_base_instance` (`:454`, 4.2),
  `use_debug_output` / `use_debug_groups` (`:312-313`, 4.3),
  `use_multi_draw_indirect_core` (`:432`).
- `use_clip_control` (4.5 core) -- the functional sites are `:1057`
  (`gl::clip_control`) and `:1065` (the `native_depth_range` ternary); the
  reverse-Z warning block `:1069-1082` becomes dead with them.
- `gl_device.cpp:1050-1055` -- `primitive_restart_fixed_index` vs.
  `primitive_restart` + `primitive_restart_index`.
- **GLSL-version emulation**, which the worker-context work never touches but
  which is part of the same layer: `shader_resource.cpp:656`, `:803`, `:815`,
  `:836`, `:870` (`layout(binding=)` and std430 emulation for
  `glsl_version < 420 / 430`), `gl_shader_stages_prototype.cpp:601`, `:617`,
  `glyph_buffer.cpp:66`, and `shader_stages_create_info.cpp:271`
  (`use_shader_storage_buffers && gl_version < 430` -- dead on both halves
  once 4.5 is required).
- **Compute fallbacks outside the id-renderer**: `sky_renderer.cpp:92-96` +
  `sky_renderer.hpp:43`, and `imgui_renderer.cpp:166`, `:186` (SSBO to UBO).
  Debug renderer: declaration `debug_renderer.cpp:29`, reads at `:159`,
  `:384`, `:420`, `:541`, plus `debug_renderer_bucket.cpp:47` and `:152`.
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
  it. `:64` repeats the stale "the editor's `force_no_direct_state_access`"
  claim, and `:97` documents the `__APPLE__` cube-map upload branch that 3b
  deletes. Update or strike; the variable is gone.
- `scripts/run_circular_ring_buffer_smoke_test.py:38` and `.gitignore:59-60`
  reference `build_xcode_opengl`.
- **A codegen definition, which therefore ships into the generated config
  UI**: `src/editor/config/definitions/preview_edge_lines_config.py:8`, whose
  `long_desc` justifies the field by "macOS OpenGL 4.1". This one is
  user-visible, not just a comment, and it needs the double-build treatment
  that every codegen edit needs.
- **Docs still describing the GL 4.1 path as live**:
  `doc/shader_workarounds.md:33`, `doc/editor_rendering.md:131` and `:333`,
  `doc/mesh_component_selection.md:237`, `doc/esoterica_rendering.md:241`,
  `doc/debug_renderer_multiview.md:276`, `doc/async-asset-loading-plan.md:426`,
  `src/erhe/renderer/notes.md:37`.
- **In-source comments justifying behavior by "macOS GL 4.1"**:
  `src/erhe/scene_renderer/erhe_scene_renderer/program_interface.cpp:269`,
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

Note: the `DRAW_CAPABLE` guard sites placed here are transitional -- the
per-context caches (section 10, commits 10-11) relax most of them to
`HAS_CONTEXT`. They still land first: they convert the driver crash into a
named assert immediately, and the role machinery and `HAS_CONTEXT` survive
phase 7 unchanged.

New GL-backend-internal header, `erhe_graphics/gl/gl_thread_role.hpp`:

```cpp
enum class Gl_thread_role { none, main, worker };

[[nodiscard]] auto get_gl_thread_role() -> Gl_thread_role;   // thread_local
void set_gl_thread_role(Gl_thread_role role);
```

An **explicit per-thread role**, not a "is this the main thread" test. The
role test is harder to defeat by accident: a future non-executor thread that
makes a context current without going through the worker API gets `none` and
trips the first guard rather than silently passing an `is_main_thread()`
negation.

Two macros, both `ERHE_VERIFY`-backed (always on, Debug and Release):

- `ERHE_VERIFY_GL_THREAD_HAS_CONTEXT()` -- role must not be `none`. This one
  alone converts the section 1 access violation into a legible assert at the
  call site.
- `ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()` -- role must be `main`.

The role is set in exactly three places: `Device_impl`'s constructor and
`Device_impl::on_thread_enter()` set `main`; `Scoped_worker_context`'s
acquire sets `worker`, and its release restores the *previous* role -- not
unconditionally `none`, see the re-entrancy discussion in section 6.

There is deliberately **no separate role for container-object access**.
Per-object accessors (section 6) express that permission at the granularity
it actually varies; a role cannot say "this task may touch *this*
framebuffer".

### Guard placement

`ERHE_VERIFY_GL_THREAD_HAS_CONTEXT()`:
- every `Device_impl::create_*` for a **shared** object -- `create_buffer`,
  `create_texture`, `create_texture_view`, `create_renderbuffer`,
  `create_sampler`, `create_program`, `create_shader` -- in
  `gl_device.cpp:2070-2190`;
- `Buffer_impl::allocate_storage`.

The **container**-object creators are handled separately:
`create_vertex_array` (`gl_device.cpp:2153`) and `create_framebuffer`
(`:2111`) below; `create_query` (`:2167`) takes
`ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()`, because its only caller is
`Gpu_timer_impl::create()` (`gl_gpu_timer.cpp:87`), which section 6 makes
main-thread-only rather than per-context. Getting that split right matters
more than the guard macro does, because shared-versus-container is the axis
the whole design turns on: a worker-created query or framebuffer is as
unusable on the main context as a worker-created VAO.
(`create_renderbuffer`, `:2125`, *is* shared and stays in the list above.)

`ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()`:
- every `Gl_binding_state` mutator and query: `push_/pop_/bind_ buffer`,
  `texture`, `framebuffer`, `renderbuffer`, `vertex_array`, plus
  `bind_sampler`, `use_program`, and **all seven `on_*_deleted` hooks**;
- `OpenGL_state_tracker::execute_` (both overloads), `reset`,
  `on_thread_enter`, `on_thread_exit`;
- `Vertex_input_state_tracker::execute`, `set_index_buffer`,
  `set_vertex_buffer`;
- `Buffer_impl::map_bytes`, `map_all_bytes`, `unmap`, `flush_bytes` -- a
  worker must never map a buffer (but see the `allocate_storage` note below);
- the top of `Render_command_encoder_impl` and `Compute_command_encoder_impl`
  construction, and `Render_pass_impl` start/end.

Nothing is placed on the per-thread migration hooks of
`Vertex_input_state_impl`, `Render_pass_impl` or `Gpu_timer_impl`: section 6
**deletes** all six hooks, and `m_owner_thread` with them. The static
registries go too -- *except* `Gpu_timer_impl::s_all_gpu_timers` and its
mutex, which `end_frame` needs for polling; see section 6's exclusion note.

**Container objects reached through an accessor** --
`Device_impl::create_vertex_array` (`gl_device.cpp:2153`),
`create_framebuffer` (`:2111`), and the `create` / `reset` pairs on
`Vertex_input_state_impl` and `Render_pass_impl` -- take plain
`ERHE_VERIFY_GL_THREAD_HAS_CONTEXT()`. (`Gpu_timer_impl::create` / `reset`
take `DRAW_CAPABLE` instead, per its exclusion.) What makes them safe off the main
thread is not a role but the per-object accessor of section 6: they are
reached *through* `Scoped_vertex_input_state` / `Scoped_framebuffer`, which
guarantees the object being created belongs to the calling thread's own
context.

### Blit_command_encoder is guarded per method, not at construction

Guarding `Blit_command_encoder_impl` at construction would make every texture
upload main-thread-only as a side effect rather than as a decision. Split it:

- the texture upload / copy paths take `HAS_CONTEXT`. After phase 0 they
  touch only per-context pixel-unpack and pixel-store state
  (`gl_blit_command_encoder.cpp:307-311`, `:466`): the
  `Gl_binding_state::push_texture` scratch-unit guard at `:247-250` lives in
  the `#if defined(__APPLE__)` arm (`:224`) that phase 0 deletes, and the
  surviving arm emplaces `texture_guard` only `if (!use_dsa)` -- never, once
  DSA is mandatory;
- `blit_framebuffer` takes `HAS_CONTEXT` **and** requires a
  `Scoped_framebuffer` for each of its two render passes;
- the readback / pack paths (`:607-611`) keep `DRAW_CAPABLE`, since nothing
  needs them off the main thread today.

### allocate_storage calls map_bytes

`allocate_storage` is worker-legal and `map_bytes` is not, but
`gl_buffer.cpp:296-300` calls one from the other:

```cpp
const bool map_persistent = erhe::utility::test_bit_set(gl_storage_mask, gl::Buffer_storage_mask::map_persistent_bit);
if (map_persistent) {
    map_bytes(0, m_capacity_byte_count);
}
```

The repro path survives only by accident: `Mesh_memory`'s pools request
`device_local` with `preferred = none` (`mesh_memory.cpp:536-537`, `:618-619`),
so `map_persistent_bit` is never set. **Any** worker-allocated host-visible
buffer -- including one a future call site adds -- would assert inside
`allocate_storage`.

Resolve it explicitly rather than leaving it to luck: `allocate_storage`
asserts that a worker only ever allocates **non-persistent** buffers, which
turns the accidental invariant into a stated one. The alternative -- a
documented "except from within `allocate_storage`" carve-out on the
`map_bytes` guard -- is weaker, because it is a hole in the guard rather than
a narrowing of the permission.

### What the guards leave standing

After phase 0 there are no non-DSA fallbacks left to guard -- the emulation
that would have taken the binding path is gone rather than merely asserted
against. What remains is the by-construction consequence:

- `Gl_binding_state::on_*_deleted` under the draw-capable guard means any
  worker-side destruction of a shared GL object asserts. `Buffer_pool` blocks
  are never destroyed (capacity only grows), so this is expected to be
  unreachable -- but it must be **verified by running**, not assumed
  (section 12, item 4). If a real worker-side destruction turns up, the fix
  is a main-thread deferred-delete queue drained in
  `Device_impl::wait_frame()`, not a relaxed guard.

Phase 1 is independently valuable and independently committable: on its own it
turns the driver crash into a named assert, with no behavior change on any
configuration that passes today.

## 5. Phase 2 -- cross-context publication

**This is a first-class part of the design, not a detail of the API.**

GL share contexts do **not** automatically synchronize. The GL spec's
"Shared Objects and Multiple Contexts" chapter (transcribed in
`doc/gl-spec-section-5.md`) conditions cross-context visibility on the
producing context's changes having **completed** (rules 2 and 4, section
5.3.3) -- and 5.3.1 names exactly two ways to establish completion: `Finish`,
or `FenceSync` in the producing context plus a `WaitSync` in the consuming
context. The word *flush* appears nowhere in the chapter; "flush is enough"
is WGL/GLX implementation folklore, not a spec guarantee. The
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

The worker may hold its context for a long time -- section 9 leaves the scope
width open, and the widest option holds one context for a whole finalize
task. Between the enqueue and the
release there is no flush of any kind: the GL backend has `gl::fence_sync`
only on the main-context per-frame path (`gl_device.cpp:1640`) and
`gl::finish()` at `:1690` / `:2014`.

This looks like it works on Windows and Linux desktop, because WGL and GLX
implicitly flush on `MakeCurrent(NULL)` -- which covers release-then-use and
not the ordering above. Symptoms when it does bite are driver-dependent:
`GL_INVALID_OPERATION` on the copy, a zero-filled or garbage mesh, or an
access violation.

**Decision: fence-based publication, at publication granularity, not at
context release.** A worker-created GL object must be published -- fence
issued and flushed, on the worker -- before its name escapes into anything
the main thread can read, and the main thread must wait on that fence before
its first use of the object.

**A fence is required; a flush alone has no spec backing.** The spec's rules
2 and 4 (5.3.3)
condition visibility on *completion* as defined in 5.3.1, and 5.3.1
explicitly recommends the `FenceSync` / `WaitSync` pair for exactly this
cross-context case. Sync objects are on the chapter's shared-object list,
which is what makes the mechanism implementable at all: a fence created on
the worker context can be waited on from the main context. The CPU-side
happens-before that hands the *name* across threads is already supplied by
`Buffer_transfer_queue`'s `std::mutex` (`buffer_transfer_queue.cpp:25`); the
fence supplies the GL-side ordering that the mutex cannot.

### The publication mechanism

- **Producer (worker), at each publication point:**
  `gl::fence_sync(sync_gpu_commands_complete, 0)` followed by `gl::flush()`.
  The flush is not replaced by the fence -- it is what submits the fence:
  `glWaitSync` does not flush the producing context, so an unflushed fence
  may never reach the GL server and the wait would be indefinite.
  Fence-then-flush is the standard pattern.
- **The sync travels with the object.** `Buffer_impl` and `Texture_impl`
  gain a publication-sync member (the `GLsync` plus a consumed flag;
  null for main-thread-created objects, which need no wait). It is written
  only at the publication points below, always by the creating worker,
  *before* the name escapes into any main-thread-readable structure. A later
  publication on the same worker context (texture storage, then upload) may
  replace an unconsumed earlier sync -- delete the old one -- because a
  fence covers every command issued before it on its context.
- **Consumer (main thread), before first use:** `wait_publication()` on the
  impl -- `glWaitSync(sync, 0, GL_TIMEOUT_IGNORED)` (a server-side wait, no
  CPU stall), then `glDeleteSync`, then mark consumed. Because a single
  context processes its commands in order (5.3.1 note 1), one wait orders
  *every subsequent* main-context command after the worker's publication --
  so the wait is once per object, at the first main-context touch, not per
  use.
- **Where the main thread first touches each object kind:** for buffers,
  `Device_impl::upload_to_buffer` (`gl_device.cpp:1425`), reached from
  `Mesh_memory::flush` -- the wait goes at its top. For textures (the
  precautionary path), the first main-context bind; the consumer obligation
  is stated with rule 4 below and exercised by section 12 item 9.
- **Cost:** one fence + flush per publication. Buffer publications are per
  pool block, and pool blocks are created rarely (a block is megabytes), so
  this is noise. If profiling ever shows otherwise, batch fences per
  publication *scope*, never wider.

The publication points:

- **Buffer storage**: fence-then-flush at the end of
  `Buffer_impl::allocate_storage` when the calling thread's role is `worker`,
  storing the sync on the impl.
  One publication per pool block, and pool blocks are created rarely -- a
  block is megabytes, and `Buffer_pool::create_new_block` runs only when a
  pool rolls over.
- **Texture storage**: the same, at the end of the `Texture_impl`
  constructor's storage branch (`gl_texture.cpp:662`, the
  `texture_storage_*` / `tex_storage_*` calls at `:925-981`).
- **Texture pixel upload**: the same, at the end of each upload **method** --
  `Blit_command_encoder_impl::copy_from_buffer`
  (`gl_blit_command_encoder.cpp:137`) and `copy_from_buffer_compressed`
  (`:426`). Section 4 makes worker-side upload legal, so this is a live case
  rather than a hypothetical.

  Publish **once per method call, not per `*_sub_image_*` call**. Those inner
  calls (`:329`, `:359`, `:376`, `:395`; compressed `:484`, `:509`, `:528`)
  sit inside cube-face and mip loops, so fencing at each one would create a
  sync per sub-image for no benefit -- publication is about the object
  becoming visible to another context, which happens once, when the call
  returns, and one fence at the end covers every command before it.

**Buffers need no second publication point**, because this plan forbids
workers from mapping buffers (section 4) and `init_data` is passed at storage
time -- so there is no worker-written buffer data that postdates creation.
`Texture_create_info` (`texture.hpp:17-44`) likewise carries no initial pixel
data, which is why texture upload is a separate point rather than part of
construction.

Do **not** put the fence only in the context release path, and do not record
the release contract as sufficient. The two call sites in section 9 both
publish before their scope ends
(`async_raytrace_kickoff_operation.cpp:162` enqueues to the commit queue
inside the per-item loop, with more items still to build under the same
context; `gltf_load_task.cpp:132-141` does its `finished.store(...,
release)` as the last statement *inside* the lambda, before the scoped
destructor runs). Publication-point fencing is the only placement that
covers them.

### The consumer-side half: rule 4's attach / re-attach

Completion is only half of what the spec demands. Rule 4
(`doc/gl-spec-section-5.md`, 5.3.3) also requires the consuming context to
**attach or re-attach** the changed object to a binding point (or to an
attachment point of a currently bound container object) before its new
contents are guaranteed visible. State where that attach actually happens
for each publication point, or the plan relies on it silently:

- **Vertex / index buffers: satisfied in the steady state.**
  `set_vertex_buffer` / `set_index_buffer` are re-issued per draw from
  `Render_command_encoder_impl` (`gl_render_command_encoder.cpp:118`, `:123`)
  -- a fresh attach on the main context, after the `wait_publication()`,
  which is exactly rule 4's requirement.
- **The DSA gap, accepted:** the *first* main-context touch of a
  worker-created buffer is `glCopyNamedBufferSubData` -- a DSA write with no
  bind at all. Chapter 5's rules are written entirely in terms of binding
  and attachment and simply do not address DSA access to an object changed
  in another context. The `WaitSync` ordering is what covers it in
  practice. This is a known gap between the spec's letter and the DSA-era
  reality; it is recorded here rather than claimed to be spec-covered.
- **Textures (precautionary path): a stated consumer rule.** A worker may
  only create-and-upload a texture that the main context has **not yet
  bound**; the main context's first bind, issued after `wait_publication()`,
  is then the rule 4 attach. If a worker ever needs to modify a texture the
  main context already has bound, rule 3 says the change is not guaranteed
  visible until re-bind -- the consumer must then re-bind after waiting,
  and the re-bind must reach real GL (route it so `Gl_binding_state`'s
  cache does not elide it as redundant). No such site exists today; do not
  add one without adding its re-bind.

Nothing creates or uploads a texture on a worker today
(`texture_file_loader.cpp:153` is decode-only), so the texture rules are
precautionary -- but the decode-then-upload path is the obvious first user,
and the rule should be in place before it lands rather than after it
misbehaves.

## 6. Phase 3 -- the worker context API and per-object access

The call sites (`deferred_finalize_mesh_items`,
`build_imported_buffer_meshes`, the lightmap partitioner) are **backend-shared
code**, so the API must exist on every backend and be a no-op on the ones that
do not need it.

### One context API, plus explicit per-object access

There is **one** worker-context API. `Scoped_worker_context` grants a worker a
share context and the right to create and operate on **shared** objects
(buffers, textures, samplers) via DSA. Access to a **container** object is a
second, separate, explicit step, granted per *object*.

**Alternative considered and rejected: tiering the context** -- a `limited`
context that may create buffers and a `full` one that may also create VAOs.
It fails because it encodes container-object access in the *context type*,
while which container objects a task needs is a property of the **work**: a
tier cannot express "this task needs *this* framebuffer". It also draws the
boundary in the wrong place, since a context tier has no way to distinguish
per-context state (legal) from shared state (not).

### The container-object problem, and the two places it already exists

GL share contexts share *object* state -- buffers, textures, samplers,
programs -- but **container objects are not shared**: vertex array objects,
framebuffers and transform-feedback objects are per-context. A VAO created on
a worker context is not the same object on the drawing thread.

The tree already has this problem **three** times, in structurally identical
form:

| | `Vertex_input_state_impl` | `Render_pass_impl` | `Gpu_timer_impl` |
|---|---|---|---|
| GL object | `std::optional<Gl_vertex_array>` | `std::optional<Gl_framebuffer>` (x2) | `std::optional<Gl_query>` |
| owner | `std::thread::id m_owner_thread` | `std::thread::id m_owner_thread` | `std::thread::id m_owner_thread` |
| registry | `static s_all_vertex_input_states` | `static s_all_framebuffers` | `static s_all_gpu_timers` |
| migration | `on_thread_enter` / `on_thread_exit`, whole-set | same | same |

(`gl_vertex_input_state.hpp:29-53`, `gl_render_pass.hpp:19-77`,
`gl_render_pass.cpp:249-276`, `gl_gpu_timer.hpp:27-75`,
`gl_gpu_timer.cpp:31-57`.) All three are dispatched from the same place:
`OpenGL_state_tracker::on_thread_enter` / `on_thread_exit`
(`gl_state_tracker.cpp:734-736`, `:741-743`). So there are **six** thread
hooks across three types, not four across two -- and all six go, though not
all three types go the same way (below).

The GL backend is not the only place these are declared: `metal` and `null`
carry no-op `on_thread_enter` / `on_thread_exit` stubs for
`Vertex_input_state_impl` and `Render_pass_impl` too
(`metal_vertex_input_state.cpp:27-28`, `metal_render_pass.cpp:37-38`,
`null_vertex_input_state.cpp:63`/`:68`, `null_render_pass.cpp:31`/`:36`, plus
their headers). They are reachable only from GL's dispatcher, so they become
dead code -- take them in the same commit, or say in the commit message that
they are deliberately left.

Query objects are per-context in GL, like VAOs and FBOs -- though for a
different spec reason: chapter 5's container-object list is framebuffer,
program pipeline, transform feedback and vertex array objects, and queries
are not on it; they are simply absent from the shared-object list too
(buffers, program/shader, renderbuffer, sampler, sync, texture objects), so
they are unshared without being containers. The consequence is the same. But
`Gpu_timer_impl` is **deliberately excluded from the per-object accessor
mechanism**, and here is the reason, so it is not silently skipped:

- Its per-context state is not a single GL name. It owns a **ring** of four
  `Query { std::optional<Gl_query> query_object; bool pending; }`
  (`gl_gpu_timer.hpp:54-59`) plus `m_last_result` (`:70`). The
  `std::atomic<unsigned int>` slot below cannot represent that, so it would
  need a third accessor and a different slot type for no benefit.
- Nothing wants it. A GPU timer times a render pass on the drawing thread;
  workers do not render. Timing worker-side GL work is not a goal of this
  plan.

**So instead of converting it, delete its migration machinery and enforce
main-thread-only:** drop the two thread hooks (`gl_gpu_timer.cpp:31`, `:44` --
whole-set sweeps identical to the other two types) and assert `main` in
`create()`, `write_begin_timestamp` and `write_end_timestamp`.

`m_owner_thread` goes with them, and **every use has to go in the same edit or
the build breaks** -- the writes in `create()` (`:91`) and `reset()` (`:98`),
and the filters in `write_begin_timestamp` (`:112`), `write_end_timestamp`
(`:138`), `poll()` (`:157`) and `end_frame` (`:196`). Each filter becomes
unconditional, which is safe once creation is main-thread-only: every
registered timer was created on the drawing thread, and `poll()` re-checks
`pending` and `has_value()` per slot anyway.

**Keep** `s_all_gpu_timers` and `s_mutex`: `end_frame` (`:189`) legitimately
walks the registry to poll results, and that is not migration.

That still removes the hooks `OpenGL_state_tracker::on_thread_enter` /
`on_thread_exit` dispatch to (`gl_state_tracker.cpp:736`, `:743`), so no type
is left on `m_owner_thread` migration while the dispatcher still calls it --
which is the failure this section exists to prevent.

That three independent types converged on the same shape is the argument for
solving it once, in a shared mechanism, rather than three times.

### The API

```cpp
namespace erhe::graphics {

class Worker_context_token { public: int id{-1}; };   // id < 0 is the empty token

// Grants the calling worker thread a share context: create and operate on
// SHARED objects (buffers, textures, samplers) via DSA. Does NOT by itself
// grant access to any container object -- take a Scoped_vertex_input_state
// or Scoped_framebuffer for that. No-op on the main thread and on every
// backend that needs no per-thread context (Vulkan, Metal, null).
// RE-ENTRANT: nested construction on one thread refcounts.
class Scoped_worker_context final
{
public:
    explicit Scoped_worker_context(Device& device);
    ~Scoped_worker_context() noexcept;
    // non-copyable, non-movable

private:
    Device&              m_device;
    Worker_context_token m_token;
};

// Ensures this Vertex_input_state has a VAO on the CALLING THREAD'S CURRENT
// CONTEXT, and yields its name. Asserts if no context is current (role
// `none`). Cheap and idempotent after the first use on a given context.
class Scoped_vertex_input_state final
{
public:
    // NOTE the const reference. Every adoption site reaches the state only as
    // `const Vertex_input_state*` (render_pipeline.hpp:55,
    // render_pipeline_state.hpp:26), so a non-const parameter cannot be formed
    // at any of them. This is well-formed because the per-context slots are
    // std::atomic<unsigned int>, whose load() and store() are const-qualified:
    // adoption mutates only the slot, never the logical object.
    //
    // Consequently, on the *_impl side: the slot array and the per-object
    // adoption mutex are `mutable`, the adoption entry point is const-
    // qualified, and adoption is reached through the const overload
    // Vertex_input_state::get_impl() const (vertex_input_state.cpp:103-106).
    // Same for Render_pass -- where it also forces
    // process_attachment / process_multisample_resolve_attachment
    // (gl_render_pass.cpp:320-323, :368-371) to take their descriptor by
    // const ref; they only read it today.
    // Miss this and the first build fails.
    Scoped_vertex_input_state(Device& device, const Vertex_input_state& state);
    ~Scoped_vertex_input_state() noexcept;
    [[nodiscard]] auto gl_name() const -> unsigned int;
    // non-copyable, non-movable
};

// The same, for a Render_pass's framebuffer(s). Const for the same reason
// Scoped_vertex_input_state is: blit_framebuffer takes const Render_pass&
// (blit_command_encoder.hpp:32), so a non-const parameter cannot be formed
// at one of this type's two adoption points.
class Scoped_framebuffer final
{
public:
    Scoped_framebuffer(Device& device, const Render_pass& render_pass);
    ~Scoped_framebuffer() noexcept;
    [[nodiscard]] auto gl_name                    () const -> unsigned int;
    [[nodiscard]] auto gl_multisample_resolve_name() const -> unsigned int;
};

}
```

`Worker_context_token::id` is `-1` when empty, deliberately **not** `0`: the
existing `Gl_worker_context` uses id 0 for both "main-thread no-op" and
"worker context #0" (`gl_context_provider.cpp:54-55` enqueues
`{i, context.get()}`; `:100-101` verifies `id == 0` *and*
`context == nullptr` for the no-op). The second check happens to catch a real
context #0, but the id is still overloaded for two meanings. The new API
should not inherit that.

Both accessors are **no-ops on every backend but GL**, where container
objects do not exist as a concept. On the main thread they are also
effectively free -- the object is already present on the main context.

### Per-context instances, not migration

**(a) Migration** -- one GL object, ownership moves between contexts. This is
what the tree does today. It requires mutual exclusion (the main thread must
not touch the object while a worker holds it), a hand-back on scope exit, and
a re-adoption point, and it fails on all three: there is no re-adoption point
(`on_thread_exit` is never called on the main thread -- `editor.cpp:2573` is
the only `on_thread_enter` call), the hooks are whole-set rather than
per-object, and the hand-back writes the shared binding cache from a worker.
Those are not incidental defects; they are what "one object, two threads"
costs.

**(b) Per-context instances -- chosen.** The logical object holds a small map
from *context* to GL name, and each context lazily creates its own on first
use. There is no ownership, no hand-back, no exclusion, and no migration: two
threads using the same `Vertex_input_state` concurrently each touch their own
VAO. The static registries and the `on_thread_enter` / `on_thread_exit`
sweeps are **deleted outright** from all three of `Vertex_input_state_impl`,
`Render_pass_impl` and `Gpu_timer_impl`, along with `m_owner_thread`. The
registries of the first two go with them; `Gpu_timer_impl` keeps its registry
for a non-migration reason given in its exclusion note below.

The cost is memory, and it is negligible: with a 4-context pool plus main,
that is 5 VAOs per vertex format -- nine formats, so 45 VAOs -- and 5
framebuffers per render pass.

What (b) requires that does not exist yet:

- **A context identity.** A small dense index assigned at context creation
  (main = 0, pool entries 1..N), stored thread-locally, so the per-object map
  is an array lookup rather than a hash on `thread::id`. Thread ids are the
  wrong key -- taskflow reuses threads across contexts.
- **A fixed-size slot array per object**, sized `1 + pool_size` at device
  creation and never grown, each slot a `std::atomic<unsigned int>` GL name
  (`0` = not yet created).

  **The hot path is lock-free; the enumeration paths are not.** Be precise
  about which is which, because the difference is where the bugs go:

  - *The populated fast path* -- what the accessor does on all but the first
    bind on a given context -- is a **relaxed atomic load of your own slot,
    with no lock**. Each context writes only its own slot and the array never
    reallocates, so nothing else can be observed mid-write. This is the hot
    path, and it stays lock-free.
  - *First-use adoption* -- the slot reads `0`, so the VAO has to be created
    -- **takes a per-object mutex.** This matters: the destruction path below
    must exclude concurrent adoption, and a mutex only excludes parties that
    take it. If adoption were lock-free, a worker could store a freshly
    created name into its slot just after the destruction walk passed that
    slot, and the object would never be queued for deletion -- a silent leak
    for the life of the process, caught only much later by section 12 item
    7's object-count check. Adoption is once per object per context, so the
    lock is off the hot path in practice without being absent in principle.
  - *Cross-slot enumeration* -- the destruction path, and section 12 item 7's
    check -- takes the same per-object mutex and reads every slot.

  **Memory ordering: relaxed on both sides is sufficient, and acquire/release
  would be cargo cult.** The GL name is the only datum published, and no
  context ever *uses* another context's container object, so there is no
  second value whose visibility needs ordering against it. The mutex, not the
  atomic ordering, is what makes adoption and enumeration mutually exclusive.

  Object *lifetime* is a separate contract and is not covered by any of the
  above: the accessor binds a reference to a live object, so destroying a
  `Vertex_input_state` while an accessor on it is in scope is a
  use-after-free like any other, and remains the caller's responsibility.
- **Deferred destruction**, below.

### Where adoption happens, on both the main thread and workers

Adoption must **not** be folded into `gl_name()`. Two durable reasons, and
one that expires:

- **`gl_name()` is a per-draw path.** `Vertex_input_state_tracker::execute`
  calls it on every pipeline bind (`gl_state_tracker.cpp:496-501`). Adoption
  belongs at pipeline-bind granularity, which is strictly coarser.
- **It would hide GPU object creation behind an accessor** that every caller
  reasonably reads as a getter. The accessor type makes the creation point
  explicit and greppable.
- *(Expires with commit 8.)* `s_mutex` is a plain `std::mutex`
  (`gl_vertex_input_state.hpp:52`) taken by both constructors (`:314`,
  `:324`) before `create()` (`:321`, `:332`) -> `update()` (`:382`) ->
  `gl_name()`, so a locking `gl_name()` would self-deadlock on construction
  today. Commit 8 deletes `s_mutex`, so do not lean on this one.

Note that **`const`-ness is not one of the reasons** -- the accessor is itself
`const`-friendly by design (above). Adoption is an explicit, named step; it is
not a non-`const` one.

**So name the adoption point, or "adopt main-thread-first" reintroduces
exactly what the paragraph above rejects.** The existing drawing-thread call
site *is* `Vertex_input_state_tracker::execute`, per draw and `const`.
Putting the accessor there would be the same mistake one level out.

The accessor is taken at **pipeline bind**, not at draw. Those sites are
non-`const`, run once per pipeline change rather than per draw, and are the
first point that knows which `Vertex_input_state` the pipeline uses. There
are **three** of them, not one -- missing the latter two breaks main-thread
rendering, so take all three:

- `Render_command_encoder_impl::set_render_pipeline`
  (`gl_render_command_encoder.cpp:43`);
- `Render_command_encoder_impl::set_render_pipeline_state` (`:60`) and its
  override-shader-stages overload (`:65`). Both route to
  `OpenGL_state_tracker::execute_` (`gl_state_tracker.cpp:762`), which
  reaches `vertex_input.execute(pipeline.data.vertex_input)` at `:769`.
  Live callers: `debug_renderer_bucket.cpp:517`,
  `content_wide_line_compute_renderer.cpp:493`,
  `content_wide_line_geometry_renderer.cpp:312`.

Each takes the `Scoped_vertex_input_state` and hands the resolved name down;
the tracker's `execute` keeps reading a name and stays `const`.
**`Scoped_framebuffer` adopts in `Render_pass::start_render_pass`
(`render_pass.cpp:233`), not in `Render_pass_impl::start_render_pass`.** The
impl has no back-pointer to its owner -- its members are `Device&`,
`Swapchain*`, the two `Gl_framebuffer`s and the attachment descriptors
(`gl_render_pass.hpp:53-75`) -- so there is no `Render_pass` to pass to the
accessor from inside it. The backend-neutral wrapper has `*this`, runs once
per pass, and already calls through to the impl at `render_pass.cpp:246`.

`end_render_pass` needs no accessor of its own. It reads the per-context name
(`gl_render_pass.cpp:872`) after the start side has already adopted on the
same context, so the slot is populated. That is the same "an accessor is the
adoption point, a getter is not" rule seen from the other end -- say it, or a
reviewer reads it as a missed site.

Combined with the fixed-size slot array above, the steady-state cost of an
accessor is a thread-local index read and one relaxed atomic load -- no lock,
no branch into the driver.

### The default vertex input state needs different handling

`Vertex_input_state_tracker::execute` substitutes
`Device_impl::get_default_vertex_input_state()` when a pipeline declares no
vertex input (`gl_state_tracker.cpp:496-499` -> `gl_device.cpp:2051`), so that
`glDraw*` does not hit VAO 0. Two problems, both of which the accessor design
alone does not solve:

- **The substitution happens inside the `const` per-draw path**, *after* the
  pipeline-bind sites above have already run with
  `ci.vertex_input == nullptr`. There is nothing for an accessor at those
  sites to adopt.
- **It is created lazily on first draw** (`gl_device.cpp:2057-2058`,
  `make_unique<Vertex_input_state>` inside the getter), and the comment there
  says it is owned at that spot precisely "so the per-thread VAO migration
  manages it" -- the machinery this section deletes.

**Fix: make the default state per-context and create each context's instance
as the last step of creating that context.** It is a single device-owned
object that lives for the process and that every context needs, so there is
no reason to create it lazily. The `execute` substitution then reads an
already-populated own-slot entry on the `const` path -- well-formed, because
`std::atomic::load()` is const-qualified -- and no adoption is required for it
at all.

Three things this requires that are easy to get wrong:

- **The main context's instance is created in the body of `Device::Device`
  (`device.cpp:49-53`), not in `Device_impl`'s constructor.** `m_impl` is a
  *member initializer* (`device.cpp:42`/`:44`), so while `Device_impl`'s
  constructor body runs, `Device::m_impl` is still null and
  `Device::get_impl()` (`device.cpp:253-256`, `return *m_impl.get();`) is a
  null dereference. `Vertex_input_state_impl::create()` goes through
  `get_impl()`, so "create it at device init" is unimplementable at the
  obvious spot. The `Device::Device` body runs after the member-init list and
  is the place.
- **Do not hang it off `Device::on_thread_enter()`.** `editor.cpp:2573` is its
  only caller in the whole tree -- hextiles, hello_swap, the example and the
  graphics tests never call it -- so anything created there is editor-only.
- **Each pool context's instance is created while that context is current**,
  as the final step of creating it, before it is put in the pool. That is
  free: the context is already current at that moment. Creating them later,
  in a batch, would mean make-currenting each context and overriding the
  thread-local context index from the main thread -- possible, but needless,
  and it collides with the share-context creation sequence above.

**And it constrains construction order, which the pool must respect.** The
pool cannot be created inside `Device_impl`'s constructor, which is where the
current `Gl_context_provider` member lives (`gl_device.hpp:171`, constructed
at `gl_device.cpp:118`). If it were, each pool context's default-state
instance would be created *before* the main context's -- i.e. before
`Device::Device`'s body -- and would hit exactly the null-`m_impl` fault
above. **Create the pool in `Device::Device`'s body (or a post-construction
init), after the main instance exists.**

Note the distinction between *declared* and *created*: the pool stays a
`Device_impl` member, where `m_gl_context_provider` is today
(`gl_device.hpp:171`) -- only its *population* moves to `Device::Device`'s
body. Making it a `Device` member instead would place it after `m_impl` in
declaration order, so the contexts would be destroyed **before** the
per-context VAOs and FBOs that live in `Device_impl` -- inverting the very
destruction-order constraint this paragraph exists to protect.

**And it constrains the slot array:** the array is sized from the
*configured* context count -- the fixed constant, 4 -- not from the number of
contexts actually created. Section 8 allows creation to fail entirely
(headless / null window yields zero worker contexts), and the main instance is
constructed before any pool context exists, so sizing from "contexts created
so far" is wrong in both directions. The constant must be known before the
first per-context object is constructed, which is what makes a fixed pool
load-bearing: the pool size cannot become dynamic without revisiting this.

**One backend note:** `Device::Device` is backend-neutral
(`device.cpp:34-53`), so the eager creation hangs off a per-backend hook that
is a no-op on Vulkan, Metal and null.

**And preserve one existing constraint** the rework could silently drop:
`gl_device.hpp:180` declares `m_default_vertex_input_state` *after*
`m_gl_context_provider` specifically so it is destroyed while the GL context
is still current. Per-context instances must keep that ordering property.
Two comments go stale with this change and should be updated in the same
commit: `gl_device.cpp:2052-2055` and `:582-584`, both of which describe the
lazy creation being removed.

This affects every VAO-less draw -- the id renderer, post-processing, the sky
renderer -- so it is not an edge case.

One latent API gap to note rather than solve: `Vertex_input_state::set()`
(`vertex_input_state.cpp:92-95`) reconfigures a state in place, which under
per-context instances would have to re-run `update()` on **every** context's
VAO -- something one thread cannot do. It has **no caller in the tree**, so it
is dormant. Either delete it, or give it an explicit invalidation rule: clear
every slot under the adoption mutex, and each context re-adopts on next use.
Do not leave it silently broken.

Two things that are **not** obstacles, checked so they are not re-opened:
`Vertex_input_state_tracker::m_last_state` caching is fine (the early return
at `gl_state_tracker.cpp:507-509` happens *after* `bind_vertex_array(name)`,
and the cached vectors are tied to the instance, not the name); and a
per-context VAO does not need recorded buffer bindings to carry across
contexts, because `set_vertex_buffer` / `set_index_buffer` are re-issued per
draw from `Render_command_encoder_impl` (`gl_render_command_encoder.cpp:118`,
`:123`) and `update()` re-establishes only attribute formats, enables and
divisors.

### Destruction is the remaining hard problem -- do not gloss it

A per-context GL object must be deleted **on its own context**. When a
`Vertex_input_state` or `Render_pass` is destroyed on the main thread while
worker contexts still hold instances of it, those instances cannot be deleted
from the destroying thread. Options, to be decided when implementing:

1. **Per-context deferred-delete queue**, drained at the top of the next
   `acquire` for that context (and at context teardown). Bounded, simple,
   and the deletion happens on the right context by construction.
2. **Leak until context destruction.** The pool is fixed at 4 and the
   contexts live for the process, so the bound is
   *objects-destroyed x contexts*. For VAOs and FBOs that is small, but it
   is unbounded in principle if a workload churns render passes.

(1) is preferred; (2) is acceptable only with a written bound. This is the
mirror image of the `on_*_deleted` problem in section 4: worker-side
destruction of GL objects must be routed, not merely forbidden.

### One shared member that per-context adoption would race on

`Render_pass_impl::create()` rebuilds `m_draw_buffers`
(`gl_render_pass.cpp:290-292`, `:415`, `:427`) -- a **shared**
`std::vector<gl::Color_buffer>` (`gl_render_pass.hpp:58`) also read at `:437`,
`:440`, `:697`, `:702`, `:927-939`. Under per-context instances, `create()`
runs once per context, so two contexts adopting the same `Render_pass`
concurrently would clear and refill that vector under each other.

The contents do not actually depend on the context: the swapchain arm depends
on `m_swapchain`, and the attachment arm is driven by `m_color_attachments` /
`m_depth_attachment` / `m_stencil_attachment`, all set once in the constructor
init list (`gl_render_pass.cpp:199-203`) and written nowhere else. Every
context computes the same answer. **Hoist the computation out of `create()`
into the constructor**, where it runs once. That is better than serializing it
under the adoption mutex, because it removes the shared write entirely rather
than protecting it.

**Pair the hoist with deleting the clear in `reset()` (`:281`)**, or the bug
comes straight back from the other side: `reset()` becomes per-context under
commit 8, so one context tearing down its own FBO would wipe the now-shared,
constructor-computed vector for every other context and for the main thread.

Audit the rest of `Render_pass_impl` the same way when implementing: any
member that `create()` writes and that is not the GL object itself is either
context-independent (hoist it) or genuinely per-context (move it into the
slot).

The same argument applies to `~Gl_vertex_array` / `~Gl_framebuffer` calling
`Gl_binding_state::on_*_deleted` (`gl_objects.cpp:315-323`): a worker-context
instance must carry a **null `binding_state`**, both because the destructor
would otherwise write the shared software cache from a worker, and because
container-object names are per-context so the scrub would be wrong anyway.
The destructor already guards on null and the move constructor already
supports it (`gl_objects.cpp:325-329`), so this is a constructor argument,
not new machinery.

### Does anything create a container object on a worker today?

**No** -- which sets what the initial commits can verify. `Mesh_memory`'s
constructor pre-registers an entry for every format up front on the main
thread (`mesh_memory.cpp:404-413`, the call at `:412`, looping
`get_all_vertex_formats()` -- all nine member formats, `:416-429`). Every
caller passes one of those members (`mesh_memory.cpp:800`, `:804`, `:809`,
`:826`, `:830`, `:835`; `lightmap_baker.cpp:1417`, `:3268`), and the lookup at
`:726-731` compares by value, so it always hits and the `emplace_back` at
`:734` is unreachable from a worker.

That is not a problem for the accessors, because they are not a speculative
permission: they are the mechanism the *existing* main-thread code should use
too, so they are exercised on every frame by the drawing thread. Adopting
them main-thread-first is what makes them testable before any worker call
site exists.

**While asserting that invariant, fix the other half of it.**
`get_vertex_input_from_vertex_format` is **not thread-safe on its read path**:
it walks the `m_vertex_input_entries` vector from workers with nothing
preventing a main-thread `emplace_back`. Nothing appends after construction
today -- exactly the unenforced coupling the section 9 hoist exists to
remove. Assert both halves: the miss path is only ever taken on the main
thread, **and** the vector is never grown after construction.

### The old provider is deleted -- do not reproduce its acquire bug

The new API **replaces** `Gl_context_provider`. `Gl_context_provider`,
`Gl_worker_context`, `Scoped_gl_context`, `acquire_gl_context` /
`release_gl_context` and `provide_worker_contexts` are all deleted; the pool
lives in `Device_impl` behind the API above.

The deletion is file-scoped:
`erhe_graphics/gl/gl_context_provider.cpp` and `.hpp` go entirely, along with
their two lines in `src/erhe/graphics/CMakeLists.txt:130-131`. The other
referents:

- `gl_device.hpp` / `gl_device.cpp` -- the `m_gl_context_provider` member and
  its uses, which the new pool takes over;
- `src/editor/transform/trs_tool.cpp` -- inside the `#if 0` block that
  section 7 notes, so no separate handling;
- **`editor.cpp:1450`** (`Scoped_gl_context ctx{m_graphics_device->context_provider}`
  inside `ERHE_GET_GL_CONTEXT`) and **`editor.cpp:1616`**
  (`provide_worker_contexts`). Both sit in dead `ERHE_PARALLEL_INIT` blocks.

That last pair is a **commit-order dependency, not a non-issue**: the
provider can only be deleted after section 7's blocks are gone. See the
commit split.

The analysis below is a **specification for the replacement**, not a
description of code to repair. `Gl_context_provider::acquire_gl_context` does
not block (`gl_context_provider.cpp:78-83`):

```cpp
while (!m_worker_context_pool.try_dequeue(context)) {
    std::unique_lock<...> lock(m_mutex);
    m_condition_variable.wait(lock, []{ return true; });   // predicate always true
}
```

The always-true predicate makes `wait` return immediately, so this is a
**busy spin**, not a wait. The moment the pool can be exhausted it becomes a
livelock burning every core -- and one that a profiler shows as *busy*, not
idle, so it does not look like a deadlock.

**A real predicate is not the fix -- it converts the spin into a lost-wakeup
deadlock.** Two facts make the obvious repair wrong:

- `m_worker_context_pool` is a `moodycamel::ConcurrentQueue<Gl_worker_context>`
  (`gl_context_provider.hpp:54`). It has no exact `empty()`, only
  `size_approx()`. There is nothing to write a correct predicate against.
- The producer enqueues and calls `notify_one()` **outside** `m_mutex`
  (`gl_context_provider.cpp:112-113`), and the consumer's `try_dequeue`
  (`:78`) is outside it too. A waiter that fails `try_dequeue` and is then
  preempted before `wait()` misses the notify and sleeps forever. The
  always-true predicate is precisely what hides this.

**So the replacement pool uses a `std::counting_semaphore`** (or a plain
`std::deque` under a mutex -- the lock-free queue buys nothing for a 4-entry
pool acquired around a globally serialized critical section). A half-fix here
is the same invisible-to-a-profiler hang, except it then looks *idle* rather
than busy.

### Re-entrancy and taskflow subflows

`Lightmap_partitioner` runs one region task per region and fans per-piece
work into `subflow->emplace(...)` followed by `subflow->join()`
(`lightmap_partitioner.cpp:262-275`); the GPU allocation is in
`process_piece` (`make_renderable_mesh`, `:194`), **not** in
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

So the API must be re-entrant, and acquisition must save and restore:

- a thread-local depth counter; nested construction refcounts and returns
  the same context rather than acquiring another;
- acquire saves the previously-current context and release restores it,
  rather than unconditionally clearing;
- the release path restores the previous *role*, not unconditionally `none`.

**What that machinery defends against -- state it, or the reviewer of the
worker-context commit cannot tell.** With the scope in `process_piece` (this
plan's choice), the region task parks in `join()` holding *no* context, so
the co-run-steals-the-context scenario cannot arise from this call site. The
save/restore design is a **by-construction guard against future call sites**,
not a fix for the lightmap case -- and section 9 shows how quickly new worker
call sites appear.

The part of this case that *is* load-bearing: `process_piece` also runs on
the **main thread** via the serial path -- `lightmap_partitioner.cpp:520`
calls `process_region`, which reaches `process_piece` through its inline
fallback at `:278` -- so the "no-op on the main thread" behaviour is
exercised in production, not just in theory. The serial-versus-parallel
decision is at `:515`, inside `request_prepare`.

### Pool sizing: small and fixed, created eagerly on the main thread

Do **not** size the pool at `num_workers`. The GL work behind these contexts
is globally serialized anyway: `erhe::primitive::buffer_mesh_allocation_mutex()`
is taken around every buffer-mesh allocation transaction --
`buffer_mesh.cpp:35` and `:46`, `mesh_optimizer.cpp:635`,
`primitive.cpp:954`, `primitive_builder.cpp:61`, and
`mesh_memory.cpp:904` -- so at most one thread is ever inside buffer
creation. `num_workers` contexts on
a 32-thread box means 32 hidden SDL windows and 32 driver-side context
states created for a mutually-exclusive fraction of the work.

Use a small fixed pool (start at 4) with the blocking wait from above. That
gives the same throughput at a fraction of the startup and driver cost, and it
surfaces the re-entrancy problem immediately instead of only on high-core
machines.

**With one caveat that section 9 raises**: a pool of 4 gives the same
throughput only while the scope stays *narrow*, around the allocation. If the
scope is hoisted above the per-mesh loop to avoid make-current churn, it also
spans the BVH build and geometry conversion, and 4 contexts then cap the whole
deferred finalize at 4 concurrent meshes. Pool size and scope width are one
decision, not two -- see section 9 item 1.

**Create the contexts eagerly, on the main thread, at startup.** Lazy
creation is not implementable:

- `Context_window::open()` calls `configuration.share->make_current()` at
  `sdl_window.cpp:718` *before* setting
  `SDL_GL_SHARE_WITH_CURRENT_CONTEXT` at `:719`. Creating a share context
  therefore **steals the main context away from whichever thread currently
  holds it**. Doing that from a worker while the main thread is mid-frame is
  immediate corruption, not a portability caveat.
- `open()` also calls `SDL_CreateWindow` (`:725`), mutates the global
  `s_window_count` non-atomically (`:871`, `:908`), and registers an SDL
  event watch (`:877`). SDL window creation is main-thread-only by contract
  on Windows and macOS.

First acquire happens on a worker, so lazy creation would need a main-thread
request/response hop, which buys nothing over a fixed pool of 4.

## 7. Phase 4 -- retire ERHE_PARALLEL_INIT

`ERHE_PARALLEL_INIT` is defined at `editor.cpp:5`, inside
`#if !defined(ERHE_SERIAL_INIT)` -- and `ERHE_SERIAL_INIT` is defined
unconditionally at `:2`. The blocks are therefore dead, but note *why*:
deleting `ERHE_SERIAL_INIT` alone would silently revive all of them.

One of the dead blocks does not even compile as written: `editor.cpp:1616`
reads `m_graphics_device->context_provider`, a member the abstraction
`Device` does not have.

Delete, in `editor.cpp` -- **five `#if defined(ERHE_PARALLEL_INIT)` blocks**
(at `:1434`, `:1449`, `:1459`, `:1615`, `:2522`). Note in particular that the
second and third are separate `#if`s, not one range:

| block | range | note |
|---|---|---|
| taskflow + Tracy observer | `1434-1439` | `#endif` at `:1439` |
| the `ERHE_GET_GL_CONTEXT` / `ERHE_TASK_HEADER` / `ERHE_TASK_FOOTER` macros | `1449-1457` | `#if`/`#else`/`#endif`; **keep the `#else` arm's definitions** as the unconditional ones |
| `m_executor->run(taskflow)` | `1459-1461` | a separate `#if` block |
| `provide_worker_contexts` | `1615-1621` | |
| the graph dump / `taskflow_future.get()` arm | `2522-2564` | `#endif` at `:2564` |

Getting these wrong leaves unbalanced `#if` / `#endif` and a tree that does
not compile, so re-confirm each `#endif` before cutting.

Delete the `ERHE_SERIAL_INIT` / `ERHE_PARALLEL_INIT` defines themselves
(`editor.cpp:1-6`) along with the blocks, and update the two docs that
describe them: `src/editor/notes.md:160`, which says *"Currently serial init
is the default due to GL context sharing issues"* -- a sentence this entire
plan invalidates -- and `doc/init_status_display_phase_ii.md:13`.

`src/editor/transform/trs_tool.cpp` also names `Gl_context_provider` and
`Scoped_gl_context` (the latter at `:152`), but the entire file sits inside
`#if 0` (line 1 to `:1380`) and is a leftover of the retired Component
system. Leave it alone -- but note it, so a grep hit does not read as a live
call site.

## 8. Phase 5 -- context creation and lifetime

### Creation

The creation loop runs on the main thread, for the reasons in section 6's
pool-sizing note: SDL's share-context path make-currents the main context
(`sdl_window.cpp:718`), creates a window (`:725`), mutates a global
non-atomically (`:871`, `:908`) and registers an event watch (`:877`).

### Lifetime -- three defects that this plan would otherwise introduce

`Context_window` has two pre-existing leaks that are harmless at one or two
instances and are **not** harmless at pool scale:

1. **The SDL event watch is never removed.** `Context_window::open`
   registers `SDL_AddEventWatch(Context_window_SDL_EventFilter, this)`
   unconditionally, share contexts included (`sdl_window.cpp:877`). There is
   no `SDL_RemoveEventWatch` anywhere in `src/erhe/window/`, and
   `~Context_window` (`sdl_window.cpp:888-913`) destroys the SDL window but
   leaves the watch registered with a dangling `this`. Worker contexts are
   owned by `Device_impl` (the new pool, at `gl_device.hpp:172` today) and are
   destroyed with the Device, while `m_window` (`editor.cpp:4077`, destroyed
   *after* `m_graphics_device` at `:4078`) is still pumping events. That is a
   shutdown use-after-free **introduced by this plan**. Add
   `SDL_RemoveEventWatch` to the destructor.
2. **`SDL_GL_DestroyContext` is never called.** Absent from
   `sdl_window.cpp` entirely; only `SDL_GL_CreateContext` at `:793`. One
   leaked context today, one per pool entry under this plan. Add it.
3. **Worker contexts have GL debug enabled with no callback.** The share
   constructor requests `SDL_GL_CONTEXT_DEBUG_FLAG` in Debug builds
   (`sdl_window.cpp:710`), but the callback is installed at
   `gl_device.cpp:319` (`gl::debug_message_callback(erhe_opengl_callback,
   nullptr)`) -- once, on the Device's own context. `glDebugMessageCallback`
   is **per-context** GL state, so every GL error a worker raises is
   **silently discarded** -- exactly the class of failure the cross-context
   and re-entrancy problems above produce. Install the callback on each
   worker context at creation, or this plan's own verification is blind.

Fix all three in their own commit, before the pool exists.

### When contexts cannot be created

After phase 0, "no DSA" is no longer one of the cases -- a GL device without
DSA fails at creation. What remains is a GL device whose window cannot
produce a share context (headless / null window), and any failure in the
creation loop. Then no contexts exist and
`Device::supports_worker_contexts()` returns false, and every GPU-touching
worker call site reads as:

```cpp
if (device.supports_worker_contexts()) { /* worker path */ }
else                                    { /* main-thread path */ }
```

**The fallback must be budgeted, not inline.** Routing
`prepare_geometry_buffer_mesh` into the `Scene_commit_queue` lambda, or
`build_imported_buffer_meshes` inline into `tick()`, puts unbounded work on
the frame: `Scene_commit_queue::flush()` runs early in `Editor::tick`
(`editor.cpp:610`, after `command_buffer.begin()` at `:597` and input polling
at `:601`) and before rendering, so a Bistro-sized load becomes a
multi-second stall per tick -- an apparent hang, on exactly the headless /
null-window configurations that take the fallback. Use the existing
per-frame budget machinery (`flush_budgeted`,
`Asset_load_tick_context::budget`) instead.

For `Lightmap_partitioner`, note that the serial path is chosen at launch
time (`lightmap_partitioner.cpp:514`), not inside `process_region`, so the
predicate has to be evaluated in `request_prepare`.

## 9. Phase 6 -- the call sites

GPU-allocating worker tasks:

1. `deferred_finalize_mesh_items`, `async_raytrace_kickoff_operation.cpp:85`
   -- the phase-A prepare loop. **This is the one with the repro.** It is
   dispatched one task per mesh (`items.cpp:170`
   `silent_dependent_async`, from `Async_raytrace_kickoff_operation::execute`,
   `:223-243`), so on Bistro that is thousands of acquire/release pairs and
   thousands of `SDL_GL_MakeCurrent` calls, each a heavyweight driver
   operation.

   **But do not simply hoist the scope above the per-mesh loop.** The loop
   body contains the *expensive CPU work*: `prepare_real_raytrace`, the BVH
   build, at `:126`, and geometry conversion inside
   `prepare_geometry_buffer_mesh` at `:132`. Holding a context across all of
   that against a fixed pool of 4 caps the whole deferred finalize at 4
   concurrent tasks on a 32-thread box -- a large parallelism regression on
   exactly the Bistro-scale load this plan uses as its stress test. The
   `buffer_mesh_allocation_mutex` argument for a small pool applies to the
   *allocation*, not to the surrounding geometry work. So: either keep the
   scope narrow (around allocation only) and accept the make-current cost, or
   size the pool to the concurrency actually wanted. Measure before choosing.
2. `Gltf_load_task::start_build`'s `silent_async` lambda,
   `gltf_load_task.cpp:132` -- `build_imported_buffer_meshes` runs the soup
   path's `Buffer_pool` allocation on a worker: the same crash, one code path
   over. It has simply not been reached yet, because the deferred finalize
   crashes first.
3. `Lightmap_partitioner::process_piece` (**not** `process_region`) -- see
   the subflow discussion in section 6. It reaches `make_renderable_mesh` at
   `lightmap_partitioner.cpp:194`.
4. **The geometry graph evaluates on a worker and builds renderable meshes.**
   `geometry_graph_window.cpp:720` starts a `silent_async` whose body calls
   `run->shadow_graph.evaluate_if_dirty()` at `:729`. That reaches
   `Geometry_graph::evaluate` (`geometry_graph.cpp:106`, `:114`), which calls
   `evaluate` on each node at `:141` -- reaching
   `Geometry_output_node::evaluate` and `make_renderable_mesh` at
   `geometry_output_node.cpp:189` and the ghost variant at `:239` -- and
   `build_preview_primitive` at `:144`, reaching `make_renderable_mesh` at
   `geometry_graph_node.cpp:325`.
5. **Mesh operations are constructed on a worker, and their constructors
   build renderable meshes.** `Operations::async_mesh_operation`
   (`operations_window.cpp:665`) does
   `std::make_shared<T>(std::move(mesh_operation_parameters))` inside the
   worker lambda -- the comment at `:663` says so explicitly. Those
   constructors call `make_entries` (e.g.
   `Catmull_clark_subdivision_operation`, `geometry_operations.cpp:50-55`),
   which reaches `make_renderable_mesh` at `mesh_operation.cpp:341`, and the
   CSG path reaches it at `geometry_operations.cpp:689`. Note that
   `items.cpp:170` is only the dispatcher for `deferred_finalize_mesh_items`;
   every *other* consumer of `async_for_nodes_with_mesh` reaches here.

Items 4 and 5 are the ones a scene-loading verification pass will not catch:
loading and rendering never performs a mesh edit. If they are missed, glTF
loads work and the first subdivide, CSG or geometry-graph evaluation still
faults in the driver -- or, post-phase-1, trips
`ERHE_VERIFY_GL_THREAD_HAS_CONTEXT`. Section 12 item 8 exists specifically to
exercise them.

**Still open, needs an explicit check before implementing:**
`gltf_fastgltf.cpp:1109`, `:1165` and `:1571` run nested taskflows inside
`parse_gltf`, which itself already runs on a worker
(`gltf_load_task.cpp:187`). Parse is otherwise CPU-only, but the nested flows
are unexamined. If a scope is taken on the parse thread and a nested taskflow
steals work to *other* threads, the re-entrancy design does not help -- it is
the same shape as the lightmap subflow problem.

Audited and confirmed CPU-only -- no context, no change: `Asset_browser` glTF
scan (`asset_browser.cpp:350`), `Texture_file_loader` decode
(`texture_file_loader.cpp:153`; pixels only, the upload is on main),
`Lightmap_streamer` tile read (`lightmap_streamer.cpp:299`), the BVH TLAS
build (`bvh_scene.cpp:363`), and `Gltf_load_task`'s scan
(`gltf_load_task.cpp:91`).

**This list is not proven complete.** An audit that reasoned site-by-site
already missed items 4 and 5 once. The cheap structural check is that every
`silent_async` / `silent_dependent_async` / `subflow->emplace` in
`src/editor` is accounted for, rather than that each one looked CPU-only on
inspection.

### The Scene_builder Vertex_input_state item

`Scene_builder::make_brushes` workers do **not** create a
`Vertex_input_state`. The `Mesh_memory` constructor pre-registers an entry
for every format up front, on the main thread: `mesh_memory.cpp:404-413`
loops `get_all_vertex_formats()` calling
`get_vertex_input_from_vertex_format(*format)`. By the time any worker runs,
the lookup at `:726-731` always hits an existing entry and never reaches the
`emplace_back` at `:734`.

Consequences:

- Hoisting `Scene_builder::build_info(mesh_memory)` to the main thread is
  still worth doing, as hygiene: it removes a live dependency on constructor
  pre-registration that nothing states or enforces. Keep it as its own
  commit, described as removing a latent coupling, **not** as fixing a live
  crash.
- **It cannot be verified by the phase-1 guard.** The guard on
  `Vertex_input_state_impl::create` is unreachable from `Scene_builder`
  whether or not the hoist lands, so "no assert fired" is a guard that was
  never reached. Do not record it as evidence. Assert the invariant where it
  actually holds instead -- in `get_vertex_input_from_vertex_format`, per
  section 6.

`Brush` itself is safe: `Scene_builder::make_brush` only constructs the
`Brush`, and `Brush::late_initialize()` -- which calls `make_renderable_mesh`
-- is lazy and reached from main-thread paths.

`Programs`' shader compile / link taskflow (`programs.cpp:106`, `:127`) is
commented out. If revived it needs a worker context; record that in
`src/erhe/primitive/erhe_primitive/notes.md`.

## 10. Phase 7 -- per-context binding state and state tracker

**Integral to this plan.** Its place in the sequence: after the per-context
container objects and accessors (commits 8-9), which supply its two
prerequisites -- the context index and the deferred per-context queue
mechanism -- and **before** the worker contexts and their call sites
(commit 12). Landing it in that window gives it the same property that makes
commit 8 safe: with only the main context in existence, per-context caches
of count one are behaviour-neutral and exercised by every frame before any
worker exists to stress them. And it means two hazards never ship: section
2's transitional rule 1 (workers arrive on a tree with no shared cache left
to corrupt), and commit 4's delete-hook abort (worker-side deletion of
shared objects becomes *routed* through the scrub queue below, rather than
forbidden).

`Gl_binding_state` and `OpenGL_state_tracker` describe a **GL context's**
state, but there is exactly one of each, on `Device_impl`. That single
instance is the entire reason for section 2's rule 1, and therefore for the
`DRAW_CAPABLE` guard. Making them per-context retires the rule: a worker
could bind, draw, and run a full render pass, because there would be no
shared mutable state left to corrupt.

This is the same insight as section 6's container objects, applied to the
caches rather than the objects, and it should reuse the same context index.

### Cost 1 -- memory: negligible

`Gl_binding_state::m_texture_stack` is
`s_max_texture_units` (32) x `s_texture_target_count` (10) `std::vector`s
(`gl_binding_state.hpp:177-178`) -- about 10 KB at `sizeof(std::vector) == 32`
-- plus `m_bound_textures` (1.28 KB) and the buffer stacks. Call it ~12 KB
per instance, so ~60 KB for main plus a 4-context pool.

`OpenGL_state_tracker` (`gl_state_tracker.hpp:138-165`) is **not** a plain
aggregate, and per-context instances are not mechanically trivial for it:

- it owns `Vertex_input_state_tracker` (`:94-114`), which holds
  `Device* m_device` (`:111`) and `Gl_binding_state* m_binding_state` (`:112`),
  set through `set_device` / `set_binding_state` (`:105`, `:104`) and wired at
  `gl_device.cpp:125` and `:584`. `OpenGL_state_tracker` itself has
  `set_binding_state` (`:150`) and
  `dump_state(const char*, const Gl_binding_state&)` (`:151`). **Every
  per-context tracker needs its own binding-state and device wiring**, so the
  two per-context types have to be created and connected as a pair.
- `Vertex_input_state_tracker::m_last_state`, `m_attributes` and `m_bindings`
  (`:108-110`) are **cached per-context draw state**. Sharing those across
  contexts produces wrong draws directly -- they are not an optimization
  detail, they are part of what must become per-context.

### Cost 2 -- the real one: shared-object deletion

Container objects are per-context, so a per-context cache fits them exactly
and deletion is local. **Shared objects are not**, and that asymmetry is what
makes this phase non-trivial:

`Gl_buffer`, `Gl_texture`, `Gl_sampler`, `Gl_program` and `Gl_renderbuffer`
each store a single `Gl_binding_state*` captured at construction
(`gl_objects.hpp:129`, `:24`, `:77`, `:43`, `:111`) and scrub exactly that one
cache on destruction (`Gl_buffer::~Gl_buffer` ->
`Gl_binding_state::on_buffer_deleted`). All five are **shared** GL objects,
and there is an `on_*_deleted` hook for each (`gl_binding_state.hpp:151-157`).
Today one cache makes this correct. With N caches, **N-1 of them keep a stale
entry**.

The concrete failure is **name recycling**, and it is silent:

> Context A deletes buffer 7. Context B's cache still says "7 is bound to
> target X". GL recycles name 7 for a new buffer. Context B binds the new
> buffer 7, its cache reports it already bound, the bind is skipped -- and B
> draws from the wrong buffer.

Fix: a **per-context deferred scrub queue**. On deletion of a shared object,
push its name onto every context's pending-scrub list; each context drains its
own list at a defined point. No cross-thread writes into live cache data. This
is the same shape as section 6's per-context deferred-delete queue, so the two
should share one mechanism.

**Two things about that drain are easy to get wrong, and both are silent.**

**(a) The drain must issue real GL unbinds, not just edit cache entries.** GL
unbinds a deleted object **only in the context that deleted it**; in every
other context the real binding point still holds the now-orphaned object. So a
drain that merely zeroes context B's cache entry leaves B's cache saying
"target X: 0" while B's actual GL state still has the orphan bound. B's next
`bind_buffer(target, 0)` sees "already 0", skips the call, and the orphan
stays bound -- keeping a deleted object alive and feeding samplers the old
texture. That is the same silent wrong-draw this section exists to prevent,
mirrored. The drain has to call the real `glBind*` for each affected binding
point and then update the cache.

Note that the in-source comment this area rests on is already wrong:
`gl_binding_state.hpp:147` says *"GL auto-unbinds deleted objects from every
binding point"*. That is true for one context and false for a share group.
Correct it as part of this phase.

**And the queue is itself subject to name recycling.** GL names are
share-group-wide and freed for reuse the instant `glDelete*` runs (5.1.3 in
`doc/gl-spec-section-5.md`: the name "immediately becomes invalid" and "may
be returned by `Gen*` or `Create*` commands"), so between
context A's delete and context B's drain, B may have bound a *new* object that
received the recycled name. A naive drain would then real-unbind a live
binding -- the same class of bug one step further out. The queue entries need
an epoch or generation counter, or the drain must skip any name B has rebound
since the entry was enqueued.

**(b) The main context needs an explicit drain point, because it never
becomes current again.** `Device_impl::on_thread_enter` is the only enter hook
(`gl_device.cpp:1523`) and the application calls it exactly once
(`editor.cpp:2573`); the drawing context is then current for the life of the
process. "Drain when the context next becomes current" therefore never fires
for the one context that draws, so a worker deleting a shared object would
push an entry that is never drained. Name a main-thread drain point --
`Device_impl::wait_frame()` or the start of `begin_frame` -- rather than
leaving it to the acquire path.

### Cost 3 -- hot-path access

47 call sites in `erhe_graphics` reach the binding state through
`Device_impl::get_binding_state()` (declared `gl_device.hpp:133`, defined
`gl_device.cpp:2045`), some of them per-draw. A thread-local lookup per call
is not free. Mitigate by resolving the per-context instance once per command
encoder or render pass and passing it down, rather than a TLS hit per access.

**And that count understates the refactor**, because most holders never go
through the accessor at all: `Vertex_input_state_tracker` stores a
`Gl_binding_state*` (`gl_state_tracker.hpp:112`), all five `Gl_*` object types
store one (above), and so do all five `*_binding_guard` types
(`gl_binding_state.hpp:28`, `:43`, `:59`, `:74`, `:88`). Every one of those
stored pointers has to become "the binding state of the context this object
belongs to", which for a *shared* object is not a single answer -- see the
deletion problem above. This, not the 47 call sites, is the bulk of the work.

### What it relaxes

- Section 2's rule 1 disappears; rules 3 (container objects) and 4 (explicit
  worker-context API) stand -- container objects are still per-context by
  GL's rules, so section 6's accessors are still needed.
- `ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()` loses most of its call sites. Keep
  the role enum and `HAS_CONTEXT`: "no context is current" is still an error,
  and the role is still the cheapest way to detect it.
- Section 5's publication rules are **unchanged** -- they follow from GL's
  shared-object rules, not from the cache design.

One more per-context item this phase has to take, not on the list
above: `Device_impl::s_active_render_pass` is a **static**
(`gl_render_pass.cpp:194`) asserted null when a pass starts (`:605`). "A
worker could run a full render pass" is false while that is process-wide.

Nothing in the earlier phases is wasted: the guards relax rather than being
discarded, and the context index and the deferred queue (commits 8-9) are the
prerequisites that fix this phase's place in the sequence.

## 11. Known pre-existing race this plan makes reachable

`Mesh_memory::allocate_vertex_buffer_range` does
`m_vertex_pools.emplace_back(...)` on the worker (`mesh_memory.cpp:525`;
index pools `:612`) -- a `std::vector` reallocation -- under
`buffer_mesh_allocation_mutex()`. The main thread's `Mesh_memory::flush`
iterates `for (Buffer_pool& pool : m_vertex_pools)` at `:925` **without**
taking that mutex (it is taken only inside `apply_ready_pending_frees`,
`:904`), and again over the index pools at `:928`.

This is pre-existing, but the plan converts it from unreachable -- the GL
build crashes before it gets there -- into the steady state, and section 9
widens it to five concurrent sites. `Pool_block` being `unique_ptr`-owned
means the `Buffer*` handed to `Buffer_transfer_queue` stays stable; the race
is on the pool **vector**, not the buffers.

**Taking the mutex in `flush` and `get_vertex_buffer` is a partial fix that
looks complete.** Those are two of *many* unguarded main-thread readers of
`m_vertex_pools` / `m_index_pools` -- roughly a dozen more across
`mesh_memory.cpp`, including **both** `get_index_buffer` overloads and
`get_memory_usage`, plus the allocate-path reads.

**Re-derive that list from the members rather than trusting a list here**: an
enumeration written once has already drifted, and a mutex retrofit is only
correct if it is exhaustive. `grep -n 'm_vertex_pools\|m_index_pools'
mesh_memory.cpp` is the whole derivation.

**Decide explicitly**, and given the count the **stable-address container is
the better option**: it is correct without an exhaustive list, which is
exactly what the mutex option is not. Deferring with a written reason remains
open. Do not leave it unstated.

## 12. Verification

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

3. **Cross-context publication.** Mutation-check the section 5 fences:
   remove a `wait_publication()` (or a producer fence) and confirm something
   observably breaks. Failing that on this driver, check the plumbing
   structurally instead: every worker-published object carries a sync when
   its name escapes, and the sync is consumed before the first main-context
   touch (`upload_to_buffer` asserts consumed-or-waits at its top). The
   fence itself is retained on spec grounds (5.3.1) either way -- the
   mutation check tests the plumbing, not the spec.

4. **Guards fire, and nothing legitimate trips them.** After a full glTF load
   plus a few hundred frames, no `HAS_CONTEXT` and no remaining
   `DRAW_CAPABLE` assert has fired, and every worker-side `on_*_deleted`
   routed through the scrub queue rather than into another context's cache.
   Mutation-check the guard by temporarily placing an
   `ERHE_VERIFY_GL_THREAD_DRAW_CAPABLE()` inside the worker prepare loop.

   **Scope of this evidence:** a happy-path load exercises no error paths.
   `Gl_buffer::~Gl_buffer` calls `on_buffer_deleted` whenever its
   `binding_state` is non-null, so worker-side handle destruction happens
   only on a failure path (`create_new_block` returning false,
   `buffer_pool.cpp:216`; a throwing `make_unique<Buffer>`) -- which is also
   the path that exercises the scrub-queue routing (item 10 below) -- and a
   successful run proves nothing about it. Either force one of those paths,
   or record the limit rather than claiming the class is clear.

5. **The other backends stay unaffected.** `ninja` Vulkan Debug + Release, VS
   null backend, VS vulkan-headless, Quest APK. Load `ABeautifulGame.glb` on
   Vulkan and confirm pixel-identity to a pre-change capture via
   `scripts/mesh_ab_capture.py` (MCP port 3743, DDGI off, paused time,
   control pair first).

6. **Shutdown is clean.** Run under ASan with the pool populated, load a
   glTF, exit. No use-after-free from the SDL event watch, no leaked
   contexts.

7. **The per-object accessors.**

   - **Same object, two contexts, concurrently.** A worker under
     `Scoped_worker_context` takes `Scoped_vertex_input_state` on a state the
     main thread is *also* drawing with, in the same frame. Both must
     succeed. **Do not check this by comparing GL names**: container objects
     have a per-context name space, so both contexts allocate from 1 upward
     and equal names are expected, not a failure. Check the per-context map
     -- two entries, two distinct context indices.
   - **The same for `Scoped_framebuffer`**, and then a worker-side
     `blit_framebuffer` between two render passes it holds accessors for.
   - **Idempotence and cost.** Re-entering an accessor for an object already
     present on the current context must not create a second GL object, and
     must not take the GL driver path at all.
   - **Destruction.** Destroy a `Vertex_input_state` and a `Render_pass` on
     the main thread while worker contexts hold instances, then re-acquire
     each worker context and confirm the deferred-delete queue drained --
     under ASan, and with a GL object-count check, since a leak here is
     silent.
   - **Mutation-check the guard**: reach `Vertex_input_state_impl::create`
     from a worker *without* an accessor and confirm
     `ERHE_VERIFY_GL_THREAD_HAS_CONTEXT()` fires. Without this the accessor
     is a convention rather than a mechanism.

8. **The mesh-edit worker call sites** (section 9 items 4 and 5), which no
   scene load exercises. In the GL build:
   - a Catmull-Clark subdivide and a CSG operation on a selected mesh
     (`operations_window.cpp:665` -> `mesh_operation.cpp:341` /
     `geometry_operations.cpp:689`);
   - a geometry-graph evaluation that produces output and preview geometry
     (`geometry_graph_window.cpp:720` -> `geometry_output_node.cpp:189`,
     `:239`, `geometry_graph_node.cpp:325`);
   - a lightmap partition run, taking the parallel path
     (`lightmap_partitioner.cpp:514`), and separately the serial path so the
     main-thread no-op is covered.

   These can be driven through the editor MCP server (127.0.0.1:**3743**,
   per `src/editor/mcp/mcp_server.hpp:75`) rather than by hand.

9. **Worker-side texture create and upload.** Nothing does this today, so
   items 1-8 prove nothing about it. A worker under `Scoped_worker_context`
   creates a `Texture`, uploads pixels to it through a blit encoder, and the
   main thread then samples it and reads back the expected texels. That
   covers both texture publication points of section 5 at once; if either
   fence is missing, or the main thread's first bind precedes its
   `wait_publication()`, this is what catches it.

   Set the pixel-store state on the worker explicitly rather than relying on
   defaults -- it is per-context, so a worker context does **not** inherit
   whatever the main thread last set.

10. **The per-context caches and the scrub queue** (section 10, commits
    10-11).
    - **Commit 10 is behaviour-neutral: prove it.** With only the main
      context in existence, a pixel-identity run against a pre-change
      capture (same recipe as item 5's Vulkan check, on the GL build).
    - **Name-recycling scrub.** A worker deletes a shared buffer / texture
      that the main context has bound; the main drain point issues a *real*
      unbind -- verify with a GL binding query, not by reading the cache --
      and a name recycled between the delete and the drain is *not*
      scrubbed away (the epoch check).
    - **The main-context drain point actually runs.** Provoke a worker-side
      delete and confirm the `wait_frame()` / `begin_frame` drain fires
      within a frame; "drain on next make-current" never fires for the main
      context, which is the failure this item exists to catch.
    - **Per-context tracker wiring.** Each context's tracker resolves its
      *own* binding state -- mutation-check by mis-wiring one pair in a
      scratch build and confirming the item-5 pixel check catches it.

11. **Tests.** `build_tests`, once, at the end of the phase, with
    `ERHE_MCP_TEST_TIMEOUT_S=1`.

## 13. Commit split

1. `graphics: drop macOS OpenGL support` -- sections 3a + 3b.
2. `graphics: require OpenGL 4.5 and delete the non-DSA emulation` --
   section 3c. Separate so a bisect distinguishes "Apple config removed"
   from "emulation removed".
3. *(optional, separate)* `graphics: collapse gates that GL 4.5 makes
   constant` -- section 3d. Not required by anything below.
4. `graphics: GL thread-role guards` -- section 4. Turns the crash into a
   named assert.

   **It is only safe in isolation for object *creation*.** The reasoning
   "the only GL path reaching `create_buffer` off the main thread is the glTF
   load, which already faults" covers creation and says nothing about
   *destruction* -- and this commit also puts `DRAW_CAPABLE` on all seven
   `on_*_deleted` hooks, which `~Gl_texture` / `~Gl_buffer` / `~Gl_sampler` /
   `~Gl_program` / `~Gl_renderbuffer` call whenever their `binding_state` is
   non-null, which is always for Device-created objects (`gl_device.cpp:2083`
   texture, `:2108` buffer, `:2150` sampler, `:2187` program, `:2136`
   renderbuffer). `ERHE_VERIFY` is on in Release. So
   **any** last-`shared_ptr` release of a texture, buffer, sampler or program
   on a taskflow worker becomes a hard abort on a path that works today.

   Candidates to start the audit from, both around section 9's worker paths:
   `Geometry_graph_node` releases preview GPU resources at
   `geometry_graph_node.cpp:270-271` (`m_preview_primitive.reset()` then
   **`m_preview_texture.reset()`**, the latter reaching `~Gl_texture` ->
   `on_texture_deleted` directly) and again at `:277` inside
   `build_preview_primitive`; and the mesh operations of item 5 are
   constructed *and* have their temporaries destroyed on workers.

   Note the two are not equally strong: `m_preview_primitive` is a
   `shared_ptr<Primitive>` whose `Buffer_mesh` holds pool sub-ranges rather
   than `Gl_buffer` handles, so it may not reach a `~Gl_*` at all, while the
   texture release plainly does. **The audit is to establish which of these
   run on a worker and which reach a delete hook** -- do not assume either
   from the call site alone.

   So either audit worker-side destruction before this commit lands, or land
   the seven delete hooks as **log-once**. Do not ship the abort on an
   unaudited path -- and do not bother promoting to `ERHE_VERIFY` later:
   commit 11 replaces the single cache and routes worker-side deletion
   through the scrub queue, retiring these hooks' guard entirely.
   Worker-side destruction with *no context current* remains an error
   (`HAS_CONTEXT` on the object creators/destructors is unaffected), with
   section 4's main-thread deferred-delete queue as the fix if a real such
   site turns up.
5. `window: fix Context_window event-watch and GL context teardown` --
   section 8's three lifetime defects, plus the worker debug callback.
   **Must land before any pool exists.**
6. `editor: retire ERHE_PARALLEL_INIT` -- section 7, mechanical and
   **independently committable**: the blocks are dead (section 7), so this is
   pure removal that interacts with nothing. It must land **before** commit 12,
   which cannot delete `Gl_context_provider` while `editor.cpp:1450` and
   `:1616` still name it.
7. `scene: build brush Build_info on the main thread` -- section 9, hygiene.
8. `graphics: per-context container objects` -- section 6, applied to **all
   three** of `Vertex_input_state_impl`, `Render_pass_impl` and
    `Gpu_timer_impl`, though **not all three the same way** (section 6):

   - `Vertex_input_state_impl` and `Render_pass_impl` get the context-index
     key and the fixed-size per-object slot array; their `m_owner_thread`,
     static registries and thread hooks all go.
   - `Gpu_timer_impl` is **excluded from the accessor mechanism** and instead
     becomes main-thread-only: `m_owner_thread` and both thread hooks go, but
     `s_all_gpu_timers` and `s_mutex` **stay**, because `end_frame`
     (`gl_gpu_timer.cpp:189`) walks the registry to poll results and that is
     not migration.

   All **six** `on_thread_enter` / `on_thread_exit` sweeps go either way, so
   nothing is left on `m_owner_thread` migration while
   `OpenGL_state_tracker::on_thread_enter` still dispatches to it
   (`gl_state_tracker.cpp:736`, `:743`) -- which is the failure this commit
   exists to remove.

   **This is the load-bearing commit, and it is behaviour-neutral on its
   own** -- with one context in existence, a per-context map of size one
   behaves exactly as the current single object does, and the main thread's
   lone `on_thread_enter` (`editor.cpp:2573`) is already a no-op because
   every object is constructed on the main thread. That is what makes a large
   refactor bisectable: it lands and is exercised by every frame before any
   worker context exists to stress it.

   It also removes a live latent bug: `~Vertex_input_state_impl` early-returns
   when `!m_gl_vertex_array.has_value()` **before** erasing itself from
   `s_all_vertex_input_states` (`gl_vertex_input_state.cpp:337-346`), leaving
   a dangling registry pointer. That stops mattering when the registry stops
   existing. If this commit slips, land the one-line erase fix separately; it
   is independently correct.
9. `graphics: per-object scoped accessors` -- `Scoped_vertex_input_state` and
   `Scoped_framebuffer`, no-ops on non-GL backends, plus the deferred
   per-context delete queues. **Adopt them main-thread-first**, at *all four*
   adoption points section 6 names -- converting only the first is the
   defect this list exists to prevent:

   - `Render_command_encoder_impl::set_render_pipeline`
     (`gl_render_command_encoder.cpp:43`);
   - `Render_command_encoder_impl::set_render_pipeline_state` (`:60`);
   - its override-shader-stages overload (`:65`);
   - `Render_pass::start_render_pass` (`render_pass.cpp:233`) for
     `Scoped_framebuffer` -- the backend-neutral wrapper, **not**
     `Render_pass_impl::start_render_pass`, which has no owner back-pointer
     to hand the accessor.

   **Not** `Vertex_input_state_tracker::execute`, which is per-draw. Plus the
   eager per-context creation of the default vertex input state (section 6),
   which has no adoption point at all. Converting these in this commit is
   what gets the mechanism exercised on every frame rather than only by a
   future worker.
10. `graphics: per-context Gl_binding_state and OpenGL_state_tracker` --
    section 10's core. Each context gets a **wired pair** -- the tracker
    holds its own binding-state and device pointers (`gl_device.cpp:125`,
    `:584`) -- keyed by the commit-8 context index.
    `Vertex_input_state_tracker`'s cached draw state (`m_last_state`,
    `m_attributes`, `m_bindings`) goes per-context with it, and
    `Device_impl::s_active_render_pass` (`gl_render_pass.cpp:194`) becomes
    per-context. The hot-path plumbing is section 10 cost 3: resolve the
    per-context instance once per command encoder / render pass and pass it
    down, and re-point every *stored* `Gl_binding_state*` (the five `Gl_*`
    object types, the five `*_binding_guard` types, the tracker) -- that,
    not the 47 accessor call sites, is the bulk.

    **Behaviour-neutral with one context in existence** -- the same
    bisectability property as commit 8, and the reason this lands before the
    pool exists (commit 12) rather than after.
11. `graphics: cross-context scrub queue for shared-object deletion` --
    section 10 cost 2: per-context scrub queues sharing commit 9's deferred
    queue mechanism, real GL unbinds on drain, the name-recycling epoch, and
    the explicit main-thread drain point in `Device_impl::wait_frame()` /
    `begin_frame`. This commit also cashes in what commit 10 makes possible:
    the `DRAW_CAPABLE` guards that section 2's rule 1 motivated -- the
    `Gl_binding_state` mutators and the seven `on_*_deleted` hooks
    (commit 4's log-once) -- relax to per-context-safe `HAS_CONTEXT`. The
    role enum, `HAS_CONTEXT`, and the readback / encoder-construction
    `DRAW_CAPABLE` sites stay. Also correct the wrong comment at
    `gl_binding_state.hpp:147` here.
12. `graphics: GL worker contexts, and their call sites` -- the residual
    parts of sections 5, 6 and 8 plus section 9, in **one** commit (the rest
    of section 6 is commits 8 and 9; section 8's lifetime defects are commit
    5): `Scoped_worker_context`, re-entrancy, the
    eagerly-created pool, the publication fences (producer fence-then-flush,
    the per-object sync member on `Buffer_impl` / `Texture_impl`, and the
    main-thread `wait_publication()` at `upload_to_buffer` and before a
    published texture's first bind), the semaphore-based
    acquire, the deletion of `Gl_context_provider` / `Gl_worker_context` /
    `Scoped_gl_context` / `provide_worker_contexts` entire, **and** section
    9's call sites with their budgeted fallbacks.

    The call sites are not a separate commit: contexts that nothing acquires
    are a pure regression (startup cost, plus a shutdown crash before commit
    5), so the pool and its first users have to land together. Requires
    commit 6 to have removed the last `Gl_context_provider` references, and
    commits 10-11 so that workers arrive on a tree with no shared cache left
    to corrupt.
13. `graphics: split the Blit_command_encoder guard per method` -- section 4:
    upload and copy take `HAS_CONTEXT`, `blit_framebuffer` additionally
    requires `Scoped_framebuffer`, readback keeps `DRAW_CAPABLE`. Depends on
    commit 9.
14. `editor: print a callstack for structured exceptions` -- see below.

Section 11's `Mesh_memory` pool-vector race gets its own commit or an
explicit written deferral; it is not covered by any of the above.

## 14. Aside: the crash handler

`unhandled_exception_filter` (`src/editor/crash_handler.cpp:73`) writes a
minidump but prints **no callstack** for a structured exception, and this
machine has no cdb / windbg. A temporary `erhe_dump_callstack()` call there is
what produced the stack in section 1. Making it permanent is a two-line change
and belongs in its own commit.
