#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::primitive {

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

void log_mesh_optimize_statistics(std::string_view name, const Mesh_optimize_statistics& statistics);

} // namespace erhe::primitive
