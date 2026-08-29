#include "erhe_graphics/gl/gl_binding_state.hpp"
#include "erhe_graphics/gl/gl_context_index.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

#include <atomic>
#include <utility>

namespace erhe::graphics {

// -- RAII Guards --------------------------------------------------------------

Buffer_binding_guard::Buffer_binding_guard(Gl_binding_state& state, gl::Buffer_target target)
    : m_state {&state}
    , m_target{target}
{
}

Buffer_binding_guard::~Buffer_binding_guard() noexcept
{
    if (m_state != nullptr) {
        m_state->pop_buffer(m_target);
    }
}

Buffer_binding_guard::Buffer_binding_guard(Buffer_binding_guard&& other) noexcept
    : m_state {std::exchange(other.m_state, nullptr)}
    , m_target{other.m_target}
{
}

Texture_binding_guard::Texture_binding_guard(Gl_binding_state& state, GLuint unit, gl::Texture_target target)
    : m_state {&state}
    , m_unit  {unit}
    , m_target{target}
{
}

Texture_binding_guard::~Texture_binding_guard() noexcept
{
    if (m_state != nullptr) {
        m_state->pop_texture(m_unit, m_target);
    }
}

Texture_binding_guard::Texture_binding_guard(Texture_binding_guard&& other) noexcept
    : m_state {std::exchange(other.m_state, nullptr)}
    , m_unit  {other.m_unit}
    , m_target{other.m_target}
{
}

Framebuffer_binding_guard::Framebuffer_binding_guard(Gl_binding_state& state, gl::Framebuffer_target target)
    : m_state {&state}
    , m_target{target}
{
}

Framebuffer_binding_guard::~Framebuffer_binding_guard() noexcept
{
    if (m_state != nullptr) {
        m_state->pop_framebuffer(m_target);
    }
}

Framebuffer_binding_guard::Framebuffer_binding_guard(Framebuffer_binding_guard&& other) noexcept
    : m_state {std::exchange(other.m_state, nullptr)}
    , m_target{other.m_target}
{
}

Renderbuffer_binding_guard::Renderbuffer_binding_guard(Gl_binding_state& state)
    : m_state{&state}
{
}

Renderbuffer_binding_guard::~Renderbuffer_binding_guard() noexcept
{
    if (m_state != nullptr) {
        m_state->pop_renderbuffer();
    }
}

Renderbuffer_binding_guard::Renderbuffer_binding_guard(Renderbuffer_binding_guard&& other) noexcept
    : m_state{std::exchange(other.m_state, nullptr)}
{
}

Vertex_array_binding_guard::Vertex_array_binding_guard(Gl_binding_state& state)
    : m_state{&state}
{
}

Vertex_array_binding_guard::~Vertex_array_binding_guard() noexcept
{
    if (m_state != nullptr) {
        m_state->pop_vertex_array();
    }
}

Vertex_array_binding_guard::Vertex_array_binding_guard(Vertex_array_binding_guard&& other) noexcept
    : m_state{std::exchange(other.m_state, nullptr)}
{
}

// -- Gl_binding_state ---------------------------------------------------------

Gl_binding_state::Gl_binding_state()
{
    reset();
}

void Gl_binding_state::reset()
{
    // Slot epochs reset with the slots: epoch 0 always reads as stale,
    // matching a slot that holds no binding.
    m_bound_buffer_epochs.fill(0);
    for (auto& unit_epochs : m_bound_texture_epochs) {
        unit_epochs.fill(0);
    }
    m_bound_sampler_epochs.fill(0);
    m_renderbuffer_epoch = 0;
    m_program_epoch      = 0;

    m_bound_buffers.fill(0);
    for (auto& stack : m_buffer_stack) {
        stack.clear();
    }
    m_active_texture_unit = 0;
    for (auto& unit_textures : m_bound_textures) {
        unit_textures.fill(0);
    }
    for (auto& unit_stacks : m_texture_stack) {
        for (auto& stack : unit_stacks) {
            stack.clear();
        }
    }
    m_bound_samplers.fill(0);
    m_draw_framebuffer = 0;
    m_read_framebuffer = 0;
    m_draw_framebuffer_stack.clear();
    m_read_framebuffer_stack.clear();
    m_bound_renderbuffer = 0;
    m_renderbuffer_stack.clear();
    m_bound_vertex_array = 0;
    m_vertex_array_stack.clear();
    m_current_program = 0;
}

// -- Buffer target index mapping ----------------------------------------------

auto Gl_binding_state::buffer_target_to_index(const gl::Buffer_target target) const -> std::size_t
{
    switch (target) {
        case gl::Buffer_target::array_buffer:              return 0;
        case gl::Buffer_target::copy_read_buffer:          return 1;
        case gl::Buffer_target::copy_write_buffer:         return 2;
        case gl::Buffer_target::draw_indirect_buffer:      return 3;
        case gl::Buffer_target::pixel_pack_buffer:         return 4;
        case gl::Buffer_target::pixel_unpack_buffer:       return 5;
        case gl::Buffer_target::texture_buffer:            return 6;
        case gl::Buffer_target::transform_feedback_buffer: return 7;
        case gl::Buffer_target::uniform_buffer:            return 8;
        case gl::Buffer_target::shader_storage_buffer:     return 9;
        case gl::Buffer_target::atomic_counter_buffer:     return 10;
        default: ERHE_FATAL("Unknown buffer target"); return 0;
    }
}

// -- Texture target index mapping ---------------------------------------------

auto Gl_binding_state::texture_target_to_index(const gl::Texture_target target) const -> std::size_t
{
    switch (target) {
        case gl::Texture_target::texture_1d:                   return 0;
        case gl::Texture_target::texture_2d:                   return 1;
        case gl::Texture_target::texture_3d:                   return 2;
        case gl::Texture_target::texture_1d_array:             return 3;
        case gl::Texture_target::texture_2d_array:             return 4;
        case gl::Texture_target::texture_cube_map:             return 5;
        case gl::Texture_target::texture_cube_map_array:       return 6;
        case gl::Texture_target::texture_2d_multisample:       return 7;
        case gl::Texture_target::texture_2d_multisample_array: return 8;
        case gl::Texture_target::texture_buffer:               return 9;
        default: ERHE_FATAL("Unknown texture target"); return 0;
    }
}

// -- Buffers ------------------------------------------------------------------

auto Gl_binding_state::push_buffer(const gl::Buffer_target target, const GLuint buffer) -> Buffer_binding_guard
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(target != gl::Buffer_target::element_array_buffer);
    const std::size_t index = buffer_target_to_index(target);
    m_buffer_stack[index].push_back(m_bound_buffers[index]);
    bind_buffer(target, buffer);
    return Buffer_binding_guard{*this, target};
}

