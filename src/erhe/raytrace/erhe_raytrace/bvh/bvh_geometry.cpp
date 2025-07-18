#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include <fmt/chrono.h>

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_raytrace/bvh/bvh_geometry.hpp"
#include "erhe_raytrace/bvh/bvh_instance.hpp"
#include "erhe_raytrace/bvh/glm_conversions.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/ray.hpp"

#include "erhe_hash/hash.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_time/timer.hpp"

#include <bvh/v2/bvh.h>
#include <bvh/v2/default_builder.h>
#include <bvh/v2/executor.h>
#include <bvh/v2/node.h>
#include <bvh/v2/ray.h>
#include <bvh/v2/stack.h>
#include <bvh/v2/thread_pool.h>

#include <fstream>

namespace erhe::raytrace {

using Bvh = bvh::v2::Bvh<bvh::v2::Node<float, 3>>;

// TODO Add versioning
auto save_bvh(const Bvh& bvh, const uint64_t hash_code) -> bool
{
    std::string file_name = fmt::format("cache/bvh/{}", hash_code);
    std::ofstream out{file_name, std::ofstream::binary};
    if (!out) {
        return false;
    }

    try {
        bvh::v2::StdOutputStream stream{out};
        bvh.serialize(stream);
        return true;
    } catch (...) {
        return false;
    }
}

// TODO Add versioning
auto load_bvh(Bvh& bvh, const uint64_t hash_code) -> bool
{
    std::string file_name = fmt::format("cache/bvh/{}", hash_code);
    std::ifstream in{file_name, std::ofstream::binary};
    if (!in) {
        return false;
    }
    try {
        bvh::v2::StdInputStream stream{in};
        bvh = Bvh::deserialize(stream);
        return true;
    } catch (...) {
        return false;
    }
}

auto IGeometry::create(const std::string_view debug_label, const Geometry_type geometry_type) -> IGeometry*
{
    return new Bvh_geometry(debug_label, geometry_type);
}

auto IGeometry::create_shared(const std::string_view debug_label, const Geometry_type geometry_type) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Bvh_geometry>(debug_label, geometry_type);
}

auto IGeometry::create_unique(const std::string_view debug_label, const Geometry_type geometry_type) -> std::unique_ptr<IGeometry>
{
    return std::make_unique<Bvh_geometry>(debug_label, geometry_type);
}

Bvh_geometry::Bvh_geometry(const std::string_view debug_label, const Geometry_type geometry_type)
    : m_debug_label{debug_label}
{
    static_cast<void>(geometry_type);
}

Bvh_geometry::~Bvh_geometry() noexcept = default;

using Scalar         = float;
using Vec3           = bvh::v2::Vec<Scalar, 3>;
using BBox           = bvh::v2::BBox<Scalar, 3>;
using Tri            = bvh::v2::Tri<Scalar, 3>;
using Node           = bvh::v2::Node<Scalar, 3>;
using Bvh            = bvh::v2::Bvh<Node>;
using PrecomputedTri = bvh::v2::PrecomputedTri<Scalar>;

static constexpr bool should_permute = true; // TODO

class Executor_resources
{
public:
    static Executor_resources& get_instance() {
        static Executor_resources static_instance;
        return static_instance;
    }

    auto get_thread_pool() -> bvh::v2::ThreadPool&       { return m_thread_pool; }
    auto get_executor   () -> bvh::v2::ParallelExecutor& { return m_executor; }

private:
    Executor_resources() : m_thread_pool{}, m_executor{m_thread_pool} {}
    ~Executor_resources(){};

    bvh::v2::ThreadPool       m_thread_pool;
    bvh::v2::ParallelExecutor m_executor;
};

