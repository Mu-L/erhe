#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::primitive {

class Buffer_info;
class Element_mappings;
class Primitive_render_shape;
class Triangle_soup;

// Which meshoptimizer passes to run. Pass order is fixed by the implementation
// (weld, vertex cache, overdraw, vertex fetch); these only select which of them
// are applied. Turning every pass off still produces a faithful copy of the
// source soup rather than nothing, so do not do that expecting to disable the
// feature - leave optimization off in config instead.
class Mesh_optimize_options
{
public:
    bool  weld              {true};
    bool  vertex_cache      {true};
    bool  overdraw          {true};
    bool  vertex_fetch      {true};
    float overdraw_threshold{1.05f};
};

// Before/after measurements from meshopt_analyze*(), for logging. The "after"
// fields stay zero when no optimization ran (Mesh_optimize_result::triangle_soup
// null) - they are never seeded from the "before" values, so a consumer can tell
// "unchanged" from "not measured".
class Mesh_optimize_statistics
{
public:
    // False when the ACMR / overdraw / fetch figures below were never taken -
    // a cache hit replays the derivation without running meshopt_analyze*(),
    // which costs a pass of its own and exists only for the log line. Without
    // this flag a hit is indistinguishable from an optimization that achieved
    // nothing at all.
    bool measured{false};

    std::size_t vertex_count_before{0};
    std::size_t vertex_count_after {0};
    std::size_t triangle_count     {0};

    float acmr_before        {0.0f}; // meshopt_analyzeVertexCache().acmr
    float acmr_after         {0.0f};
    float overdraw_before    {0.0f}; // meshopt_analyzeOverdraw().overdraw
    float overdraw_after     {0.0f};
    std::size_t fetch_bytes_before{0}; // meshopt_analyzeVertexFetch().bytes_fetched
    std::size_t fetch_bytes_after {0};

    float elapsed_seconds{0.0f};
};

class Mesh_optimize_result
{
public:
    // A source vertex no triangle references. Welding drops it, so it has no
    // output slot; anything composing through vertex_remap must carry the
    // sentinel through rather than index with it.
    static constexpr uint32_t no_vertex = 0xffffffffu;

    // Optimized copy of the source soup. Null when optimization did not run.
    std::shared_ptr<Triangle_soup> triangle_soup;

    // Triangle permutation: for each output triangle, the source triangle index
    // it came from. Size == triangle count.
    std::vector<uint32_t> triangle_permutation;

    // Forward source -> output vertex remap. Size == source vertex count.
    // Weld-merged source vertices all map to the same output slot; this is
    // exactly what Element_mappings corner composition needs. The output ->
    // source gather table is derivable from this, not the other way around.
    std::vector<uint32_t> vertex_remap;

    Mesh_optimize_statistics statistics;
};

[[nodiscard]] auto optimize_triangle_soup(
    const Triangle_soup&         source,
    const Mesh_optimize_options& options
) -> Mesh_optimize_result;

// optimize_triangle_soup() through a filesystem cache keyed by the source
// content. Optimizing a large primitive costs on the order of 100 ms, and an
// import re-does that on every load of the same asset, so the derivation is
// stored and replayed instead.
//
// What is stored is the two REMAPS, not the optimized soup: they fully
// determine it (gather vertices through the remap, rebuild indices as
// permutation-ordered source triples mapped through it) and they are what
// Element_mappings composition needs anyway. A hit therefore reproduces a
// bit-identical Mesh_optimize_result without calling meshoptimizer at all, and
// the entry stays small - two uint32 tables rather than whole vertex data.
//
// The cache never fails a load. A missing, corrupt, truncated, mismatched or
// unwritable entry degrades to a plain optimize (and a rewrite attempt), with
// at most a warning. `cache_directory` is created if needed; pass an empty path
// to bypass the cache entirely.
//
// Concurrency: loader workers can race on the same entry. Writes go to a
// uniquely named temporary file and are renamed into place, so a reader sees
// either the old entry or the new one, never a half-written one.
[[nodiscard]] auto optimize_triangle_soup_cached(
    const Triangle_soup&         source,
    const Mesh_optimize_options& options,
    const std::filesystem::path& cache_directory
) -> Mesh_optimize_result;

// Rewrites `source` - which describes the ORDER of the soup that was optimized -
// into the equivalent description of the optimized soup, by composing each of
// its members through the remaps in `optimization`.
//
// The two are order-dependent descriptions of the same mesh, so they can never
// be shared between the two builds: a triangle index means a different triangle
// and a vertex buffer index a different vertex after reordering. Composing is
// what lets the optimized build carry a correct description without re-deriving
// one from geometry.
//
//   triangle_to_mesh_facet            through triangle_permutation (an output
//                                     triangle's facet is its source triangle's;
//                                     degenerate NO_INDEX entries carry through)
//   mesh_corner_to_vertex_buffer_index  through vertex_remap (weld-merged source
//   mesh_vertex_to_vertex_buffer_index  vertices land on one output slot, which
//                                     is exactly what a corner needs)
//
// An empty member composes to an empty member: the soup import path leaves the
// mesh_vertex_ table empty, and leaves all of them empty until geometry is
// parsed. A source entry that no output slot exists for - a vertex no triangle
// references, which welding drops - becomes GEO::NO_INDEX rather than an index
// into nothing.
//
// Passing an `optimization` whose triangle_permutation does not cover `source`'s
// triangles, or whose vertex_remap does not cover its vertex indices, is a
// programming error and aborts: it would mean composing against a different
// soup than the one the mappings describe.
[[nodiscard]] auto compose_element_mappings(
    const Element_mappings&     source,
    const Mesh_optimize_result& optimization
) -> Element_mappings;

// Optimizes `source_shape`'s triangle soup and returns a COMPLETE variant shape
// for it - soup optimized, Element_mappings composed onto the new order, buffer
// mesh built - ready to be attached as Primitive::optimized_render_shape.
// Returns null when there is nothing to attach: no soup, an input the optimizer
// refuses (see optimize_triangle_soup), or a buffer mesh that would not build.
//
// It returns rather than attaches on purpose. An optimized shape is visible to
// renderers the instant it is attached, and one attached without a mesh would
// resolve as present and then draw nothing, so the shape is finished first and
// published in one step by the caller - the same publish-once discipline the
// shapes themselves use. That is also why there is no failure path to unwind:
// a variant that could not be finished was simply never attached.
//
// `source_mappings` MUST describe the source shape's triangle SOUP - values
// that index its vertices and triangles - because that is what the remaps
// compose through. Pass `{}` for anything else. This is deliberately a
// parameter rather than read off the shape: a shape that has had a
// geometry-path build carries builder-produced mappings, which index the built
// vertex buffer instead and would compose into nonsense. At the import call
// site the shape has had no such build, and its mappings are empty until the
// deferred geometry finalize anyway.
//
// `cache_directory` is forwarded to optimize_triangle_soup_cached(); empty
// bypasses the cache.
//
// `source_shape` is only read from. `name` appears in the statistics log line.
[[nodiscard]] auto make_optimized_render_shape(
    const Primitive_render_shape& source_shape,
    const Element_mappings&       source_mappings,
    const Mesh_optimize_options&  options,
    const Buffer_info&            buffer_info,
    const std::filesystem::path&  cache_directory,
    std::string_view              name
) -> std::shared_ptr<Primitive_render_shape>;

void log_mesh_optimize_statistics(std::string_view name, const Mesh_optimize_statistics& statistics);

} // namespace erhe::primitive