void Gl_binding_state::pop_buffer(const gl::Buffer_target target)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(target != gl::Buffer_target::element_array_buffer);
    const std::size_t index = buffer_target_to_index(target);
    ERHE_VERIFY(!m_buffer_stack[index].empty());
    const GLuint old_buffer = m_buffer_stack[index].back();
    m_buffer_stack[index].pop_back();
    bind_buffer(target, old_buffer);
}

void Gl_binding_state::bind_buffer(const gl::Buffer_target target, const GLuint buffer)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    // element_array_buffer binding is part of VAO state, not global state.
    // We do not track it here; let it pass through directly to GL.
    if (target == gl::Buffer_target::element_array_buffer) {
        gl::bind_buffer(target, buffer);
        return;
    }
    const std::size_t index = buffer_target_to_index(target);
    if (m_bound_buffers[index] != buffer) {
        m_bound_buffers[index]       = buffer;
        m_bound_buffer_epochs[index] = bump_bind_epoch();
        gl::bind_buffer(target, buffer);
    }
}

auto Gl_binding_state::get_bound_buffer(const gl::Buffer_target target) const -> GLuint
{
    ERHE_VERIFY(target != gl::Buffer_target::element_array_buffer);
    return m_bound_buffers[buffer_target_to_index(target)];
}

