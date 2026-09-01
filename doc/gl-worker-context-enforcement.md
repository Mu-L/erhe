# Enforcing the GL worker-context blocking invariant

Status: **proposal**. Nothing here is implemented. Companion to
`doc/gl-worker-thread-contexts.md`, which describes the subsystem itself;
this document covers only how to keep one of its rules from being broken by
future code.

This document has been through three independent code reviews; the
corrections are folded in, and the surviving unknowns are listed under "Not
verified". Each round found errors in the previous round's additions: round 2
corrected the observer firing-point list and a wrong claim that
`scene_builder.cpp:739` parks a worker; round 3 corrected the failure
signature (co-run spins, it does not idle) and found that observers do not
cover all node kinds.

## The invariant

> **A thread holding a worker GL context must never block on work that may
> itself need a worker GL context.**

That is the real constraint, and it is not mechanically checkable. Two
checkable approximations are proposed below (A and B). **Both are proxies** -
each fires on a superset of the real condition - and the gap for each is
recorded so a later reader does not mistake a guard for the rule.

The enforced form of A is the stricter, simpler statement:

> **A thread holding a worker GL context must not spawn tasks.**

## Why: the deadlock shape

`Scoped_worker_context` (`src/erhe/graphics/erhe_graphics/scoped_worker_context.cpp`)
acquires a slot from a pool of share contexts. The size is a **compile-time
constant**, `gl_worker_context_pool_size = 4`
(`src/erhe/graphics/erhe_graphics/gl/gl_context_index.hpp:21`), and is fixed
by design: the same header records that a dynamic pool size would invalidate
every per-object slot array. Raising it is not a configuration change and not
a mitigation.

Acquisition **blocks**: `Device_impl::acquire_worker_context_slot`
(`src/erhe/graphics/erhe_graphics/gl/gl_device.cpp:1798-1811`) waits on a
`std::condition_variable` with no timeout. The scope is a no-op on the main
thread, and is **re-entrant per thread**: `t_worker_context_depth++ > 0`
reuses the slot the thread already holds, so only the outermost scope
acquires and releases.

Re-entrancy is per *thread*, and that is exactly why nesting taskflows under
a scope is dangerous: a stolen task runs on a *different* thread, so it needs
a *different* pool slot. Hold-and-wait follows:

1. N parent tasks each take a scope and hold a slot;
2. each parent blocks in `join()` / `wait()` on child tasks;
3. children run on other threads and each construct their own scope;
4. with N == pool size, no slot can ever be released.

Step 2 needs one refinement, because Taskflow co-runs children on the parked
parent: a permanent wedge requires the parents' remaining children to have
been *stolen* by workers that are themselves now blocked in `acquire`, leaving
the parents with empty local queues and nothing to co-run. That configuration
is reachable, and once reached it is permanent.

`doc/gl-worker-thread-contexts.md` states the rule in Traps as *"Subflow scope
placement cuts both ways: scope in the parent task and stolen children have
no context; scope in every parent against a pool of 4 and `join()` parks all
of them holding contexts. Scope in the leaf that allocates."*
`Lightmap_partitioner` is the worked example: the scope sits in
`process_piece`, not `process_region`, so region tasks park holding nothing.

The failure mode splits the threads into two populations, and confusing them
will misdirect triage. Threads parked in `Subflow::join` are inside
`_corun_until` (`executor.hpp:2314-2352`), which **spins**: it re-explores and
calls `std::this_thread::yield()` past `MAX_STEALS`, never sleeps. Those burn
close to a full core each. Only the threads blocked in
`acquire_worker_context_slot`'s condition variable are genuinely idle. So the
process looks BUSY while making no progress - "hung but idle" is the wrong
signature to look for, and `doc/gl-worker-thread-contexts.md`'s warning about
a deadlock that "looks idle instead of busy" describes the acquire side only.

### What the re-entrancy refcount does and does not buy

When Taskflow co-runs a queued task onto a parked thread that holds a slot,
that task's own scope hits `depth++ > 0` and shares the parent's slot: no new
acquisition, no block, and the per-context caches stay coherent because both
tasks are on the same context. Co-run is therefore *safe*.

Adopting A and B is a deliberate tightening: parking while holding a context
becomes **forbidden**, and the refcount remains as belt-and-braces rather
than a licence to rely on it.