void Bvh_geometry::commit()
{
    ERHE_PROFILE_FUNCTION();

    {
        const Buffer_info* index_buffer_info{nullptr};
        const Buffer_info* vertex_buffer_info{nullptr};
        for (const auto& buffer : m_buffer_infos) {
            if (buffer.type == erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX) {
                index_buffer_info = &buffer;
                continue;
            }
            if (buffer.type == erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX) {
                vertex_buffer_info = &buffer;
                continue;
            }
        }
        if ((index_buffer_info == nullptr) || (vertex_buffer_info == nullptr)) {
            return;
        }

        if (vertex_buffer_info->format != erhe::dataformat::Format::format_32_vec3_float) {
            return;
        }

        if (index_buffer_info->format != erhe::dataformat::Format::format_32_vec3_uint) {
            return;
        }

        erhe::buffer::Cpu_buffer* index_buffer  = index_buffer_info->buffer;
        erhe::buffer::Cpu_buffer* vertex_buffer = vertex_buffer_info->buffer;
        if ((index_buffer == nullptr) || (vertex_buffer == nullptr)) {
            return;
        }

        const char* raw_index_ptr  = reinterpret_cast<char*>(index_buffer ->get_span().data()) + index_buffer_info ->byte_offset;
        const char* raw_vertex_ptr = reinterpret_cast<char*>(vertex_buffer->get_span().data()) + vertex_buffer_info->byte_offset;
        const std::size_t triangle_count = index_buffer_info->item_count;

        std::vector<Tri> tris;

        uint64_t hash_code{0xcbf29ce484222325};
        std::vector<BBox> bboxes(triangle_count);
        std::vector<Vec3> centers(triangle_count);
        {
            ERHE_PROFILE_SCOPE("collect");

            for (std::size_t i = 0; i < triangle_count; ++i) {
                const uint32_t i0 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 0 * sizeof(uint32_t));
                const uint32_t i1 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 1 * sizeof(uint32_t));
                const uint32_t i2 = *reinterpret_cast<const uint32_t*>(raw_index_ptr + i * index_buffer_info->byte_stride + 2 * sizeof(uint32_t));

                const float p0_x = *reinterpret_cast<const float*>(raw_vertex_ptr + i0 * index_buffer_info->byte_stride + 0 * sizeof(float));
                const float p0_y = *reinterpret_cast<const float*>(raw_vertex_ptr + i0 * index_buffer_info->byte_stride + 1 * sizeof(float));
                const float p0_z = *reinterpret_cast<const float*>(raw_vertex_ptr + i0 * index_buffer_info->byte_stride + 2 * sizeof(float));

                const float p1_x = *reinterpret_cast<const float*>(raw_vertex_ptr + i1 * index_buffer_info->byte_stride + 0 * sizeof(float));
                const float p1_y = *reinterpret_cast<const float*>(raw_vertex_ptr + i1 * index_buffer_info->byte_stride + 1 * sizeof(float));
                const float p1_z = *reinterpret_cast<const float*>(raw_vertex_ptr + i1 * index_buffer_info->byte_stride + 2 * sizeof(float));

                const float p2_x = *reinterpret_cast<const float*>(raw_vertex_ptr + i2 * index_buffer_info->byte_stride + 0 * sizeof(float));
                const float p2_y = *reinterpret_cast<const float*>(raw_vertex_ptr + i2 * index_buffer_info->byte_stride + 1 * sizeof(float));
                const float p2_z = *reinterpret_cast<const float*>(raw_vertex_ptr + i2 * index_buffer_info->byte_stride + 2 * sizeof(float));

                hash_code = erhe::hash::hash(p0_x, p0_y, p0_z, hash_code);
                hash_code = erhe::hash::hash(p1_x, p1_y, p1_z, hash_code);
                hash_code = erhe::hash::hash(p2_x, p2_y, p2_z, hash_code);

                const bvh::v2::Tri<float, 3> triangle{
                    Vec3{p2_x, p2_y, p2_z},
                    Vec3{p1_x, p1_y, p1_z},
                    Vec3{p0_x, p0_y, p0_z}
                };
                tris.emplace_back(triangle);
                bboxes[i] = triangle.get_bbox();
                centers[i] = triangle.get_center();
            }
            log_geometry->trace("BVH hash for {} : {:x}", debug_label(), hash_code);
        }

        Executor_resources& executor_resources = Executor_resources::get_instance();

        const bool load_ok = load_bvh(m_bvh, hash_code);
        if (!load_ok) {
            typename bvh::v2::DefaultBuilder<Node>::Config config;
            config.quality = bvh::v2::DefaultBuilder<Node>::Quality::High; // TODO Low

            {
                ERHE_PROFILE_SCOPE("bvh build");
                erhe::time::Timer timer{m_debug_label.c_str()};

                timer.begin();
                m_bvh = bvh::v2::DefaultBuilder<Node>::build(
                    executor_resources.get_thread_pool(),
                    bboxes,
                    centers,
                    config
                );
                timer.end();

                const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(timer.duration().value()).count();
                log_geometry->info("BVH build {} in {} ms", debug_label(), time);
            }

            const bool save_ok = save_bvh(m_bvh, hash_code);
            if (!save_ok) {
                log_geometry->error("BVH save failed, hash = {}", hash_code);
            }
        }

        // This precomputes some data to speed up traversal further.
        {
            ERHE_PROFILE_SCOPE("bvh precompute");
            m_precomputed_triangles.clear();
            m_precomputed_triangles.resize(tris.size());

            executor_resources.get_executor().for_each(
                0,
                tris.size(),
                [&] (const size_t begin, const size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        auto j = should_permute ? m_bvh.prim_ids[i] : i;
                        m_precomputed_triangles[i] = tris[j];
                    }
                }
            );
        }
    }
}

