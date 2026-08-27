#include "erhe_primitive/mesh_optimizer.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <chrono>
#include <numeric>
#include <unordered_map>

namespace erhe::primitive {

namespace {

// The vertex cache model meshopt_analyzeVertexCache() reports against. These
// are the generic values meshoptimizer's own demo uses; the documentation also
// lists vendor-specific models (NVidia 32/32/32, AMD 14/64/128, Intel 128/0/0).
// They affect only the reported ACMR, never any optimization.
constexpr unsigned int cache_size     = 16;
constexpr unsigned int warp_size      = 0;
constexpr unsigned int primgroup_size = 0;

// meshopt_generateVertexRemap() and meshopt_analyzeVertexFetch() both assert
// vertex_size <= 256.
constexpr std::size_t max_vertex_size = 256;

// An ordered triangle triple, used to recover which source triangle each
// output triangle came from. The cache and overdraw passes reorder triangles
// but never rotate or rewrite a triple - vertex cache copies (a, b, c)
// positionally and overdraw memcpy's whole clusters - so an exact ordered
// match is the right key.
class Triangle_key
{
public:
    uint32_t a{0};
    uint32_t b{0};
    uint32_t c{0};

    [[nodiscard]] auto operator==(const Triangle_key& other) const -> bool
    {
        return (a == other.a) && (b == other.b) && (c == other.c);
    }
};

class Triangle_key_hash
{
public:
    [[nodiscard]] auto operator()(const Triangle_key& key) const -> std::size_t
    {
        // FNV-1a over the three indices.
        uint64_t hash = 1469598103934665603ull;
        const uint32_t values[3] = { key.a, key.b, key.c };
        for (const uint32_t value : values) {
            for (int byte = 0; byte < 4; ++byte) {
                hash ^= static_cast<uint64_t>((value >> (byte * 8)) & 0xffu);
                hash *= 1099511628211ull;
            }
        }
        return static_cast<std::size_t>(hash);
    }
};

// erhe::dataformat::convert() covers about half of the Format enum and its
// default branches are fatal, so anything outside this list must skip the
// overdraw pass rather than abort the process. These are the position formats
// erhe actually produces (glTF restricts POSITION to float, and the AABB
// quantization adds snorm16x3).
[[nodiscard]] auto is_convertible_position_format(const erhe::dataformat::Format format) -> bool
{
    switch (format) {
        case erhe::dataformat::Format::format_32_vec2_float:
        case erhe::dataformat::Format::format_32_vec3_float:
        case erhe::dataformat::Format::format_32_vec4_float:
        case erhe::dataformat::Format::format_16_vec2_snorm:
        case erhe::dataformat::Format::format_16_vec3_snorm:
        case erhe::dataformat::Format::format_16_vec4_snorm: return true;
        default:                                             return false;
    }
}

// Positions as tightly packed float3, which meshopt_optimizeOverdraw() and
// meshopt_analyzeOverdraw() require. Returns an empty vector when the soup has
// no position attribute or one this cannot read, in which case the overdraw
// pass is skipped - it is an ordering heuristic, so losing it costs quality,
// never correctness.
[[nodiscard]] auto make_float_positions(
    const erhe::dataformat::Vertex_format& vertex_format,
    const std::vector<uint8_t>&            vertex_data,
    const std::size_t                      vertex_count,
    const std::size_t                      stride
) -> std::vector<float>
{
    const erhe::dataformat::Attribute_stream position = vertex_format.find_attribute(
        erhe::dataformat::Vertex_attribute_usage::position, 0
    );
    if ((position.attribute == nullptr) || !is_convertible_position_format(position.attribute->format)) {
        return {};
    }

    std::vector<float> positions(vertex_count * 3, 0.0f);
    const uint8_t* const base = vertex_data.data() + position.attribute->offset;
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        erhe::dataformat::convert(
            base + vertex * stride,
            position.attribute->format,
            &value[0],
            erhe::dataformat::Format::format_32_vec4_float,
            1.0f
        );
        positions[vertex * 3 + 0] = value[0];
        positions[vertex * 3 + 1] = value[1];
        positions[vertex * 3 + 2] = value[2];
    }
    return positions;
}

} // anonymous namespace