## Current state

Every `Scoped_worker_context` in the tree (complete list, tests excluded):

| scope site | encloses |
| --- | --- |
| `src/editor/assets/gltf_load_task.cpp:149` | `build_imported_buffer_meshes` |
| `src/editor/geometry_graph/geometry_graph_window.cpp:733` | `evaluate_if_dirty` |
| `src/editor/items.cpp:207` | `op(parameters)` (arbitrary mesh operation) |
| `src/editor/operations/async_raytrace_kickoff_operation.cpp:171` | `prepare_geometry_buffer_mesh` |
| `src/editor/renderers/lightmap_partitioner.cpp:203` | `make_renderable_mesh` |

No scope has a **taskflow** spawn beneath it, so the GL pool cannot deadlock
against itself today. For four of the five that is a closed question; for
`items.cpp:207` it is **inference from inspection of today's callers**, because
`op` is an arbitrary caller-supplied mesh operation and any future one could
spawn. That open set is the single strongest argument for D. That is a narrower statement than "the code under a
scope is single-threaded", which is **false**: two other thread pools and a
process-global mutex are reached from inside scopes.

- **The BVH pool.** `Bvh_geometry::commit()` dispatches to a process-wide
  singleton `bvh::v2::ThreadPool` + `ParallelExecutor`
  (`src/erhe/raytrace/erhe_raytrace/bvh/bvh_geometry.cpp:205-222`) and blocks
  on it twice (`:321`, `:344`). It is reached inside two scopes:
  `items.cpp:207` -> `op` -> `make_raytrace()`
  (`src/editor/operations/mesh_operation.cpp:342`, and the same pattern in
  `geometry_operations.cpp:690`, `merge_operation.cpp:189`,
  `merge_static_subtree_operation.cpp:228`, `paint_colors_operation.cpp:103`,
  `paint_weights_operation.cpp:111`, `move_mesh_vertices_operation.cpp:147`,
  and `src/editor/transform/mesh_component_transform.cpp:635,1094,1182`); and
  `geometry_graph_window.cpp:733` -> `evaluate_if_dirty` ->
  `src/editor/geometry_graph/nodes/geometry_output_node.cpp:196`.
- **Geogram's internal `parallel_for` pool**, plus the global
  `geogram_lock()` recursive mutex that `Geometry::process()` takes
  (`src/erhe/geometry/erhe_geometry/geometry.cpp:83,1439-1443`), reached under
  `items.cpp:207` via `mesh_operation.cpp:338`.