// -- Textures -----------------------------------------------------------------

auto Gl_binding_state::push_texture(const GLuint unit, const gl::Texture_target target, const GLuint texture) -> Texture_binding_guard
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(unit < s_max_texture_units);
    const std::size_t target_index = texture_target_to_index(target);
    m_texture_stack[unit][target_index].push_back(m_bound_textures[unit][target_index]);
    bind_texture(unit, target, texture);
    return Texture_binding_guard{*this, unit, target};
}

void Gl_binding_state::pop_texture(const GLuint unit, const gl::Texture_target target)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(unit < s_max_texture_units);
    const std::size_t target_index = texture_target_to_index(target);
    ERHE_VERIFY(!m_texture_stack[unit][target_index].empty());
    const GLuint old_texture = m_texture_stack[unit][target_index].back();
    m_texture_stack[unit][target_index].pop_back();
    bind_texture(unit, target, old_texture);
}

void Gl_binding_state::bind_texture(const GLuint unit, const gl::Texture_target target, const GLuint texture)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(unit < s_max_texture_units);
    const std::size_t target_index = texture_target_to_index(target);
    if (m_bound_textures[unit][target_index] != texture) {
        if (m_active_texture_unit != unit) {
            m_active_texture_unit = unit;
            gl::active_texture(gl::Texture_unit{static_cast<unsigned int>(gl::Texture_unit::texture0) + unit});
        }
        m_bound_textures[unit][target_index]       = texture;
        m_bound_texture_epochs[unit][target_index] = bump_bind_epoch();
        gl::bind_texture(target, texture);
    }
}

auto Gl_binding_state::get_bound_texture(const GLuint unit, const gl::Texture_target target) const -> GLuint
{
    ERHE_VERIFY(unit < s_max_texture_units);
    return m_bound_textures[unit][texture_target_to_index(target)];
}

// -- Framebuffers -------------------------------------------------------------

auto Gl_binding_state::push_framebuffer(const gl::Framebuffer_target target, const GLuint framebuffer) -> Framebuffer_binding_guard
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    switch (target) {
        case gl::Framebuffer_target::draw_framebuffer: {
            m_draw_framebuffer_stack.push_back(m_draw_framebuffer);
            break;
        }
        case gl::Framebuffer_target::read_framebuffer: {
            m_read_framebuffer_stack.push_back(m_read_framebuffer);
            break;
        }
        case gl::Framebuffer_target::framebuffer: {
            m_draw_framebuffer_stack.push_back(m_draw_framebuffer);
            m_read_framebuffer_stack.push_back(m_read_framebuffer);
            break;
        }
    }
    bind_framebuffer(target, framebuffer);
    return Framebuffer_binding_guard{*this, target};
}

void Gl_binding_state::pop_framebuffer(const gl::Framebuffer_target target)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    switch (target) {
        case gl::Framebuffer_target::draw_framebuffer: {
            ERHE_VERIFY(!m_draw_framebuffer_stack.empty());
            const GLuint old = m_draw_framebuffer_stack.back();
            m_draw_framebuffer_stack.pop_back();
            bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, old);
            break;
        }
        case gl::Framebuffer_target::read_framebuffer: {
            ERHE_VERIFY(!m_read_framebuffer_stack.empty());
            const GLuint old = m_read_framebuffer_stack.back();
            m_read_framebuffer_stack.pop_back();
            bind_framebuffer(gl::Framebuffer_target::read_framebuffer, old);
            break;
        }
        case gl::Framebuffer_target::framebuffer: {
            ERHE_VERIFY(!m_draw_framebuffer_stack.empty());
            ERHE_VERIFY(!m_read_framebuffer_stack.empty());
            const GLuint old_draw = m_draw_framebuffer_stack.back();
            const GLuint old_read = m_read_framebuffer_stack.back();
            m_draw_framebuffer_stack.pop_back();
            m_read_framebuffer_stack.pop_back();
            if (old_draw == old_read) {
                bind_framebuffer(gl::Framebuffer_target::framebuffer, old_draw);
            } else {
                bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, old_draw);
                bind_framebuffer(gl::Framebuffer_target::read_framebuffer, old_read);
            }
            break;
        }
    }
}

