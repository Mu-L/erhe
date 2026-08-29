#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <set>

// The geometry-path optimized-variant build with the split position encoding:
// the base (content) format stores float3 positions, the optimized format
// stores format_16_vec3_snorm, and take_optimizable_snapshot() encodes during
// the gather - against the same AABB affine the decoders use. The weld then
// runs on the encoded bytes. A wrong affine here mispositions every optimized
// vertex rather than failing, which is why this checks the decoded values.

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;

[[nodiscard]] auto make_content_format() -> Vertex_format
{
    return Vertex_format{
        Vertex_stream{
            0,
            {
                {Format::format_32_vec3_float, Vertex_attribute_usage::position, 0}
            }
        }
    };
}

[[nodiscard]] auto make_optimized_format() -> Vertex_format
{
    return Vertex_format{
        Vertex_stream{
            0,
            {
                {Format::format_16_vec3_snorm, Vertex_attribute_usage::position, 0}
            }
        }
    };
}

} // namespace

TEST(OptimizedVariantBuild, geometry_path_encodes_snorm16_positions_and_welds)
{
    // Unit box: 8 distinct vertices at (+-1, +-1, +-1), 6 facets, 24 corners.
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("box");
    erhe::geometry::shapes::make_box(geometry->get_mesh(), -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
        }
    );

    const Vertex_format content_format   = make_content_format();
    const Vertex_format optimized_format = make_optimized_format();

    erhe::buffer::Cpu_buffer                vertex_buffer{"test_vertex", 1024 * 1024};
    erhe::buffer::Cpu_buffer                index_buffer {"test_index",  1024 * 1024};
    erhe::primitive::Cpu_vertex_buffer_sink vertex_buffer_sink{{&vertex_buffer}};
    erhe::primitive::Cpu_index_buffer_sink  index_buffer_sink {index_buffer};

    erhe::primitive::Primitive primitive{geometry};
    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles = true
        },
        .buffer_info = {
            .normal_style               = erhe::primitive::Normal_style::corner_normals,
            .index_type                 = Format::format_32_scalar_uint,
            .vertex_format              = content_format,
            .vertex_buffer_sink         = vertex_buffer_sink,
            .index_buffer_sink          = index_buffer_sink,
            .optimize_meshes            = true,
            .optimized_vertex_format    = &optimized_format,
            .optimized_vertex_input_key = 1
        },
        .normal_style = erhe::primitive::Normal_style::corner_normals
    };
    const bool build_ok = primitive.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals);
    ASSERT_TRUE(build_ok);
    ASSERT_TRUE(primitive.render_shape);
    ASSERT_TRUE(primitive.optimized_render_shape);

    // Base variant: per-corner, float3, exact.
    const erhe::primitive::Buffer_mesh& base_mesh = primitive.render_shape->get_renderable_mesh();
    ASSERT_FALSE(base_mesh.vertex_buffer_ranges.empty());
    const erhe::primitive::Buffer_range& base_range = base_mesh.vertex_buffer_ranges.front();
    EXPECT_EQ(base_range.element_size, 12u);
    EXPECT_EQ(base_range.count,        24u);
    for (std::size_t vertex = 0; vertex < base_range.count; ++vertex) {
        float position[3] = {0.0f, 0.0f, 0.0f};
        std::memcpy(position, vertex_buffer.get_span().data() + base_range.byte_offset + vertex * 12, sizeof(position));
        for (const float component : position) {
            EXPECT_TRUE((component == 1.0f) || (component == -1.0f));
        }
    }

    // Optimized variant: welded (24 position-only corners collapse to the 8 box
    // vertices) and snorm16-encoded against the base build's AABB.
    const erhe::primitive::Buffer_mesh& optimized_mesh = primitive.optimized_render_shape->get_renderable_mesh();
    ASSERT_FALSE(optimized_mesh.vertex_buffer_ranges.empty());
    const erhe::primitive::Buffer_range& optimized_range = optimized_mesh.vertex_buffer_ranges.front();
    EXPECT_EQ(optimized_range.element_size, 6u);
    EXPECT_EQ(optimized_range.count,        8u);

    // The box AABB is [-1, 1] on every axis: center 0, scale 1, so the decode is
    // encoded / 32767. Every decoded component must land on +-1 within one
    // quantization step, and all 8 sign combinations must be present.
    std::set<std::array<int, 3>> sign_combinations;
    for (std::size_t vertex = 0; vertex < optimized_range.count; ++vertex) {
        int16_t encoded[3] = {0, 0, 0};
        std::memcpy(encoded, vertex_buffer.get_span().data() + optimized_range.byte_offset + vertex * 6, sizeof(encoded));
        std::array<int, 3> signs{0, 0, 0};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const float decoded = static_cast<float>(encoded[axis]) / 32767.0f;
            EXPECT_NEAR(std::abs(decoded), 1.0f, 1.0f / 32767.0f);
            signs[axis] = (decoded < 0.0f) ? -1 : 1;
        }
        sign_combinations.insert(signs);
    }
    EXPECT_EQ(sign_combinations.size(), 8u);

    // 6 facets -> 12 fill triangles; the composed mappings describe them all.
    const erhe::primitive::Element_mappings& mappings = primitive.optimized_render_shape->get_element_mappings();
    EXPECT_EQ(mappings.triangle_to_mesh_facet.size(), 12u);
}
