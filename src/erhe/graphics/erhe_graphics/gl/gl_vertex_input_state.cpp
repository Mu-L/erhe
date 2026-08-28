#include "erhe_graphics/gl/gl_vertex_input_state.hpp"
#include "erhe_graphics/gl/gl_binding_state.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_verify/verify.hpp"

#include <mutex>

namespace erhe::graphics {

auto get_gl_attribute_type(const erhe::dataformat::Format format) -> gl::Attribute_type
{
    using namespace erhe::dataformat;
    switch (format) {
        case Format::format_8_scalar_unorm:           return gl::Attribute_type::float_;
        case Format::format_8_scalar_snorm:           return gl::Attribute_type::float_;
        case Format::format_8_scalar_uscaled:         return gl::Attribute_type::float_;
        case Format::format_8_scalar_sscaled:         return gl::Attribute_type::float_;
        case Format::format_8_scalar_uint:            return gl::Attribute_type::unsigned_int;
        case Format::format_8_scalar_sint:            return gl::Attribute_type::int_;
        case Format::format_8_vec2_unorm:             return gl::Attribute_type::float_vec2;
        case Format::format_8_vec2_snorm:             return gl::Attribute_type::float_vec2;
        case Format::format_8_vec2_uscaled:           return gl::Attribute_type::float_vec2;
        case Format::format_8_vec2_sscaled:           return gl::Attribute_type::float_vec2;
        case Format::format_8_vec2_uint:              return gl::Attribute_type::unsigned_int_vec2;
        case Format::format_8_vec2_sint:              return gl::Attribute_type::int_vec2;
        case Format::format_8_vec3_unorm:             return gl::Attribute_type::float_vec3;
        case Format::format_8_vec3_snorm:             return gl::Attribute_type::float_vec3;
        case Format::format_8_vec3_uscaled:           return gl::Attribute_type::float_vec3;
        case Format::format_8_vec3_sscaled:           return gl::Attribute_type::float_vec3;
        case Format::format_8_vec3_uint:              return gl::Attribute_type::unsigned_int_vec3;
        case Format::format_8_vec3_sint:              return gl::Attribute_type::int_vec3;
        case Format::format_8_vec4_unorm:             return gl::Attribute_type::float_vec4;
        case Format::format_8_vec4_snorm:             return gl::Attribute_type::float_vec4;
        case Format::format_8_vec4_uscaled:           return gl::Attribute_type::float_vec4;
        case Format::format_8_vec4_sscaled:           return gl::Attribute_type::float_vec4;
        case Format::format_8_vec4_uint:              return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_8_vec4_sint:              return gl::Attribute_type::int_vec4;
        case Format::format_16_scalar_unorm:          return gl::Attribute_type::float_;
        case Format::format_16_scalar_snorm:          return gl::Attribute_type::float_;
        case Format::format_16_scalar_uscaled:        return gl::Attribute_type::float_;
        case Format::format_16_scalar_sscaled:        return gl::Attribute_type::float_;
        case Format::format_16_scalar_uint:           return gl::Attribute_type::unsigned_int;
        case Format::format_16_scalar_sint:           return gl::Attribute_type::int_;
        case Format::format_16_scalar_float:          return gl::Attribute_type::float_;
        case Format::format_16_vec2_unorm:            return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_snorm:            return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_uscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_sscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_uint:             return gl::Attribute_type::unsigned_int_vec2;
        case Format::format_16_vec2_sint:             return gl::Attribute_type::int_vec2;
        case Format::format_16_vec2_float:            return gl::Attribute_type::float_vec2;
        case Format::format_16_vec3_unorm:            return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_snorm:            return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_uscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_sscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_uint:             return gl::Attribute_type::unsigned_int_vec3;
        case Format::format_16_vec3_sint:             return gl::Attribute_type::int_vec3;
        case Format::format_16_vec3_float:            return gl::Attribute_type::float_vec3;
        case Format::format_16_vec4_unorm:            return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_snorm:            return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_uscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_sscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_uint:             return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_16_vec4_sint:             return gl::Attribute_type::int_vec4;
        case Format::format_16_vec4_float:            return gl::Attribute_type::float_vec4;
        case Format::format_32_scalar_uint:           return gl::Attribute_type::unsigned_int;
        case Format::format_32_scalar_sint:           return gl::Attribute_type::int_;
        case Format::format_32_scalar_float:          return gl::Attribute_type::float_;
        case Format::format_32_vec2_uint:             return gl::Attribute_type::unsigned_int_vec2;
        case Format::format_32_vec2_sint:             return gl::Attribute_type::int_vec2;
        case Format::format_32_vec2_float:            return gl::Attribute_type::float_vec2;
        case Format::format_32_vec3_uint:             return gl::Attribute_type::unsigned_int_vec3;
        case Format::format_32_vec3_sint:             return gl::Attribute_type::int_vec3;
        case Format::format_32_vec3_float:            return gl::Attribute_type::float_vec3;
        case Format::format_32_vec4_uint:             return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_32_vec4_sint:             return gl::Attribute_type::int_vec4;
        case Format::format_32_vec4_float:            return gl::Attribute_type::float_vec4;
        case Format::format_packed1010102_vec4_unorm: return gl::Attribute_type::float_vec4;
        case Format::format_packed1010102_vec4_snorm: return gl::Attribute_type::float_vec4;
        case Format::format_packed1010102_vec4_uint:  return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_packed1010102_vec4_sint:  return gl::Attribute_type::int_vec4;
        case Format::format_packed111110_vec3_unorm:  return gl::Attribute_type::float_vec3;

        default: {
            ERHE_FATAL("Bad format");
            return static_cast<gl::Attribute_type>(0);
        }
    }
}

auto get_gl_vertex_attrib_type(const erhe::dataformat::Format format) -> gl::Vertex_attrib_type
{
    using namespace erhe::dataformat;
    switch (format) {
        case Format::format_8_scalar_unorm:           return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_scalar_snorm:           return gl::Vertex_attrib_type::byte;
        case Format::format_8_scalar_uscaled:         return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_scalar_sscaled:         return gl::Vertex_attrib_type::byte;
        case Format::format_8_scalar_uint:            return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_scalar_sint:            return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec2_unorm:             return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec2_snorm:             return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec2_uscaled:           return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec2_sscaled:           return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec2_uint:              return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec2_sint:              return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec3_unorm:             return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec3_snorm:             return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec3_uscaled:           return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec3_sscaled:           return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec3_uint:              return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec3_sint:              return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec4_unorm:             return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec4_snorm:             return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec4_uscaled:           return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec4_sscaled:           return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec4_uint:              return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec4_sint:              return gl::Vertex_attrib_type::byte;
        case Format::format_16_scalar_unorm:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_scalar_snorm:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_scalar_uscaled:        return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_scalar_sscaled:        return gl::Vertex_attrib_type::short_;
        case Format::format_16_scalar_uint:           return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_scalar_sint:           return gl::Vertex_attrib_type::short_;
        case Format::format_16_scalar_float:          return gl::Vertex_attrib_type::half_float;
        case Format::format_16_vec2_unorm:            return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec2_snorm:            return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec2_uscaled:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec2_sscaled:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec2_uint:             return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec2_sint:             return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec2_float:            return gl::Vertex_attrib_type::half_float;
        case Format::format_16_vec3_unorm:            return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec3_snorm:            return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec3_uscaled:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec3_sscaled:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec3_uint:             return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec3_sint:             return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec3_float:            return gl::Vertex_attrib_type::half_float;
        case Format::format_16_vec4_unorm:            return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec4_snorm:            return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec4_uscaled:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec4_sscaled:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec4_uint:             return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec4_sint:             return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec4_float:            return gl::Vertex_attrib_type::half_float;
        case Format::format_32_scalar_uint:           return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_scalar_sint:           return gl::Vertex_attrib_type::int_;
        case Format::format_32_scalar_float:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec2_uint:             return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_vec2_sint:             return gl::Vertex_attrib_type::int_;
        case Format::format_32_vec2_float:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec3_uint:             return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_vec3_sint:             return gl::Vertex_attrib_type::int_;
        case Format::format_32_vec3_float:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec4_uint:             return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_vec4_sint:             return gl::Vertex_attrib_type::int_;
        case Format::format_32_vec4_float:            return gl::Vertex_attrib_type::float_;
        case Format::format_packed1010102_vec4_unorm: return gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev;
        case Format::format_packed1010102_vec4_snorm: return gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev;
        case Format::format_packed1010102_vec4_uint:  return gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev;
        case Format::format_packed1010102_vec4_sint:  return gl::Vertex_attrib_type::int_2_10_10_10_rev;
        case Format::format_packed111110_vec3_unorm:  return gl::Vertex_attrib_type::unsigned_int_10f_11f_11f_rev;

        default: {
            ERHE_FATAL("Bad format");
            return static_cast<gl::Vertex_attrib_type>(0);
        }
    }
}

auto get_gl_normalized(erhe::dataformat::Format format) -> bool
{
    using namespace erhe::dataformat;
    switch (format) {
        case Format::format_8_scalar_unorm:           return true;
        case Format::format_8_scalar_snorm:           return true;
        case Format::format_8_scalar_uscaled:         return false;
        case Format::format_8_scalar_sscaled:         return false;
        case Format::format_8_scalar_uint:            return false;
        case Format::format_8_scalar_sint:            return false;
        case Format::format_8_vec2_unorm:             return true;
        case Format::format_8_vec2_snorm:             return true;
        case Format::format_8_vec2_uscaled:           return false;
        case Format::format_8_vec2_sscaled:           return false;
        case Format::format_8_vec2_uint:              return false;
        case Format::format_8_vec2_sint:              return false;
        case Format::format_8_vec3_unorm:             return true;
        case Format::format_8_vec3_snorm:             return true;
        case Format::format_8_vec3_uscaled:           return false;
        case Format::format_8_vec3_sscaled:           return false;
        case Format::format_8_vec3_uint:              return false;
        case Format::format_8_vec3_sint:              return false;
        case Format::format_8_vec4_unorm:             return true;
        case Format::format_8_vec4_snorm:             return true;
        case Format::format_8_vec4_uscaled:           return false;
        case Format::format_8_vec4_sscaled:           return false;
        case Format::format_8_vec4_uint:              return false;
        case Format::format_8_vec4_sint:              return false;
        case Format::format_16_scalar_unorm:          return true;
        case Format::format_16_scalar_snorm:          return true;
        case Format::format_16_scalar_uscaled:        return false;
        case Format::format_16_scalar_sscaled:        return false;
        case Format::format_16_scalar_uint:           return false;
        case Format::format_16_scalar_sint:           return false;
        case Format::format_16_scalar_float:          return false;
        case Format::format_16_vec2_unorm:            return true;
        case Format::format_16_vec2_snorm:            return true;
        case Format::format_16_vec2_uscaled:          return false;
        case Format::format_16_vec2_sscaled:          return false;
        case Format::format_16_vec2_uint:             return false;
        case Format::format_16_vec2_sint:             return false;
        case Format::format_16_vec2_float:            return false;
        case Format::format_16_vec3_unorm:            return true;
        case Format::format_16_vec3_snorm:            return true;
        case Format::format_16_vec3_uscaled:          return false;
        case Format::format_16_vec3_sscaled:          return false;
        case Format::format_16_vec3_uint:             return false;
        case Format::format_16_vec3_sint:             return false;
        case Format::format_16_vec3_float:            return false;
        case Format::format_16_vec4_unorm:            return true;
        case Format::format_16_vec4_snorm:            return true;
        case Format::format_16_vec4_uscaled:          return false;
        case Format::format_16_vec4_sscaled:          return false;
        case Format::format_16_vec4_uint:             return false;
        case Format::format_16_vec4_sint:             return false;
        case Format::format_16_vec4_float:            return false;
        case Format::format_32_scalar_uint:           return false;
        case Format::format_32_scalar_sint:           return false;
        case Format::format_32_scalar_float:          return false;
        case Format::format_32_vec2_uint:             return false;
        case Format::format_32_vec2_sint:             return false;
        case Format::format_32_vec2_float:            return false;
        case Format::format_32_vec3_uint:             return false;
        case Format::format_32_vec3_sint:             return false;
        case Format::format_32_vec3_float:            return false;
        case Format::format_32_vec4_uint:             return false;
        case Format::format_32_vec4_sint:             return false;
        case Format::format_32_vec4_float:            return false;
        case Format::format_packed1010102_vec4_unorm: return true;
        case Format::format_packed1010102_vec4_snorm: return true;
        case Format::format_packed1010102_vec4_uint:  return false;
        case Format::format_packed1010102_vec4_sint:  return false;
        case Format::format_packed111110_vec3_unorm:  return false;

        default: {
            ERHE_FATAL("Bad format");
            return false;
        }
    }
}

Vertex_input_state_impl::Vertex_input_state_impl(Device& device)
    : m_device{device}
{
    ensure_created_on_current_context();
}

Vertex_input_state_impl::Vertex_input_state_impl(Device& device, Vertex_input_state_data&& create_info)
    : m_device{device}
    , m_data  {std::move(create_info)}
{
    ensure_created_on_current_context();
}

Vertex_input_state_impl::~Vertex_input_state_impl() noexcept
{
    // Deleting a per-context GL object is only possible on its own context.
    // Until worker contexts exist, every populated slot belongs to the
    // destroying thread's current context; the deferred per-context delete
    // queues arrive with the accessor commit. The mutex excludes a
    // concurrent first-use adoption storing a fresh name into a slot the
    // walk has already passed.
    const std::lock_guard<std::mutex> lock{m_adoption_mutex};
    const int context_index = get_gl_context_index();
    for (int slot = 0; slot < gl_context_slot_count; ++slot) {
        unsigned int name = m_gl_names[slot].load(std::memory_order_relaxed);
        if (name == 0) {
            continue;
        }
        ERHE_VERIFY(slot == context_index);
        m_device.get_impl().get_binding_state().on_vertex_array_deleted(name);
        gl::delete_vertex_arrays(1, &name);
        m_gl_names[slot].store(0, std::memory_order_relaxed);
    }
}

auto Vertex_input_state_impl::ensure_created_on_current_context() const -> unsigned int
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);

    // Populated fast path: relaxed load of our own slot, no lock.
    const unsigned int existing_name = m_gl_names[context_index].load(std::memory_order_relaxed);
    if (existing_name != 0) {
        return existing_name;
    }

    // First use on this context: create and configure under the adoption
    // mutex, so destruction cannot race a concurrent adoption.
    const std::lock_guard<std::mutex> lock{m_adoption_mutex};
    const unsigned int recheck_name = m_gl_names[context_index].load(std::memory_order_relaxed);
    if (recheck_name != 0) {
        return recheck_name;
    }
    GLuint new_name{0};
    gl::create_vertex_arrays(1, &new_name);
    ERHE_VERIFY(new_name != 0);
    update(new_name);
    m_gl_names[context_index].store(new_name, std::memory_order_relaxed);
    return new_name;
}