void Gl_binding_state::bind_framebuffer(const gl::Framebuffer_target target, const GLuint framebuffer)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    switch (target) {
        case gl::Framebuffer_target::draw_framebuffer: {
            if (m_draw_framebuffer != framebuffer) {
                m_draw_framebuffer = framebuffer;
                gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer);
            }
            break;
        }
        case gl::Framebuffer_target::read_framebuffer: {
            if (m_read_framebuffer != framebuffer) {
                m_read_framebuffer = framebuffer;
                gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, framebuffer);
            }
            break;
        }
        case gl::Framebuffer_target::framebuffer: {
            const bool draw_changed = (m_draw_framebuffer != framebuffer);
            const bool read_changed = (m_read_framebuffer != framebuffer);
            m_draw_framebuffer = framebuffer;
            m_read_framebuffer = framebuffer;
            if (draw_changed || read_changed) {
                gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, framebuffer);
            }
            break;
        }
    }
}

auto Gl_binding_state::get_draw_framebuffer() const -> GLuint
{
    return m_draw_framebuffer;
}

auto Gl_binding_state::get_read_framebuffer() const -> GLuint
{
    return m_read_framebuffer;
}

// -- Renderbuffers ------------------------------------------------------------

auto Gl_binding_state::push_renderbuffer(const GLuint renderbuffer) -> Renderbuffer_binding_guard
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    m_renderbuffer_stack.push_back(m_bound_renderbuffer);
    bind_renderbuffer(renderbuffer);
    return Renderbuffer_binding_guard{*this};
}

void Gl_binding_state::pop_renderbuffer()
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(!m_renderbuffer_stack.empty());
    const GLuint old = m_renderbuffer_stack.back();
    m_renderbuffer_stack.pop_back();
    bind_renderbuffer(old);
}

void Gl_binding_state::bind_renderbuffer(const GLuint renderbuffer)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    if (m_bound_renderbuffer != renderbuffer) {
        m_bound_renderbuffer = renderbuffer;
        m_renderbuffer_epoch = bump_bind_epoch();
        gl::bind_renderbuffer(gl::Renderbuffer_target::renderbuffer, renderbuffer);
    }
}

auto Gl_binding_state::get_bound_renderbuffer() const -> GLuint
{
    return m_bound_renderbuffer;
}

// -- Vertex Arrays ------------------------------------------------------------

auto Gl_binding_state::push_vertex_array(const GLuint vao) -> Vertex_array_binding_guard
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    m_vertex_array_stack.push_back(m_bound_vertex_array);
    bind_vertex_array(vao);
    return Vertex_array_binding_guard{*this};
}

void Gl_binding_state::pop_vertex_array()
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(!m_vertex_array_stack.empty());
    const GLuint old = m_vertex_array_stack.back();
    m_vertex_array_stack.pop_back();
    bind_vertex_array(old);
}

void Gl_binding_state::bind_vertex_array(const GLuint vao)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    if (m_bound_vertex_array != vao) {
        m_bound_vertex_array = vao;
        gl::bind_vertex_array(vao);
    }
}

auto Gl_binding_state::get_bound_vertex_array() const -> GLuint
{
    return m_bound_vertex_array;
}

// -- Samplers -----------------------------------------------------------------

void Gl_binding_state::bind_sampler(const GLuint unit, const GLuint sampler)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(unit < s_max_texture_units);
    if (m_bound_samplers[unit] != sampler) {
        m_bound_samplers[unit]       = sampler;
        m_bound_sampler_epochs[unit] = bump_bind_epoch();
        gl::bind_sampler(unit, sampler);
    }
}

auto Gl_binding_state::get_bound_sampler(const GLuint unit) const -> GLuint
{
    ERHE_VERIFY(unit < s_max_texture_units);
    return m_bound_samplers[unit];
}

// -- Programs -----------------------------------------------------------------

