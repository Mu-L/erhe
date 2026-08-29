#pragma once

#include "erhe_gl/wrapper_enums.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

typedef unsigned int GLuint;

namespace erhe::graphics {

class Gl_binding_state;

// RAII scope guards - returned by push_* methods, pop on destruction.

class Buffer_binding_guard final
{
public:
    Buffer_binding_guard(Gl_binding_state& state, gl::Buffer_target target);
    ~Buffer_binding_guard() noexcept;
    Buffer_binding_guard(const Buffer_binding_guard&) = delete;
    void operator=(const Buffer_binding_guard&) = delete;
    Buffer_binding_guard(Buffer_binding_guard&& other) noexcept;
    void operator=(Buffer_binding_guard&&) = delete;

private:
    Gl_binding_state* m_state;
    gl::Buffer_target m_target;
};

class Texture_binding_guard final
{
public:
    Texture_binding_guard(Gl_binding_state& state, GLuint unit, gl::Texture_target target);
    ~Texture_binding_guard() noexcept;
    Texture_binding_guard(const Texture_binding_guard&) = delete;
    void operator=(const Texture_binding_guard&) = delete;
    Texture_binding_guard(Texture_binding_guard&& other) noexcept;
    void operator=(Texture_binding_guard&&) = delete;

private:
    Gl_binding_state*  m_state;
    GLuint             m_unit;
    gl::Texture_target m_target;
};

class Framebuffer_binding_guard final
{
public:
    Framebuffer_binding_guard(Gl_binding_state& state, gl::Framebuffer_target target);
    ~Framebuffer_binding_guard() noexcept;
    Framebuffer_binding_guard(const Framebuffer_binding_guard&) = delete;
    void operator=(const Framebuffer_binding_guard&) = delete;
    Framebuffer_binding_guard(Framebuffer_binding_guard&& other) noexcept;
    void operator=(Framebuffer_binding_guard&&) = delete;

private:
    Gl_binding_state*      m_state;
    gl::Framebuffer_target m_target;
};

class Renderbuffer_binding_guard final
{
public:
    explicit Renderbuffer_binding_guard(Gl_binding_state& state);
    ~Renderbuffer_binding_guard() noexcept;
    Renderbuffer_binding_guard(const Renderbuffer_binding_guard&) = delete;
    void operator=(const Renderbuffer_binding_guard&) = delete;
    Renderbuffer_binding_guard(Renderbuffer_binding_guard&& other) noexcept;
    void operator=(Renderbuffer_binding_guard&&) = delete;

private:
    Gl_binding_state* m_state;
};

class Vertex_array_binding_guard final
{
public:
    explicit Vertex_array_binding_guard(Gl_binding_state& state);
    ~Vertex_array_binding_guard() noexcept;
    Vertex_array_binding_guard(const Vertex_array_binding_guard&) = delete;
    void operator=(const Vertex_array_binding_guard&) = delete;
    Vertex_array_binding_guard(Vertex_array_binding_guard&& other) noexcept;
    void operator=(Vertex_array_binding_guard&&) = delete;

private:
    Gl_binding_state* m_state;
};

// Shadows OpenGL binding state to avoid querying GL and to allow
// save/restore when DSA emulation needs to temporarily bind objects.
//
// Push methods save the current binding onto a stack, bind the new value,
// and return an RAII guard that pops the stack (restoring the old binding)
// on destruction.

class Gl_binding_state
{
public:
    Gl_binding_state();

    void reset();

    // -- Buffers ----------------------------------------------------------
    auto push_buffer        (gl::Buffer_target target, GLuint buffer) -> Buffer_binding_guard;
    void pop_buffer         (gl::Buffer_target target);
    void bind_buffer        (gl::Buffer_target target, GLuint buffer);
    auto get_bound_buffer   (gl::Buffer_target target) const -> GLuint;