auto optimize_triangle_soup(const Triangle_soup& source, const Mesh_optimize_options& options) -> Mesh_optimize_result
{
    ERHE_PROFILE_FUNCTION();

    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    Mesh_optimize_result result{};

    // Match Triangle_soup::get_vertex_count() and mesh_from_triangle_soup(),
    // which both assume the soup has exactly one stream, bound at 0.
    const erhe::dataformat::Vertex_stream* stream = source.vertex_format.get_stream(0);
    if ((source.vertex_format.streams.size() != 1) || (stream == nullptr)) {
        return result;
    }

    const std::size_t stride = stream->stride;
    if (
        (source.primitive_type != Primitive_type::triangles) ||
        (stride == 0) ||
        (stride > max_vertex_size) ||
        source.index_data.empty() ||
        ((source.index_data.size() % 3) != 0)
    ) {
        return result;
    }

    const std::size_t vertex_count_in = source.get_vertex_count();
    const std::size_t index_count     = source.index_data.size();
    const std::size_t triangle_count  = index_count / 3;
    if (vertex_count_in == 0) {
        return result;
    }

    // erhe does not validate glTF indices anywhere on the import path, and every
    // meshopt entry point below indexes vertex_count-sized scratch arrays by the
    // index values - an out-of-range index is an out-of-bounds heap write once
    // the asserts are compiled out.
    const uint32_t max_index = *std::max_element(source.index_data.begin(), source.index_data.end());
    if (static_cast<std::size_t>(max_index) >= vertex_count_in) {
        return result;
    }

    result.statistics.vertex_count_before = vertex_count_in;
    result.statistics.triangle_count      = triangle_count;

    {
        const meshopt_VertexCacheStatistics before_cache = meshopt_analyzeVertexCache(
            source.index_data.data(), index_count, vertex_count_in, cache_size, warp_size, primgroup_size
        );
        const meshopt_VertexFetchStatistics before_fetch = meshopt_analyzeVertexFetch(
            source.index_data.data(), index_count, vertex_count_in, stride
        );
        result.statistics.acmr_before        = before_cache.acmr;
        result.statistics.fetch_bytes_before = before_fetch.bytes_fetched;

        const std::vector<float> before_positions = make_float_positions(
            source.vertex_format, source.vertex_data, vertex_count_in, stride
        );
        if (!before_positions.empty()) {
            const meshopt_OverdrawStatistics before_overdraw = meshopt_analyzeOverdraw(
                source.index_data.data(), index_count, before_positions.data(), vertex_count_in, 3 * sizeof(float)
            );
            result.statistics.overdraw_before = before_overdraw.overdraw;
        }
    }

    // --- Weld -------------------------------------------------------------
    // Bitwise equality over the whole interleaved vertex, so joints and weights
    // take part in the compare and skinned meshes are safe by construction.
    // Unreferenced source vertices come back as no_vertex and are dropped.
    //
    // Precondition: the compare covers every stride byte INCLUDING inter-attribute
    // and tail padding, so a producer that leaves padding uninitialized simply
    // welds fewer vertices - a silent loss of the optimization, never a wrong
    // result. The glTF importer zero-fills (resize() on a fresh vector).
    std::vector<uint32_t> weld_remap(vertex_count_in, Mesh_optimize_result::no_vertex);
    std::size_t           vertex_count_welded = vertex_count_in;
    std::vector<uint32_t> indices(index_count);
    std::vector<uint8_t>  vertex_data;

    if (options.weld) {
        vertex_count_welded = meshopt_generateVertexRemap(
            weld_remap.data(), source.index_data.data(), index_count, source.vertex_data.data(), vertex_count_in, stride
        );
        meshopt_remapIndexBuffer(indices.data(), source.index_data.data(), index_count, weld_remap.data());
        vertex_data.resize(vertex_count_welded * stride);
        meshopt_remapVertexBuffer(vertex_data.data(), source.vertex_data.data(), vertex_count_in, stride, weld_remap.data());
    } else {
        std::iota(weld_remap.begin(), weld_remap.end(), 0u);
        indices     = source.index_data;
        vertex_data = source.vertex_data;
        vertex_data.resize(vertex_count_welded * stride);
    }

    // The triangle triples as they stand after welding, which is still source
    // triangle order - remapIndexBuffer rewrites indices without reordering.
    // This is the table the permutation is recovered against.
    //
    // One map plus two flat arrays rather than a map of vectors: on a large
    // asset the per-triangle vector allocation dominated the whole pass.
    // `head` holds the first source triangle for a triple and doubles as the
    // consumption cursor; `next_same_triple` chains the rest in source order.
    std::unordered_map<Triangle_key, uint32_t, Triangle_key_hash> head;
    std::vector<uint32_t> next_same_triple(triangle_count, Mesh_optimize_result::no_vertex);
    head.reserve(triangle_count);
    {
        std::vector<uint32_t> tail;
        tail.resize(triangle_count, Mesh_optimize_result::no_vertex);
        std::size_t collision_count = 0;
        for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
            const Triangle_key key{indices[triangle * 3 + 0], indices[triangle * 3 + 1], indices[triangle * 3 + 2]};
            const std::pair<std::unordered_map<Triangle_key, uint32_t, Triangle_key_hash>::iterator, bool> inserted =
                head.emplace(key, static_cast<uint32_t>(triangle));
            if (inserted.second) {
                tail[triangle] = static_cast<uint32_t>(triangle);
            } else {
                // Append, so candidates are consumed in source order.
                const uint32_t first = inserted.first->second;
                next_same_triple[tail[first]] = static_cast<uint32_t>(triangle);
                tail[first] = static_cast<uint32_t>(triangle);
                ++collision_count;
            }
        }
        if (collision_count > 0) {
            // Two distinct source facets can produce a bitwise-identical triple
            // once welding merges coincident geometry. Either candidate gives
            // the same rendering, but triangle_to_mesh_facet composes through
            // this permutation, so facet IDENTITY is ambiguous for them and
            // picking may report the other facet. Geometrically indistinguishable,
            // but worth knowing whether real assets hit it.
            log_primitive->trace(
                "mesh optimize: {} of {} triangles share a triple with an earlier one; facet identity is ambiguous for those",
                collision_count,
                triangle_count
            );
        }
    }

    // --- Vertex cache order ----------------------------------------------
    if (options.vertex_cache) {
        meshopt_optimizeVertexCache(indices.data(), indices.data(), index_count, vertex_count_welded);
    }

    // --- Overdraw order ---------------------------------------------------
    if (options.overdraw) {
        const std::vector<float> positions = make_float_positions(source.vertex_format, vertex_data, vertex_count_welded, stride);
        if (!positions.empty()) {
            meshopt_optimizeOverdraw(
                indices.data(), indices.data(), index_count, positions.data(), vertex_count_welded, 3 * sizeof(float), options.overdraw_threshold
            );
        }
    }

    // --- Recover the triangle permutation ---------------------------------
    // Both passes above permute triangles; neither rewrites a triple. Consume
    // matches in emit order, which is deterministic - a collision means two
    // bitwise-identical (duplicate or degenerate) triangles, and picking either
    // is equally correct.
    result.triangle_permutation.resize(triangle_count);
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const Triangle_key key{indices[triangle * 3 + 0], indices[triangle * 3 + 1], indices[triangle * 3 + 2]};
        const std::unordered_map<Triangle_key, uint32_t, Triangle_key_hash>::iterator i = head.find(key);
        // Unreachable: both passes preserve the multiset of triples exactly, so
        // every emitted triple still has an unconsumed source triangle.
        ERHE_VERIFY(i != head.end());
        ERHE_VERIFY(i->second != Mesh_optimize_result::no_vertex);
        const uint32_t source_triangle = i->second;
        result.triangle_permutation[triangle] = source_triangle;
        // Advance the cursor to the next triangle sharing this triple.
        i->second = next_same_triple[source_triangle];
    }

    // --- Vertex fetch order -----------------------------------------------
    // The Remap form, not optimizeVertexFetch() proper: the remap is needed to
    // compose the forward source -> output vertex mapping below.
    std::vector<uint32_t> fetch_remap(vertex_count_welded, Mesh_optimize_result::no_vertex);
    std::size_t           vertex_count_out = vertex_count_welded;
    if (options.vertex_fetch) {
        vertex_count_out = meshopt_optimizeVertexFetchRemap(fetch_remap.data(), indices.data(), index_count, vertex_count_welded);
        // In-place: unlike its neighbours meshopt_remapIndexBuffer() makes no
        // documented in-place promise, but its implementation is an element-wise
        // forward loop (indexgenerator.cpp), verified against the pinned v1.2.
        // Re-check on a version bump.
        meshopt_remapIndexBuffer(indices.data(), indices.data(), index_count, fetch_remap.data());
        std::vector<uint8_t> fetched(vertex_count_out * stride);
        meshopt_remapVertexBuffer(fetched.data(), vertex_data.data(), vertex_count_welded, stride, fetch_remap.data());
        vertex_data = std::move(fetched);
    } else {
        std::iota(fetch_remap.begin(), fetch_remap.end(), 0u);
    }

    // --- Compose the forward source -> output vertex remap ------------------
    // Weld-merged source vertices land on the same output slot by construction,
    // which is exactly what per-corner Element_mappings composition needs.
    result.vertex_remap.resize(vertex_count_in);
    for (std::size_t vertex = 0; vertex < vertex_count_in; ++vertex) {
        const uint32_t welded = weld_remap[vertex];
        result.vertex_remap[vertex] = (welded == Mesh_optimize_result::no_vertex)
            ? Mesh_optimize_result::no_vertex
            : fetch_remap[welded];
    }

    std::shared_ptr<Triangle_soup> optimized = std::make_shared<Triangle_soup>();
    optimized->vertex_format  = source.vertex_format;
    optimized->primitive_type = source.primitive_type;
    optimized->vertex_data    = std::move(vertex_data);
    optimized->index_data     = std::move(indices);

    result.statistics.vertex_count_after = vertex_count_out;
    {
        const meshopt_VertexCacheStatistics after_cache = meshopt_analyzeVertexCache(
            optimized->index_data.data(), index_count, vertex_count_out, cache_size, warp_size, primgroup_size
        );
        const meshopt_VertexFetchStatistics after_fetch = meshopt_analyzeVertexFetch(
            optimized->index_data.data(), index_count, vertex_count_out, stride
        );
        result.statistics.acmr_after        = after_cache.acmr;
        result.statistics.fetch_bytes_after = after_fetch.bytes_fetched;

        const std::vector<float> after_positions = make_float_positions(
            optimized->vertex_format, optimized->vertex_data, vertex_count_out, stride
        );
        if (!after_positions.empty()) {
            const meshopt_OverdrawStatistics after_overdraw = meshopt_analyzeOverdraw(
                optimized->index_data.data(), index_count, after_positions.data(), vertex_count_out, 3 * sizeof(float)
            );
            result.statistics.overdraw_after = after_overdraw.overdraw;
        }
    }

    result.statistics.measured = true;
    result.triangle_soup = std::move(optimized);
    result.statistics.elapsed_seconds = std::chrono::duration<float>{std::chrono::steady_clock::now() - start_time}.count();
    return result;
}