void Gl_binding_state::use_program(const GLuint program)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    if (m_current_program != program) {
        m_current_program = program;
        m_program_epoch   = bump_bind_epoch();
        gl::use_program(program);
    }
}

auto Gl_binding_state::get_current_program() const -> GLuint
{
    return m_current_program;
}

// -- Object deletion hooks ---------------------------------------------------
//
// The hooks run on the DELETING context's binding state (per-context since
// commit 10), so they are cache-only edits on state the calling thread
// owns; the cross-context scrub queue covers every other context.
//
// A helper that scrubs a single-slot value.
namespace {

void scrub(GLuint& slot, GLuint deleted)
{
    if (slot == deleted) {
        slot = 0;
    }
}

void scrub(std::vector<GLuint>& stack, GLuint deleted)
{
    for (GLuint& slot : stack) {
        if (slot == deleted) {
            slot = 0;
        }
    }
}

} // anonymous namespace

void Gl_binding_state::on_texture_deleted(const GLuint texture)
{
    if (texture == 0) {
        return;
    }
    for (auto& unit_textures : m_bound_textures) {
        for (GLuint& slot : unit_textures) {
            scrub(slot, texture);
        }
    }
    for (auto& unit_stacks : m_texture_stack) {
        for (std::vector<GLuint>& stack : unit_stacks) {
            scrub(stack, texture);
        }
    }
}

void Gl_binding_state::on_buffer_deleted(const GLuint buffer)
{
    if (buffer == 0) {
        return;
    }
    for (GLuint& slot : m_bound_buffers) {
        scrub(slot, buffer);
    }
    for (std::vector<GLuint>& stack : m_buffer_stack) {
        scrub(stack, buffer);
    }
}

void Gl_binding_state::on_sampler_deleted(const GLuint sampler)
{
    if (sampler == 0) {
        return;
    }
    for (GLuint& slot : m_bound_samplers) {
        scrub(slot, sampler);
    }
}

void Gl_binding_state::on_framebuffer_deleted(const GLuint framebuffer)
{
    if (framebuffer == 0) {
        return;
    }
    scrub(m_draw_framebuffer, framebuffer);
    scrub(m_read_framebuffer, framebuffer);
    scrub(m_draw_framebuffer_stack, framebuffer);
    scrub(m_read_framebuffer_stack, framebuffer);
}

void Gl_binding_state::on_renderbuffer_deleted(const GLuint renderbuffer)
{
    if (renderbuffer == 0) {
        return;
    }
    scrub(m_bound_renderbuffer, renderbuffer);
    scrub(m_renderbuffer_stack, renderbuffer);
}

void Gl_binding_state::on_vertex_array_deleted(const GLuint vertex_array)
{
    if (vertex_array == 0) {
        return;
    }
    scrub(m_bound_vertex_array, vertex_array);
    scrub(m_vertex_array_stack, vertex_array);
}

void Gl_binding_state::on_program_deleted(const GLuint program)
{
    if (program == 0) {
        return;
    }
    scrub(m_current_program, program);
}

// -- Deferred cross-context scrub ---------------------------------------------
//
// See the header comment: these run on the owning context's thread, for a
// shared object some OTHER context deleted. They must issue real GL unbinds
// (GL auto-unbinds only in the deleting context) and must leave alone any
// slot rebound after the entry was enqueued (the name may have been
// recycled for a new, live object). The stale check is
// slot_epoch <= enqueue_epoch: the slot was last bound before the delete
// was enqueued, so the name it holds is the deleted object's.
//
// Stack entries (push/pop guard save slots) are scrubbed to 0 without an
// epoch check: they are not bound, so no GL call is involved, and a guard
// can not be alive across a drain point on its own thread.