    // -- Textures ---------------------------------------------------------
    auto push_texture       (GLuint unit, gl::Texture_target target, GLuint texture) -> Texture_binding_guard;
    void pop_texture        (GLuint unit, gl::Texture_target target);
    void bind_texture       (GLuint unit, gl::Texture_target target, GLuint texture);
    auto get_bound_texture  (GLuint unit, gl::Texture_target target) const -> GLuint;

    // -- Framebuffers -----------------------------------------------------
    auto push_framebuffer   (gl::Framebuffer_target target, GLuint framebuffer) -> Framebuffer_binding_guard;
    void pop_framebuffer    (gl::Framebuffer_target target);
    void bind_framebuffer   (gl::Framebuffer_target target, GLuint framebuffer);
    auto get_draw_framebuffer() const -> GLuint;
    auto get_read_framebuffer() const -> GLuint;

    // -- Renderbuffers ----------------------------------------------------
    auto push_renderbuffer  (GLuint renderbuffer) -> Renderbuffer_binding_guard;
    void pop_renderbuffer   ();
    void bind_renderbuffer  (GLuint renderbuffer);
    auto get_bound_renderbuffer() const -> GLuint;

    // -- Vertex Arrays ----------------------------------------------------
    auto push_vertex_array  (GLuint vao) -> Vertex_array_binding_guard;
    void pop_vertex_array   ();
    void bind_vertex_array  (GLuint vao);
    auto get_bound_vertex_array() const -> GLuint;

    // -- Samplers ---------------------------------------------------------
    void bind_sampler       (GLuint unit, GLuint sampler);
    auto get_bound_sampler  (GLuint unit) const -> GLuint;

    // -- Programs ---------------------------------------------------------
    void use_program        (GLuint program);
    auto get_current_program() const -> GLuint;

    // -- Object deletion hooks -------------------------------------------
    //
    // Called on the DELETING context's binding state, right before the
    // underlying glDelete* call. GL auto-unbinds a deleted object only in
    // the context that deletes it - NOT in every context of the share
    // group - so these hooks mirror that auto-unbind in this context's
    // cached state only. Every OTHER context's cache is scrubbed later,
    // through Device_impl's deferred scrub queue, by the scrub_deleted_*
    // methods below (which must issue real GL unbinds, because the other
    // contexts' real binding points still hold the orphaned object).
    void on_texture_deleted     (GLuint texture);
    void on_buffer_deleted      (GLuint buffer);
    void on_sampler_deleted     (GLuint sampler);
    void on_framebuffer_deleted (GLuint framebuffer);
    void on_renderbuffer_deleted(GLuint renderbuffer);
    void on_vertex_array_deleted(GLuint vertex_array);
    void on_program_deleted     (GLuint program);

    // -- Deferred cross-context scrub -------------------------------------
    //
    // Drain-side handlers for shared objects deleted on ANOTHER context.
    // Must run on the thread this binding state's context is current on.
    // Each one real-unbinds (glBind* 0) every binding point that still
    // holds `name` and updates the cache to match - a cache-only edit
    // would leave the orphan bound in real GL state, and the next
    // bind(0) would be skipped as redundant.
    //
    // enqueue_epoch guards against name recycling: GL frees a deleted
    // name for reuse immediately, so this context may have bound a NEW
    // object under the same name after the delete was enqueued. A slot
    // whose last-bind epoch is newer than enqueue_epoch holds such a
    // recycled binding and is left alone.
    void scrub_deleted_buffer      (GLuint name, uint64_t enqueue_epoch);
    void scrub_deleted_texture     (GLuint name, uint64_t enqueue_epoch);
    void scrub_deleted_sampler     (GLuint name, uint64_t enqueue_epoch);
    void scrub_deleted_renderbuffer(GLuint name, uint64_t enqueue_epoch);
    void scrub_deleted_program     (GLuint name, uint64_t enqueue_epoch);

    // Snapshot of the monotonically increasing bind counter, read by the
    // DELETING thread when it enqueues a scrub entry for this context.
    [[nodiscard]] auto get_bind_epoch() const -> uint64_t;