namespace {

// One member of an Element_mappings that indexes VERTEX BUFFER slots, composed
// through the source -> output vertex remap. A source slot that welding dropped
// - nothing referenced it - has no output slot, so it becomes NO_INDEX rather
// than an index into nothing.
[[nodiscard]] auto compose_vertex_buffer_indices(
    const std::vector<uint32_t>& source,
    const std::vector<uint32_t>& vertex_remap
) -> std::vector<uint32_t>
{
    std::vector<uint32_t> composed;
    composed.resize(source.size());
    for (std::size_t i = 0, end = source.size(); i < end; ++i) {
        const uint32_t source_vertex = source[i];
        if (source_vertex == GEO::NO_INDEX) {
            composed[i] = GEO::NO_INDEX;
            continue;
        }
        // Composing against a remap that does not cover this index would mean
        // the mappings describe a different soup than the one optimized.
        ERHE_VERIFY(source_vertex < vertex_remap.size());
        const uint32_t output_vertex = vertex_remap[source_vertex];
        composed[i] = (output_vertex == Mesh_optimize_result::no_vertex) ? GEO::NO_INDEX : output_vertex;
    }
    return composed;
}

} // anonymous namespace

auto compose_element_mappings(
    const Element_mappings&     source,
    const Mesh_optimize_result& optimization
) -> Element_mappings
{
    ERHE_PROFILE_FUNCTION();

    Element_mappings composed;

    if (!source.triangle_to_mesh_facet.empty()) {
        // The permutation names, for each OUTPUT triangle, the source triangle
        // it came from - so the output table is indexed by output triangle and
        // reads the source table at that source triangle.
        const std::vector<uint32_t>& permutation = optimization.triangle_permutation;
        composed.triangle_to_mesh_facet.resize(permutation.size());
        for (std::size_t triangle = 0, end = permutation.size(); triangle < end; ++triangle) {
            const uint32_t source_triangle = permutation[triangle];
            ERHE_VERIFY(source_triangle < source.triangle_to_mesh_facet.size());
            composed.triangle_to_mesh_facet[triangle] = source.triangle_to_mesh_facet[source_triangle];
        }
    }

    composed.mesh_corner_to_vertex_buffer_index = compose_vertex_buffer_indices(
        source.mesh_corner_to_vertex_buffer_index,
        optimization.vertex_remap
    );
    composed.mesh_vertex_to_vertex_buffer_index = compose_vertex_buffer_indices(
        source.mesh_vertex_to_vertex_buffer_index,
        optimization.vertex_remap
    );

    return composed;
}