namespace {

auto buffer_target_from_index(const std::size_t index) -> gl::Buffer_target
{
    // Inverse of Gl_binding_state::buffer_target_to_index().
    constexpr gl::Buffer_target targets[] = {
        gl::Buffer_target::array_buffer,
        gl::Buffer_target::copy_read_buffer,
        gl::Buffer_target::copy_write_buffer,
        gl::Buffer_target::draw_indirect_buffer,
        gl::Buffer_target::pixel_pack_buffer,
        gl::Buffer_target::pixel_unpack_buffer,
        gl::Buffer_target::texture_buffer,
        gl::Buffer_target::transform_feedback_buffer,
        gl::Buffer_target::uniform_buffer,
        gl::Buffer_target::shader_storage_buffer,
        gl::Buffer_target::atomic_counter_buffer
    };
    ERHE_VERIFY(index < sizeof(targets) / sizeof(targets[0]));
    return targets[index];
}

auto texture_target_from_index(const std::size_t index) -> gl::Texture_target
{
    // Inverse of Gl_binding_state::texture_target_to_index().
    constexpr gl::Texture_target targets[] = {
        gl::Texture_target::texture_1d,
        gl::Texture_target::texture_2d,
        gl::Texture_target::texture_3d,
        gl::Texture_target::texture_1d_array,
        gl::Texture_target::texture_2d_array,
        gl::Texture_target::texture_cube_map,
        gl::Texture_target::texture_cube_map_array,
        gl::Texture_target::texture_2d_multisample,
        gl::Texture_target::texture_2d_multisample_array,
        gl::Texture_target::texture_buffer
    };
    ERHE_VERIFY(index < sizeof(targets) / sizeof(targets[0]));
    return targets[index];
}

} // anonymous namespace

auto Gl_binding_state::bump_bind_epoch() -> uint64_t
{
    return m_bind_epoch.fetch_add(1, std::memory_order_relaxed) + 1;
}

auto Gl_binding_state::get_bind_epoch() const -> uint64_t
{
    return m_bind_epoch.load(std::memory_order_acquire);
}

void Gl_binding_state::scrub_deleted_buffer(const GLuint name, const uint64_t enqueue_epoch)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(name != 0);
    for (std::size_t index = 0; index < s_buffer_target_count; ++index) {
        if ((m_bound_buffers[index] == name) && (m_bound_buffer_epochs[index] <= enqueue_epoch)) {
            bind_buffer(buffer_target_from_index(index), 0);
        }
    }
    for (std::vector<GLuint>& stack : m_buffer_stack) {
        scrub(stack, name);
    }
}

void Gl_binding_state::scrub_deleted_texture(const GLuint name, const uint64_t enqueue_epoch)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(name != 0);
    for (std::size_t unit = 0; unit < s_max_texture_units; ++unit) {
        for (std::size_t target_index = 0; target_index < s_texture_target_count; ++target_index) {
            if (
                (m_bound_textures[unit][target_index] == name) &&
                (m_bound_texture_epochs[unit][target_index] <= enqueue_epoch)
            ) {
                bind_texture(static_cast<GLuint>(unit), texture_target_from_index(target_index), 0);
            }
        }
    }
    for (auto& unit_stacks : m_texture_stack) {
        for (std::vector<GLuint>& stack : unit_stacks) {
            scrub(stack, name);
        }
    }
}

void Gl_binding_state::scrub_deleted_sampler(const GLuint name, const uint64_t enqueue_epoch)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(name != 0);
    for (std::size_t unit = 0; unit < s_max_texture_units; ++unit) {
        if ((m_bound_samplers[unit] == name) && (m_bound_sampler_epochs[unit] <= enqueue_epoch)) {
            bind_sampler(static_cast<GLuint>(unit), 0);
        }
    }
}

void Gl_binding_state::scrub_deleted_renderbuffer(const GLuint name, const uint64_t enqueue_epoch)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(name != 0);
    if ((m_bound_renderbuffer == name) && (m_renderbuffer_epoch <= enqueue_epoch)) {
        bind_renderbuffer(0);
    }
    scrub(m_renderbuffer_stack, name);
}

void Gl_binding_state::scrub_deleted_program(const GLuint name, const uint64_t enqueue_epoch)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(name != 0);
    if ((m_current_program == name) && (m_program_epoch <= enqueue_epoch)) {
        use_program(0);
    }
}

} // namespace erhe::graphics