void Vertex_input_state_impl::update(const unsigned int vao_name) const
{
    ERHE_VERIFY(vao_name > 0);

    for (const auto& attribute : m_data.attributes) {
        switch (get_gl_attribute_type(attribute.format)) {
            //using enum gl::Attribute_type;
            case gl::Attribute_type::bool_:
            case gl::Attribute_type::bool_vec2:
            case gl::Attribute_type::bool_vec3:
            case gl::Attribute_type::bool_vec4:
            case gl::Attribute_type::int_:
            case gl::Attribute_type::int_vec2:
            case gl::Attribute_type::int_vec3:
            case gl::Attribute_type::int_vec4:
            case gl::Attribute_type::unsigned_int:
            case gl::Attribute_type::unsigned_int_vec2:
            case gl::Attribute_type::unsigned_int_vec3:
            case gl::Attribute_type::unsigned_int_vec4: {
                gl::vertex_array_attrib_i_format(
                    vao_name,
                    attribute.layout_location,
                    static_cast<GLint>(erhe::dataformat::get_component_count(attribute.format)),
                    static_cast<gl::Vertex_attrib_i_type>(get_gl_vertex_attrib_type(attribute.format)),
                    static_cast<GLuint>(attribute.offset)
                );
                break;
            }

            case gl::Attribute_type::float_:
            case gl::Attribute_type::float_vec2:
            case gl::Attribute_type::float_vec3:
            case gl::Attribute_type::float_vec4:
            case gl::Attribute_type::float_mat2:
            case gl::Attribute_type::float_mat2x3:
            case gl::Attribute_type::float_mat2x4:
            case gl::Attribute_type::float_mat3:
            case gl::Attribute_type::float_mat3x2:
            case gl::Attribute_type::float_mat3x4:
            case gl::Attribute_type::float_mat4:
            case gl::Attribute_type::float_mat4x2:
            case gl::Attribute_type::float_mat4x3: {
                gl::vertex_array_attrib_format(
                    vao_name,
                    attribute.layout_location,
                    static_cast<GLint>(erhe::dataformat::get_component_count(attribute.format)),
                    get_gl_vertex_attrib_type(attribute.format),
                    get_gl_normalized(attribute.format) ? GL_TRUE : GL_FALSE,
                    static_cast<GLuint>(attribute.offset)
                );
                break;
            }

            case gl::Attribute_type::unsigned_int64_arb:
            case gl::Attribute_type::double_:
            case gl::Attribute_type::double_vec2:
            case gl::Attribute_type::double_vec3:
            case gl::Attribute_type::double_vec4:
            case gl::Attribute_type::double_mat2:
            case gl::Attribute_type::double_mat2x3:
            case gl::Attribute_type::double_mat2x4:
            case gl::Attribute_type::double_mat3:
            case gl::Attribute_type::double_mat3x2:
            case gl::Attribute_type::double_mat3x4:
            case gl::Attribute_type::double_mat4:
            case gl::Attribute_type::double_mat4x2:
            case gl::Attribute_type::double_mat4x3: {
                gl::vertex_array_attrib_l_format(
                    vao_name,
                    attribute.layout_location,
                    static_cast<GLint>(erhe::dataformat::get_component_count(attribute.format)),
                    static_cast<gl::Vertex_attrib_l_type>(get_gl_vertex_attrib_type(attribute.format)),
                    0
                );
                break;
            }

            default: {
                ERHE_FATAL("Bad vertex attrib pointer type");
            }
        }

        gl::vertex_array_attrib_binding(vao_name, attribute.layout_location, static_cast<GLuint>(attribute.binding));
        gl::enable_vertex_array_attrib(vao_name, attribute.layout_location);
    }

    for (const Vertex_input_binding& binding : m_data.bindings) {
        gl::vertex_array_binding_divisor(vao_name, static_cast<GLuint>(binding.binding), binding.divisor);
    }
}

auto Vertex_input_state_impl::gl_name() const -> unsigned int
{
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    return m_gl_names[context_index].load(std::memory_order_relaxed);
}

auto Vertex_input_state_impl::get_data() const -> const Vertex_input_state_data&
{
    return m_data;
}


} // namespace erhe::graphics
