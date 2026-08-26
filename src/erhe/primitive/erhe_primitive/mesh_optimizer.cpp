#include "erhe_primitive/mesh_optimizer.hpp"

#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"

#include <meshoptimizer.h>

#include <algorithm>

namespace erhe::primitive {

namespace {

// The vertex cache model meshopt_analyzeVertexCache() reports against. These
// are the generic values meshoptimizer's own demo uses; the documentation also
// lists vendor-specific models (NVidia 32/32/32, AMD 14/64/128, Intel 128/0/0).
// They affect only the reported ACMR, never any optimization.
constexpr unsigned int cache_size     = 16;
constexpr unsigned int warp_size      = 0;
constexpr unsigned int primgroup_size = 0;

// meshopt_analyzeVertexFetch() asserts vertex_size <= 256.
constexpr std::size_t max_analyzed_stride = 256;

} // anonymous namespace

auto optimize_triangle_soup(const Triangle_soup& source, const Mesh_optimize_options& options) -> Mesh_optimize_result
{
    ERHE_PROFILE_FUNCTION();

    static_cast<void>(options);

    Mesh_optimize_result result{};

    // Match Triangle_soup::get_vertex_count() and mesh_from_triangle_soup(),
    // which both assume the soup has exactly one stream, bound at 0.
    const erhe::dataformat::Vertex_stream* stream = source.vertex_format.get_stream(0);
    if ((source.vertex_format.streams.size() != 1) || (stream == nullptr)) {
        return result;
    }

    // Note: Triangle_soup::primitive_type is never assigned by the glTF importer
    // (it keeps the class default), so it cannot be relied on as the triangle-list
    // discriminator - hence the explicit index-count check. Populating it properly
    // is phase 3 work.
    const std::size_t stride = stream->stride;
    if (
        (source.primitive_type != Primitive_type::triangles) ||
        (stride == 0) ||
        (stride > max_analyzed_stride) ||
        source.index_data.empty() ||
        ((source.index_data.size() % 3) != 0)
    ) {
        return result;
    }

    const std::size_t vertex_count = source.get_vertex_count();
    const std::size_t index_count  = source.index_data.size();
    if (vertex_count == 0) {
        return result;
    }

    // erhe does not validate glTF indices anywhere on the import path, and the
    // meshopt analyzers write into vertex_count-sized scratch arrays indexed by
    // the index values - an out-of-range index is an out-of-bounds heap write
    // once the asserts are compiled out.
    const uint32_t max_index = *std::max_element(source.index_data.begin(), source.index_data.end());
    if (static_cast<std::size_t>(max_index) >= vertex_count) {
        return result;
    }

    result.statistics.vertex_count_before = vertex_count;
    result.statistics.triangle_count      = index_count / 3;

    const meshopt_VertexCacheStatistics vertex_cache_statistics = meshopt_analyzeVertexCache(
        source.index_data.data(), index_count, vertex_count, cache_size, warp_size, primgroup_size
    );
    const meshopt_VertexFetchStatistics vertex_fetch_statistics = meshopt_analyzeVertexFetch(
        source.index_data.data(), index_count, vertex_count, stride
    );

    result.statistics.acmr_before        = vertex_cache_statistics.acmr;
    result.statistics.fetch_bytes_before = vertex_fetch_statistics.bytes_fetched;

    // The optimization passes themselves are implemented in phase 3, along with
    // the overdraw measurement (meshopt_analyzeOverdraw() needs a float3 position
    // array this skeleton does not build) and the elapsed time. Until then this
    // measures the source mesh, leaves every "after" field at zero and returns a
    // null soup, which callers read as "not optimized" and fall back to the
    // source data.
    return result;
}

} // namespace erhe::primitive