auto make_optimized_render_shape(
    const Primitive_render_shape& source_shape,
    const Element_mappings&       source_mappings,
    const Mesh_optimize_options&  options,
    const Buffer_info&            buffer_info,
    const std::filesystem::path&  cache_directory,
    const std::string_view        name
) -> std::shared_ptr<Primitive_render_shape>
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Triangle_soup>& source_soup = source_shape.get_triangle_soup();
    if (!source_soup) {
        // Procedural primitives are built from Geometry and carry no soup.
        // Optimizing those is the geometry path, not this one.
        return {};
    }

    Mesh_optimize_result optimization = optimize_triangle_soup_cached(*source_soup.get(), options, cache_directory);
    if (!optimization.triangle_soup) {
        return {};
    }
    log_mesh_optimize_statistics(name, optimization.statistics);

    // The caller's mappings describe the source order; the variant needs the
    // same correspondence expressed in the optimized order. Empty composes to
    // empty, which is what the import path passes - see the note on the
    // declaration for why these are not read off the shape.
    Element_mappings composed = compose_element_mappings(source_mappings, optimization);

    std::shared_ptr<Primitive_render_shape> shape = std::make_shared<Primitive_render_shape>(
        optimization.triangle_soup,
        std::move(composed)
    );
    if (!shape->make_buffer_mesh(buffer_info)) {
        log_primitive->warn("mesh optimize {}: the optimized buffer mesh did not build; rendering the source build", name);
        return {};
    }
    return shape;
}