    // Pending-scrub bracketing, called by Device_impl: the deleting thread
    // notes each scrub entry it enqueues for this context, and this
    // context's drain notes how many it processed. While the count is
    // nonzero, bind elision against the cache is UNSOUND for shared object
    // kinds - a cached name may belong to a deleted object whose name GL
    // has recycled, so an equal name is not proof the object is already
    // bound. The bind paths then bind for real (and bump the slot epoch,
    // which is what lets the later drain spare the rebound name).
    void note_scrub_enqueued();
    void note_scrubs_drained(std::size_t count);

    static constexpr std::size_t s_max_texture_units = 32;

private:
    auto buffer_target_to_index (gl::Buffer_target target) const -> std::size_t;
    auto texture_target_to_index(gl::Texture_target target) const -> std::size_t;

    // Owner thread only: advance the bind counter and return the new value
    // for storing in the bound slot's epoch.
    [[nodiscard]] auto bump_bind_epoch() -> uint64_t;

    // element_array_buffer is excluded - it is part of VAO state
    static constexpr std::size_t s_buffer_target_count  = 11;
    static constexpr std::size_t s_texture_target_count = 10;

    // Bumped on every real bind of a scrub-relevant object type (buffer,
    // texture, sampler, renderbuffer, program) and recorded per slot in the
    // m_*_epochs arrays below. Atomic only for the deleting thread's
    // get_bind_epoch() snapshot; slot epochs are touched by the owning
    // thread alone. 0 (the initial slot value) is always stale.
    std::atomic<uint64_t> m_bind_epoch{0};

    // Scrub entries queued for this context and not yet drained on it.
    // Same cross-thread pattern as m_bind_epoch: the deleting thread
    // increments (under the queue mutex), the owning thread's drain
    // decrements (under the same mutex); the owning thread's bind paths
    // read it relaxed and lock-free to decide whether elision is sound.
    std::atomic<uint32_t> m_pending_scrub_count{0};

    // True while elision against the cache is sound - see
    // note_scrub_enqueued(). Owner-thread read on every bind.
    [[nodiscard]] auto may_elide_binds() const -> bool
    {
        return m_pending_scrub_count.load(std::memory_order_relaxed) == 0;
    }

    // Per-target buffer bindings (current + stack)
    std::array<GLuint, s_buffer_target_count>              m_bound_buffers{};
    std::array<uint64_t, s_buffer_target_count>            m_bound_buffer_epochs{};
    std::array<std::vector<GLuint>, s_buffer_target_count> m_buffer_stack;

    // Active texture unit
    GLuint m_active_texture_unit{0};

    // Per-unit, per-target texture bindings (current + stack)
    std::array<std::array<GLuint, s_texture_target_count>, s_max_texture_units>              m_bound_textures{};
    std::array<std::array<uint64_t, s_texture_target_count>, s_max_texture_units>            m_bound_texture_epochs{};
    std::array<std::array<std::vector<GLuint>, s_texture_target_count>, s_max_texture_units> m_texture_stack;

    // Per-unit sampler bindings
    std::array<GLuint, s_max_texture_units>   m_bound_samplers{};
    std::array<uint64_t, s_max_texture_units> m_bound_sampler_epochs{};

    // Framebuffer bindings (current + stacks)
    GLuint              m_draw_framebuffer{0};
    GLuint              m_read_framebuffer{0};
    std::vector<GLuint> m_draw_framebuffer_stack;
    std::vector<GLuint> m_read_framebuffer_stack;

    // Renderbuffer binding (current + stack)
    GLuint              m_bound_renderbuffer{0};
    uint64_t            m_renderbuffer_epoch{0};
    std::vector<GLuint> m_renderbuffer_stack;

    // Vertex array binding (current + stack)
    GLuint              m_bound_vertex_array{0};
    std::vector<GLuint> m_vertex_array_stack;

    // Program binding
    GLuint   m_current_program{0};
    uint64_t m_program_epoch{0};
};

} // namespace erhe::graphics
