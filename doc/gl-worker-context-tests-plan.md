# Plan: dedicated tests for the GL worker-context subsystem

Goal: gtest coverage for doc/gl-worker-thread-contexts.md, specifically the
multithreaded contract -- buffers, textures and vertex input state prepared
on worker threads under `Scoped_worker_context` and consumed on the main
thread. Implementing this plan discharges most of that doc's "Remaining
verification" items (1, 5, 7, and the scrub-queue half of 8) with repeatable
tests instead of one-off manual checks.

## Where the tests live

Extend the existing **`erhe_graphics_gpu_tests`** target
(`src/erhe/graphics/test/`). Its `Gpu_test_environment` already creates a
hidden window + `Device` once per process -- on the OpenGL configuration
that is a real share-capable window, so the Device creates the worker
context pool and `supports_worker_contexts()` is true. The `Gpu_test`
fixture already provides `submit_and_wait`, buffer/texture readback
helpers, and -- importantly -- fails any test during which the device
message callback collected an error. Since the worker contexts install the
GL debug callback per context, **any worker-side GL error automatically
fails the test that caused it**; no extra plumbing needed.

Two new files:

- `test_worker_context.cpp` -- backend-neutral behavioral tests, always in
  the target. On Vulkan / Metal `Scoped_worker_context` is a no-op, so the
  same tests validate plain multithreaded resource creation there; on GL
  they exercise the pool, guards and publication.
- `test_worker_context_gl.cpp` -- GL-only introspection tests (per-context
  slots, scrub queue). Added to the target only when
  `ERHE_GRAPHICS_API STREQUAL "opengl"`; reaches GL internals through the
  public `Device::get_impl()` and the `erhe_graphics/gl/*.hpp` headers.

Skip discipline: a fixture helper `require_worker_contexts()` does
`GTEST_SKIP()` when `!device().supports_worker_contexts()` (GL with a
window that cannot share, hypothetically) so the suite never fails for
environmental reasons. Workers are plain `std::thread`s -- no taskflow
dependency in tests.

The standard test trees apply: `build_tests` (OpenGL, non-ASAN) is the
per-phase run; one ASAN pass (`build_tests_asan`) at the end for the
destruction / shutdown tests. `ERHE_MCP_TEST_TIMEOUT_S=1` as always.

## Phase 0 -- prerequisite hardening (own commit, before any test)

The main-side **blit-encoder** paths neither wait on a worker-created
source nor on a worker-created destination -- today's flows never hit that
(worker buffers reach main through `upload_to_buffer`, which waits), but
the phase 1 tests consume worker buffers through the blit copy path, so
they would be racing without it. Add `wait_publication()` on:

- the source and destination buffers of the buffer-to-buffer
  `copy_from_buffer`;
- the destination of `fill_buffer`;
- the source texture of the readback `copy_from_texture` and
  `copy_from_buffer`'s destination texture when called from main;
- `generate_mipmaps`' texture.

Each is a null-check no-op in the steady state. This also closes the
"consumer coverage is enumerated, not structural" gap recorded in the
publication assessment. Update the doc's consumer list in the same commit.

## Phase 1 -- worker-prepared buffers, main-thread consumption

- **T1 single worker round trip.** A `std::thread` takes
  `Scoped_worker_context`, creates a device-local (non-persistent -- the
  only worker-legal kind) `Buffer` with `Buffer_create_info::init_data`
  carrying a recognizable pattern, and exits. Main then copies it into a
  readback buffer with the blit encoder inside `submit_and_wait` and
  asserts the bytes. Exercises: worker-side `create_buffer` under the
  guard, storage publication (fence-then-flush in `allocate_storage`), and
  the main-side consumer wait from phase 0.
- **T2 pool-contention stress.** 8 threads (pool is 4 -- the blocking
  acquire is exercised by construction) x N buffers each, unique pattern
  per buffer; join all; main validates every buffer. Run enough
  iterations that contexts are recycled across threads (the executor-reuse
  case the context index exists for).
- **T3 re-entrancy and the main no-op.** Nested `Scoped_worker_context` on
  one worker (inner scope must keep the same context -- observable on GL
  as an unchanged `get_gl_context_index()`), and a `Scoped_worker_context`
  taken on the main thread (no-op; buffer creation still works).

## Phase 2 -- worker-prepared textures, main-thread consumption
(doc verification item 7)