void log_mesh_optimize_statistics(const std::string_view name, const Mesh_optimize_statistics& statistics)
{
    if (!statistics.measured) {
        // Cache hit: the derivation was replayed, so there are no before/after
        // figures to report. Saying so beats printing zeroes that read like an
        // optimization that did nothing.
        log_primitive->info(
            "mesh optimize {}: {} triangles, vertices {} -> {} ({:+.1f}%), replayed from cache",
            name,
            statistics.triangle_count,
            statistics.vertex_count_before,
            statistics.vertex_count_after,
            (statistics.vertex_count_before > 0)
                ? 100.0f * (static_cast<float>(statistics.vertex_count_after) - static_cast<float>(statistics.vertex_count_before)) / static_cast<float>(statistics.vertex_count_before)
                : 0.0f
        );
        return;
    }
    log_primitive->info(
        "mesh optimize {}: {} triangles, vertices {} -> {} ({:+.1f}%), ACMR {:.3f} -> {:.3f}, "
        "overdraw {:.3f} -> {:.3f}, fetch {} -> {} bytes, {:.1f} ms",
        name,
        statistics.triangle_count,
        statistics.vertex_count_before,
        statistics.vertex_count_after,
        (statistics.vertex_count_before > 0)
            ? 100.0f * (static_cast<float>(statistics.vertex_count_after) - static_cast<float>(statistics.vertex_count_before)) / static_cast<float>(statistics.vertex_count_before)
            : 0.0f,
        statistics.acmr_before,
        statistics.acmr_after,
        statistics.overdraw_before,
        statistics.overdraw_after,
        statistics.fetch_bytes_before,
        statistics.fetch_bytes_after,
        1000.0f * statistics.elapsed_seconds
    );
}

} // namespace erhe::primitive
