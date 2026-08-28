#pragma once

#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/gl/gl_thread_role.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_gl/wrapper_enums.hpp"

#include <array>
#include <atomic>
#include <mutex>

typedef int          GLsizei;
typedef int          GLint;
typedef unsigned int GLuint;

namespace erhe::graphics {

class Buffer;
class Device;

[[nodiscard]] auto get_vertex_divisor       (erhe::dataformat::Vertex_step step) -> GLuint;
[[nodiscard]] auto get_gl_attribute_type    (erhe::dataformat::Format format) -> gl::Attribute_type;
[[nodiscard]] auto get_gl_vertex_attrib_type(erhe::dataformat::Format format) -> gl::Vertex_attrib_type;
[[nodiscard]] auto get_gl_normalized        (erhe::dataformat::Format format) -> bool;

// GL vertex array objects are container objects: they are NOT shared between
// contexts. One logical Vertex_input_state therefore holds one VAO name per
// context (indexed by get_gl_context_index()), each created lazily on first
// use on that context - there is no ownership, no migration and no hand-back.
class Vertex_input_state_impl
{
public:
    Vertex_input_state_impl(Device& device);
    Vertex_input_state_impl(Device& device, Vertex_input_state_data&& create_info);
    ~Vertex_input_state_impl() noexcept;

    // Current context's VAO name; 0 when not yet created on this context.
    // A relaxed load of the caller's own slot - lock-free.
    [[nodiscard]] auto gl_name () const -> unsigned int;
    [[nodiscard]] auto get_data() const -> const Vertex_input_state_data&;

    // Creates and configures this state's VAO on the calling thread's
    // current context if it does not exist yet, and returns its name.
    // First use on a context takes m_adoption_mutex; afterwards this is the
    // same relaxed own-slot load as gl_name(). const because adoption
    // mutates only the per-context slot, never the logical object - every
    // adoption site reaches the state through a const pointer.
    auto ensure_created_on_current_context() const -> unsigned int;

private:
    // Writes the attribute formats / enables / binding divisors of m_data
    // into the given VAO.
    void update(unsigned int vao_name) const;

    Device&                 m_device;
    Vertex_input_state_data m_data;

    // One VAO name per context; 0 = not created. Each context writes only
    // its own slot, under m_adoption_mutex; reading one's own slot is
    // relaxed and lock-free (the array never reallocates). Cross-slot
    // enumeration (the destructor) takes the same mutex. Relaxed ordering
    // is sufficient: the name is the only datum published and no context
    // ever uses another context's VAO.
    mutable std::array<std::atomic<unsigned int>, gl_context_slot_count> m_gl_names{};
    mutable std::mutex                                                   m_adoption_mutex;
};

} // namespace erhe::graphics