- **T4 worker texture create + upload, main samples.** Inside one worker
  scope: create a staging buffer **with `init_data`** (this sidesteps the
  reverse-direction fence -- the worker produces the pixels itself, since
  workers may not map), create a `Texture`, upload via the blit encoder's
  `copy_from_buffer`, exit. Main renders a textured quad with the existing
  `test_texture_sample` idiom (the `set_sampled_image` bind is the
  publication wait AND rule 4's first-bind attach) and asserts the pixels.
  Set worker pixel-store state explicitly -- a worker context inherits
  nothing.
  - *Open implementation question:* the worker needs a `Command_buffer`
    for the blit encoder off the main thread. Verify at implementation
    time whether constructing one on a worker is supported (encoder
    construction itself carries no guard); if not, add the minimal
    worker-usable path -- that IS the dormant mechanism this test exists
    to wake, so a small enabling change is in scope, not speculative.
- **T5 mipmaps and fills as publication producers.** Worker calls
  `generate_mipmaps` on its uploaded texture / `fill_buffer` on its
  buffer; main reads back a non-zero mip level (`read_texture_level_bytes`)
  / the filled buffer.
- **T6 (separate commit, adds machinery) reverse direction.** Main maps
  and writes a staging buffer, unmaps, creates a main-side handoff fence;
  a worker waits on it, then copies staging into a texture; main samples.
  This implements the doc's stated reverse-direction rule together with
  its first call site (the test), as the doc requires -- likely a small
  public wrapper over `Gl_publication_sync` (fence on the producing
  context, wait on the consuming one; the class is already
  direction-agnostic). If judged premature, defer with a written note --
  but the doc's item 7 explicitly asks for this exercise.

## Phase 3 -- vertex input state across contexts (doc verification item 5)

- **T7 same object, two contexts, concurrently.** Main repeatedly draws a
  small mesh using a `Vertex_input_state` (the `test_vertex_index` idiom)
  while a worker holds `Scoped_vertex_input_state` on the same state.
  Both succeed; pixels correct. GL introspection: the per-object slot
  array has two populated entries with distinct context indices. Do NOT
  compare GL names across contexts -- equal names are expected.
- **T8 idempotence.** Re-taking the accessor on the same worker context
  yields the same `gl_name()` and creates no second object (GL: slot
  value unchanged).
- **T9 destruction drains.** Destroy a `Vertex_input_state` (and a
  `Render_pass`) on main while worker contexts hold instances; a worker
  re-acquires each pool context; assert the deferred-delete queues
  drained. Needs a small **test-only introspection accessor** on
  `Device_impl` (pending-delete count per context) -- add it in this
  phase's commit, clearly commented as a test hook. The environment's
  fail-on-collected-errors then covers "no GL error during drain" for
  free; run this one under ASAN in the end-of-plan pass.
- **T10 worker blit between accessor-held framebuffers.** Worker takes
  `Scoped_framebuffer` on two render passes and calls `blit_framebuffer`;
  main waits on the destination texture's publication and reads it back
  (the doc's accessor-blit check, and the blit-destination publication
  point's only exerciser).

## Phase 4 -- scrub queue, GL-only (doc verification item 8's testable half)

- **T11 real unbind on drain.** Main binds a buffer through the binding
  state; a worker scope releases the last reference to it (destructor
  routes scrub entries to every other context); drive the main drain
  (`submit_and_wait` reaches `wait_frame` / `begin_frame`); verify with a
  **raw `glGetIntegerv` binding query** -- not the cache -- that the real
  binding was cleared, and that the cache agrees.
- **T12 name-recycling epoch, best effort.** Immediately after the
  worker-side delete, create a new buffer on main; when GL hands back the
  recycled name (not forceable, so assert conditionally), the drain must
  NOT unbind it. If the conditional path proves too nondeterministic to
  keep, split the epoch logic into a directly testable unit instead and
  record that in the doc.

## Phase 5 -- guards and observability

- **T13 worker GL errors are observable** (doc verification item 1).
  Inside a worker scope, provoke a deliberate GL error (e.g. bind a bogus
  name); `take_messages()` must contain it; consume the messages so
  TearDown does not fail the case. This is the check that makes every
  other worker test's silence meaningful.
- **T14 (optional) guard death tests.** A worker thread with no scope
  calling buffer creation should die on `ERHE_VERIFY` -- but on MSVC
  `ERHE_FATAL` runs `DebugBreak()` before `abort()`, which may interact
  badly with gtest death-test matching. Investigate once; if flaky, drop
  the death tests and leave guard mutation checks as the documented
  manual procedure.

## Support changes summary (kept minimal, each with its phase's commit)

1. Phase 0: consumer `wait_publication()` at the main-side blit methods.
2. Phase 2: worker-usable command-buffer/blit path if missing; T6's
   main-side handoff fence API (separate commit).
3. Phase 3: test-only pending-delete introspection on `Device_impl`.

## Discipline and closure

Per step: edit -> build (`build_tests` tree) -> self-review diff ->
commit; tests run once per phase. Keep the null-backend and ninja-vulkan
builds green (the backend-neutral test file compiles everywhere). On
completion: mark verification items 1, 5, 7, 8 (testable half) done in
doc/gl-worker-thread-contexts.md's "Future work", note the two items NOT
covered by tests (the clean-shutdown ASAN sweep stays a manual/CI concern
beyond the T9 ASAN run; publication fence mutation-testing stays manual),
and delete this plan file, folding anything durable into that doc.