Neither pool needs GL slots, so neither can deadlock the GL pool. But the
codebase is **inconsistent** about this and the inconsistency looks
unintentional: `async_raytrace_kickoff_operation.cpp:148`
(`prepare_real_raytrace()`) and `lightmap_partitioner.cpp:206`
(`make_raytrace()`) deliberately keep raytrace work *outside* the scope,
while `items.cpp:207` and `geometry_graph_window.cpp:733` hold a slot
across an entire mesh operation - `Geometry::process()`, the global geogram
lock, and a parallel BVH build included. That directly violates the
throughput caveat in `doc/gl-worker-thread-contexts.md` ("a scope hoisted over
expensive CPU work caps that work at 4 concurrent tasks"), and is worth fixing
independently of anything proposed here.

### Task-spawn sites

`assets/gltf_load_task.cpp:92,144,202`, `asset_browser/asset_browser.cpp:350`,
`geometry_graph/geometry_graph_window.cpp:749`,
`graphics/texture_file_loader.cpp:153`, `items.cpp:246`
(`silent_dependent_async`), `renderers/lightmap_partitioner.cpp:552,561`,
`renderers/lightmap_streamer.cpp:299`, `scene/scene_builder.cpp:681,726-733,739`,
and `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp:1102,1109,1163,1165,1565,1571`.
The standalone `src/geogram_soak/main.cpp:468,476` also spawns; it has no
graphics device and is out of scope for enforcement.

Two of these matter more than the rest:

- **`renderers/lightmap_partitioner.cpp:276` - `subflow->emplace(...)`, joined
  at `:286`.** This is the exact shape the whole document is about. Any
  enforcement that does not cover `subflow->emplace` misses the motivating
  case.
- **`src/erhe/raytrace/erhe_raytrace/bvh/bvh_scene.cpp:363` -
  `executor->silent_async(...)`**, using the editor's executor injected via
  `erhe::raytrace::set_executor` (`src/editor/editor.cpp:1393`). A spawn site
  in a library *below* the editor. `Bvh_scene::commit()` has more callers than
  the main-thread ones (`scene_view.cpp:478`,
  `mcp_server_scene_query.cpp:961,1085`): `mesh_raytrace.cpp:39` calls it from
  the `Raytrace_primitive` constructor, reached from
  `Mesh::update_rt_primitives` (`mesh.cpp:66`) and so from
  `async_raytrace_kickoff_operation.cpp:251,258` - on a worker. Those never
  reach the `silent_async`, but **not** because of thread affinity:
  `start_tlas_build` bails below `k_min_tlas_children = 4`
  (`bvh_scene.hpp:88`) and a per-primitive scene attaches exactly one geometry.
  Do not restate this as "its callers are main-thread"; that is the wrong
  reason, and it means `parse_gltf` is not the only sub-editor seam that
  enforcement has to reach (see C).

The three nested flows in `parse_gltf` hold no GL context, so they cannot
deadlock the GL pool. Only the `gltf_load_task.cpp:202` caller runs on a
worker; the other five (`asset_manager.cpp:1162`, `parsers/gltf.cpp:881,1621`,
`prefabs/prefab_library.cpp:264`, `xr/controller_visualization.cpp:224`) are
main-thread, and none wraps a `Scoped_worker_context`. Note this is **inference from inspection**
(`gltf_fastgltf.hpp:107` and `gltf_fastgltf.cpp:1505` assert no device
access), and `doc/gl-worker-thread-contexts.md` still lists that code as
"never explicitly examined". They do block a taskflow worker with no co-run
(below), which is a separate concern from the GL pool.

## Portability blocker for both proposals

`gl_context_index.{cpp,hpp}` are compiled only inside the
`if (ERHE_GRAPHICS_API_OPENGL)` block (`src/erhe/graphics/CMakeLists.txt:171-172`),
and the only non-test includer from outside `gl/` is
`scoped_worker_context.cpp:6`, itself `#if`-guarded
(`src/erhe/graphics/test/test_worker_context_gl.cpp:25` also includes it, and
is OpenGL-only). Both A's
`gl_thread_is_worker_context()` and B's `get_gl_context_index()` are called
from code that also builds for Vulkan / Metal / null. A backend-neutral
accessor (on `Device`, or a shim that is constant-false off OpenGL) is a
prerequisite for writing either proposal.

## A. Spawn-site guard

A wrapper through which all task spawning goes, asserting that the calling
thread holds no worker context:

```cpp
// erhe_task or editor-local; name provisional
template <typename F>
void spawn(tf::Executor& executor, F&& f)
{
    ERHE_VERIFY(!erhe::graphics::gl_thread_is_worker_context()); // needs the shim above
    executor.silent_async(std::forward<F>(f));
}
```

The tree's spawn vocabulary is wider than `silent_async`, and the wrapper has
to cover the whole of it or it misses the motivating case. Note that
**graph construction is not scheduling**: `taskflow.emplace` only appends to a
graph, and the work is scheduled later by `executor.run`. Guarding `emplace`
would false-positive on the legitimate "build the graph on a context-holding
thread, hand it to a non-holding thread to run" pattern, and guarding `run`
already covers the `tf::Taskflow` case.

`subflow->emplace` is the same graph construction (`tf::Subflow` derives from
`FlowBuilder`, `flow_builder.hpp:1735`); its real schedule point is
`Subflow::join` (`executor.hpp:2535`) or the implicit join. It is guarded at
`emplace` as a **proxy** for that join, which is sound only because emplace
and join always occur in one task body - worth knowing if C ever puts the
check on a member of its own.

| form | guard? | why |
| --- | --- | --- |
| `silent_async` | yes | schedules immediately |
| `silent_dependent_async` | yes | schedules once its predecessors finish (`async.hpp:245-253` sets the join counter), not immediately |
| `executor.run(taskflow)` | yes | the schedule point for a built graph |
| `subflow->emplace` | yes | proxy for the `join()` that schedules it |
| `taskflow.emplace` | no | graph construction, schedules nothing |

`gl_thread_is_worker_context()` (index > 0) already exists in
`gl_context_index.hpp` alongside `gl_thread_has_context()` and
`gl_thread_is_main_context()`; the existing `ERHE_VERIFY_GL_THREAD_*` macros
are the style to match.

- **Catches**: spawn-then-join while holding - the documented trap.
- **Misses**: acquiring a context and then waiting on tasks spawned *before*
  acquisition; blocking on a non-task primitive (condition variable, future)
  that transitively needs a context; any call site reaching `tf::Executor`
  directly; and every non-taskflow pool (BVH, geogram, xatlas), which never
  touches the wrapper at all.
- **Deliberately stricter than the invariant**: a fire-and-forget spawn while
  holding a context cannot deadlock, because the parent never waits. It is
  forbidden anyway, because "did you also wait on it" is not something the
  guard can see.
- **Unstated dependency**: A guards the spawn, never the wait, so it only
  works while spawn and wait live in the same scope. True at every site today;
  it stops being true the moment someone spawns in one function and waits in
  another.
- **Cost**: the `emplace` sites push the conversion above a dozen call sites.
  Runtime cost is one thread-local read per spawn, active in Release
  (`ERHE_VERIFY` is on in Release by design) - negligible, but not zero.

## B. Taskflow observer

`tf::ObserverInterface` (`taskflow/observer/interface.hpp:94-108`: `set_up`,
`on_entry(WorkerView, TaskView)`, `on_exit`) is attached with
`executor.make_observer<T>()` (`executor.hpp:1561`).
`Executor::_observer_prologue` / `_observer_epilogue` wrap task execution.
Re-derived by grepping the call sites, they fire from six of the invoke
paths - **not all of them**, see the coverage hole below:

| `executor.hpp` | task type |
| --- | --- |
| 1987 | `_invoke_static_task` |
| 2006 | `_invoke_subflow_task` |
| 2043 | `_invoke_condition_task` |
| 2055 | `_invoke_multi_condition_task` |
| 2101 | `_invoke_async_task` case 0 - the `void()` overload `silent_async(lambda)` produces |
| 2132 | `_invoke_dependent_async_task` case 0, so `items.cpp:246` is covered |

`_invoke_subflow_task` matters most here: the lightmap region task
(`lightmap_partitioner.cpp:553`) is emplaced with a `tf::Subflow&` parameter,
so it dispatches through that path, and its prologue/epilogue sit around
`h.work(sf)` in the first-entry block. The `subflow->join()` co-run at
`lightmap_partitioner.cpp:286` therefore nests strictly inside an observed
outer task - exactly what B needs for the one case this document exists for.
`_corun_until` runs local-queue and stolen tasks through `_invoke`
(`executor.hpp:2314-2352`), and `Subflow::join` reaches it via `_corun_graph`,
so co-run on a parked thread also fires `on_entry`.

**Node-kind coverage is effectively complete.** `_invoke`'s switch
(`executor.hpp:1752-1829`) dispatches ten node kinds. Eight fire the observer:
the six above, plus `Node::RUNTIME` / `Node::NONPREEMPTIVE_RUNTIME` and the
`void(Runtime&)` / `void(Runtime&, bool)` async variants, whose prologue and
epilogue live in `taskflow/core/runtime.hpp:809/813, 849/853, 938/943` - NOT in
`executor.hpp`, which is why grepping that one header understates the
coverage. Only `Node::MODULE` and `Node::ADOPTED_MODULE` are unobserved
(`_invoke_module_task_impl`, `executor.hpp:2065-2089`), and neither can be the
outer frame B needs: that path schedules the subgraph and returns `true` to
preempt, so the worker frame unwinds rather than parking in-frame, and it can
never co-run a nested task onto itself. A future `emplace([](tf::Runtime&){…})`
is therefore observed, and `composed_of` - the one unobserved kind - cannot
park. Preemption paths fire the prologue only on first entry, and the
exception handler runs before the epilogue, so a depth counter will not
drift.

The observer runs **on the executing worker**, so it can read the
thread-local context index; and it is attached to the executor, so a call site
that skips A's wrapper cannot bypass it.

The check: a worker does not begin task B while running task A unless A
blocked. The intra-thread nesting paths all funnel through `_corun_until`,
reached from `Subflow::join`, `Runtime::corun`/`corun_all`, `Executor::corun`,
`Executor::corun_until` (`executor.hpp:2248`) and `tf::TaskGroup`
(`task_group.hpp:728`) - every one a blocking call made from inside a task.
The last two have no call sites in `src/` today. So *nested* `on_entry` is an
in-band signal that the outer task parked:

```cpp
// File scope and BEFORE the class: a static thread_local member would need an
// out-of-line definition, and a namespace-scope declaration after the class
// would not be found by unqualified lookup in the inline member bodies.
thread_local int t_task_depth = 0;

class Gl_context_task_guard : public tf::ObserverInterface
{
public:
    void set_up(std::size_t) override {}
    void on_entry(tf::WorkerView, tf::TaskView task_view) override
    {
        if ((t_task_depth++ > 0) && erhe::graphics::gl_thread_is_worker_context()) {
            ERHE_FATAL(
                "task '%s' co-ran on a thread parked while holding GL worker context slot %d",
                task_view.name().c_str(),
                erhe::graphics::get_gl_context_index()
            );
        }
    }
    void on_exit(tf::WorkerView, tf::TaskView) override { --t_task_depth; }
};
```

- **Catches**: a context-holding task that blocked *and* got a task co-run
  onto it - including the two shapes A misses (waiting on tasks spawned before
  acquisition; spawns that bypassed A's wrapper).
- **Also a proxy, not direct observation.** It detects *holding + parked*, not
  *holding + waiting on work that needs a slot*. A parent parked on children
  that never touch GL is harmless and will still abort. This matters for the
  decision about shipping it in Release.
- **Structurally blind to the wedged state.** A thread stuck in
  `acquire_worker_context_slot`'s `condition_variable::wait` runs no tasks, so
  there is no `on_entry` to observe. Likewise `tf::Future` derives from
  `std::future` (`taskflow/core/taskflow.hpp:630`) and adds no `wait()`
  override, so `executor.run(tf).wait()` parks the calling thread with **no
  co-run at all** - B sees nothing there. That applies to
  `gltf_fastgltf.cpp:1109,1165,1571`, which run on a worker (reached from the
  `silent_async` at `gltf_load_task.cpp:202`). It does NOT apply to
  `scene_builder.cpp:739`: `make_brushes` is reached only from the
  `Scene_builder` constructor (`scene_builder.cpp:114`, run straight-line on
  the main thread at `editor.cpp:2139` - the `ERHE_TASK_HEADER` macros around
  the init sequence create no task; `ERHE_GET_GL_CONTEXT` is empty and
  `ERHE_TASK_HEADER` only pumps a status line) and from `ensure_brushes`
  (`scene_builder.cpp:913`, also main-thread), so it parks the MAIN thread,
  which holds context index 0 and never a pool slot. `Executor::wait_for_all`
  (`executor.hpp:2404-2410`, used at `editor.cpp:2836`) is a second no-co-run
  park, also main-thread. This is not "opportunistic detection"; it is a
  structural hole, and E is what covers it.
- **Reports on the victim thread**: the stack shows the co-run, not who
  parked. Record a breadcrumb (acquiring site) in `Scoped_worker_context` and
  print it. `TaskView::name()` returns an empty string for unnamed async
  tasks, but naming is available - `silent_async(tf::TaskParams{"name"}, f)`
  (`taskflow/core/async.hpp:167-175`) - so naming spawn sites is a fixable
  omission, not a limitation. `scene_builder.cpp:726-733` already names its
  tasks.
- **Cost**: `_observer_prologue` iterates the `_observers` `unordered_set`
  **unconditionally**, per task, whether or not any observer is registered
  (`executor.hpp:1929-1933`). Registering one adds, per task entry AND exit, a
  virtual call plus the `WorkerView` / `TaskView` temporaries, on top of the
  thread-local operations in the guard itself. Gate to debug / validation
  builds, unlike the
  `ERHE_VERIFY_GL_THREAD_*` guards, which are deliberately on in Release.

## A and B together

Complementary, not redundant:

- A fails fast **at the offending spawn**, with the guilty stack - what you
  want while writing code.
- B catches what A misses and cannot be bypassed - what you want in CI and
  under load.

Neither observes the real deadlock. **E does**, and on the evidence above it
carries more of the weight than its "follow-up" billing suggests.

## Follow-ups

### D. CI check that spawns go through the wrapper

A grep-level rule covering the scheduling forms - `silent_async`,
`silent_dependent_async`, `executor->run` / `executor.run`, and
`subflow->emplace` - outside the wrapper. `taskflow.emplace` is deliberately
NOT in the list (graph construction, see A); `subflow->emplace` deliberately
is, because it is the lightmap shape that motivates this document. Without D, A decays from construction into documentation, and
`doc/gl-worker-thread-contexts.md` warns in Traps: *"Do not trust derived
lists in documents; re-derive from code."*

### E. Acquire watchdog

In `acquire_worker_context_slot()`, if a request waits longer than N seconds
while every slot is held, log (or abort) with the holders: slot index, thread,
and the breadcrumb of where each scope was acquired. This observes the
**actual** condition rather than a proxy, including everything A and B miss -
the `run().wait()` sites, condition-variable waits, and any future
non-taskflow blocking - and converts the wedge into a report that names the four holders - which
matters more given the signature above: the co-running threads look busy, so
without E there is nothing pointing at the context pool at all.

### Lock ordering

Held-while-holding-a-slot today: `geogram_lock()`, the mesh-memory allocation
mutex, and the BVH pool. None closes a cycle, because every path acquires the
GL slot *first*. That ordering is consistent, undocumented and unenforced -
one refactor away from inversion, e.g. a thread holding `geogram_lock` that
enters a `Scoped_worker_context`. The invariant as stated already forbids it;
none of A, B, D or E would detect it.

## Considered and not proposed

- **C. Hide `tf::Executor` behind a scheduler facade.** Makes A a compile
  error rather than an assert. Needs a decision about the sub-editor seams:
  `parse_gltf` takes `tf::Executor&` in its arguments, and
  `erhe::raytrace::set_executor` injects the same executor into `erhe_raytrace`
  (`bvh_scene.cpp:363`).
- **F. Non-blocking acquire.** `try_acquire` plus the budgeted main-thread
  fallback that requirement 6 already defines for the no-worker-contexts case.
  *Eliminates* the deadlock class instead of policing it, but the blocking
  acquire is deliberate, so it is a separate design decision rather than an
  enforcement mechanism.

## Before landing

1. Decide whether the scopes at `items.cpp:207` and
   `geometry_graph_window.cpp:733` should be narrowed to match
   `async_raytrace_kickoff_operation.cpp` and `lightmap_partitioner.cpp`,
   which keep raytrace and geogram work outside the slot. This is a real
   throughput bug independent of enforcement.
2. Add the backend-neutral context-index accessor; neither A nor B compiles
   without it.
3. Run the lightmap parallel path, a glTF load and a geometry-graph evaluation
   with B attached and confirm it does not fire - "the invariant holds today"
   is inspection, not observation.
4. Decide how A covers the `erhe_raytrace` seam: `bvh_scene.cpp:363` spawns
   through the editor's injected executor from a library below the editor, so
   a wrapper that lives in the editor cannot reach it. Its callers are
   main-thread today, so nothing is broken - but A's coverage claim is false
   from day one unless this is answered.
5. Decide whether B ships in Release or is debug / validation only, knowing it
   is a proxy that can abort on a harmless park.

## Not verified

- Whether xatlas (reached via geogram `PACK_XATLAS` under the `items.cpp:207`
  scope) spawns its own threads. It is behind the geogram pin. It would be a
  third non-taskflow pool under a scope, not a new class of problem.
- Whether `Executor::corun` / `Runtime::corun` are reachable from any editor
  path; no call sites found in `src/`, but templates make this hard to settle
  by grep.
- That B does not fire in practice (item 3 above).
- That the MCP command dispatch reaching `ensure_brushes` and
  `Bvh_scene::commit()` runs on the main thread. The call sites are `Command`
  apply paths and the rest of the codebase is consistent with it, but the
  dispatch thread was not traced. This is load-bearing for both the
  `scene_builder.cpp:739` and the `bvh_scene.cpp:363` arguments.
