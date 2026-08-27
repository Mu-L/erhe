#pragma once

#include "erhe_primitive/buffer_range.hpp"
#include "erhe_primitive/vertex_attribute_info.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <geogram/basic/geometry.h>

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace erhe::geometry {
    class Geometry;
    class Mesh_info;
}

namespace erhe::primitive {

class Build_context;
class Vertex_buffer_sink;
class Index_buffer_sink;
class Buffer_mesh;

/// Writes vertex attribute values to byte buffer/memory.
///
/// Vertex_buffer_writer is target API agnostic.
class Vertex_buffer_writer
{
public:
    // `vertex_count` sizes the CPU staging vector. It comes from the mesh
    // counts rather than from an allocated Buffer_range, because the GPU
    // allocation happens AFTER the build - see Build_context_root::allocate_buffers().
    Vertex_buffer_writer(Build_context& build_context, Vertex_buffer_sink& buffer_sink, std::size_t stream, std::size_t stride, std::size_t vertex_count);
    Vertex_buffer_writer(const Vertex_buffer_writer&) = delete;
    Vertex_buffer_writer& operator=(const Vertex_buffer_writer&) = delete;
    Vertex_buffer_writer(Vertex_buffer_writer&&) = delete;
    Vertex_buffer_writer& operator=(Vertex_buffer_writer&&) = delete;
    virtual ~Vertex_buffer_writer() noexcept;


    void write(const Vertex_attribute_info& attribute, GEO::vec3  value);
    void write(const Vertex_attribute_info& attribute, GEO::vec2f value);
    void write(const Vertex_attribute_info& attribute, GEO::vec3f value);
    void write(const Vertex_attribute_info& attribute, GEO::vec4f value);
    void write(const Vertex_attribute_info& attribute, GEO::vec2u value);
    void write(const Vertex_attribute_info& attribute, GEO::vec3u value);
    void write(const Vertex_attribute_info& attribute, GEO::vec4u value);
    void write(const Vertex_attribute_info& attribute, GEO::vec2i value);
    void write(const Vertex_attribute_info& attribute, GEO::vec3i value);
    void write(const Vertex_attribute_info& attribute, GEO::vec4i value);

    void write(const Vertex_attribute_info& attribute, glm::vec2 value);
    void write(const Vertex_attribute_info& attribute, glm::vec3 value);
    void write(const Vertex_attribute_info& attribute, glm::vec4 value);
    void write(const Vertex_attribute_info& attribute, uint32_t value);
    void write(const Vertex_attribute_info& attribute, glm::uvec2 value);
    void write(const Vertex_attribute_info& attribute, glm::uvec4 value);
    // Writes three snorm16 components verbatim. The typed write() overloads
    // dispatch on Format alone and cannot express "this attribute is already
    // quantized", so the AABB-encoded position - whose quantization goes
    // through meshopt_quantizeSnorm - needs this raw route.
    void write_snorm16x3(const Vertex_attribute_info& attribute, int16_t x, int16_t y, int16_t z);
    void move (std::size_t relative_offset);
    void next_vertex();

    // Hands the writer the range its staged bytes belong in. Called once, after
    // the build, by Build_context_root::allocate_buffers(). Until it is called
    // the writer has no destination and must not be flushed; the destructor
    // checks that, so a failed allocation drops the staged data instead of
    // writing it at a default (pool 0, offset 0) destination.
    void set_buffer_range(const Buffer_range& range);
    [[nodiscard]] auto has_buffer_range() const -> bool { return m_has_buffer_range; }

    [[nodiscard]] auto start_offset() -> std::size_t;

    Build_context&            build_context;
    Vertex_buffer_sink&       buffer_sink;
    std::size_t               stream;
    std::size_t               stride;
    Buffer_range              buffer_range;
    std::vector<std::uint8_t> vertex_data;
    std::span<std::uint8_t>   vertex_data_span;
    std::size_t               vertex_write_offset{0};

private:
    bool                      m_has_buffer_range{false};
};

/// Writes 8/16/32 -bit indices to byte buffer/memory
///
/// Index_buffer_writer is target API agnostic.
class Index_buffer_writer
{
public:
    Index_buffer_writer(Build_context& build_context, Index_buffer_sink& buffer_sink);
    virtual ~Index_buffer_writer() noexcept;

    // See Vertex_buffer_writer::set_buffer_range().
    void set_buffer_range(const Buffer_range& range);
    [[nodiscard]] auto has_buffer_range() const -> bool { return m_has_buffer_range; }

    void write_corner           (uint32_t v0);
    void write_triangle         (uint32_t v0, uint32_t v1, uint32_t v2);
    void write_expanded_triangle(uint32_t v0, uint32_t v1, uint32_t v2);
    void write_edge             (uint32_t v0, uint32_t v1);
    void write_centroid         (uint32_t v0);

    [[nodiscard]] auto start_offset() -> std::size_t;

    Build_context&                 build_context;
    Index_buffer_sink&             buffer_sink;
    Buffer_range                   buffer_range;
    const erhe::dataformat::Format index_type;
    const std::size_t              index_type_size{0};
    std::vector<std::uint8_t>      index_data;
    std::span<std::uint8_t>        index_data_span;
    std::span<std::uint8_t>        corner_point_index_data_span;
    std::span<std::uint8_t>        triangle_fill_index_data_span;
    std::span<std::uint8_t>        expanded_triangle_fill_index_data_span;
    std::span<std::uint8_t>        edge_line_index_data_span;
    std::span<std::uint8_t>        polygon_centroid_index_data_span;

    std::size_t corner_point_indices_written     {0};
    std::size_t triangle_indices_written         {0};
    std::size_t expanded_triangle_indices_written{0};
    std::size_t edge_line_indices_written        {0};
    std::size_t polygon_centroid_indices_written {0};

private:
    bool m_has_buffer_range{false};
};

} // namespace erhe::primitive