void Bvh_geometry::enable()
{
    m_enabled = true;
}

void Bvh_geometry::disable()
{
    m_enabled = false;
}

auto Bvh_geometry::is_enabled() const -> bool
{
    return m_enabled;
}

void Bvh_geometry::set_mask(const uint32_t mask)
{
    m_mask = mask;
}

void Bvh_geometry::set_vertex_attribute_count(const unsigned int count)
{
    m_vertex_attribute_count = count;
}

void Bvh_geometry::set_buffer(
    const Buffer_type               type,
    const unsigned int              slot,
    const erhe::dataformat::Format  format,
    erhe::buffer::Cpu_buffer* const buffer,
    const std::size_t               byte_offset,
    const std::size_t               byte_stride,
    const std::size_t               item_count
)
{
    m_buffer_infos.push_back(
        Buffer_info{type, slot, format, buffer, byte_offset, byte_stride, item_count}
    );
}

void Bvh_geometry::set_user_data(const void* ptr)
{
    m_user_data = ptr;
}

auto Bvh_geometry::intersect_instance(Ray& ray, Hit& hit, Bvh_instance* instance) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return false;
    }
    if ((ray.mask & m_mask) == 0) {
        return false;
    }

    const auto transform = (instance != nullptr) ? instance->get_transform() : glm::mat4{1.0};
    bvh::v2::Ray<Scalar, 3> bvh_ray{
        to_bvh(ray.origin),
        to_bvh(ray.direction),
        ray.t_near,
        ray.t_far
    };

    static constexpr size_t invalid_id = std::numeric_limits<size_t>::max();
    static constexpr size_t stack_size = 64;
    static constexpr bool   use_robust_traversal = false;

    auto  prim_id = invalid_id;
    float u;
    float v;

    // Traverse the BVH and get the u, v coordinates of the closest intersection.
    bvh::v2::SmallStack<Bvh::Index, stack_size> stack;
    m_bvh.intersect<false, use_robust_traversal>(
        bvh_ray,
        m_bvh.get_root().index,
        stack,
        [&] (const size_t begin, const size_t end) {
            for (size_t i = begin; i < end; ++i) {
                size_t j = should_permute ? i : m_bvh.prim_ids[i];
                if (auto hit = m_precomputed_triangles[j].intersect(bvh_ray)) {
                    prim_id = i;
                    std::tie(bvh_ray.tmax, u, v) = *hit;
                }
            }
            return prim_id != invalid_id;
        }
    );

    // TODO Would this be ever needed? if (prim_id != invalid_id && (bvh_ray.tmax > 0.0f)) {
    if (prim_id != invalid_id) {
        const auto triangle_index = should_permute ? prim_id : m_bvh.prim_ids[prim_id];
        const auto& triangle = m_precomputed_triangles.at(triangle_index);

        ray.t_far       = bvh_ray.tmax;
        hit.triangle_id = static_cast<unsigned int>(m_bvh.prim_ids[prim_id]);
        hit.uv          = glm::vec2{u, v};
        hit.normal      = glm::vec3{transform * glm::vec4{from_bvh(triangle.n), 0.0f}};
        hit.instance    = instance;
        hit.geometry    = this;
        return true;
    }
    return false;
}

/// auto Bvh_geometry::get_sphere() const -> const erhe::math::Sphere&
/// {
///     return m_bounding_sphere;
/// }

auto Bvh_geometry::get_mask() const -> uint32_t
{
    return m_mask;
}

auto Bvh_geometry::get_user_data() const -> const void*
{
    return m_user_data;
}

auto Bvh_geometry::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
