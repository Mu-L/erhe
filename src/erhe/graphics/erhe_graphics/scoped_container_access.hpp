#pragma once

namespace erhe::graphics {

class Device;
class Render_pass;
class Vertex_input_state;

// Per-object access to GL container objects (VAOs, framebuffers), which are
// NOT shared between contexts: constructing an accessor ensures the object
// exists on the CALLING THREAD'S CURRENT CONTEXT and yields its name(s).
// This is the only sanctioned creation point for a container object on a
// context - gl_name() on the impls is a getter, never an adoption point.
// Cheap and idempotent after the first use on a given context (a relaxed
// own-slot atomic load); asserts if no context is current. No-ops on every
// backend but GL, where container objects do not exist as a concept.
//
// Both accessors take CONST references: every adoption site reaches the
// object only through a const pointer, and adoption mutates only the
// per-context slot, never the logical object.
//
// Object lifetime is not covered: the accessor binds a reference to a live
// object, and destroying the object while an accessor on it is in scope is
// a use-after-free like any other.

class Scoped_vertex_input_state final
{
public:
    Scoped_vertex_input_state(Device& device, const Vertex_input_state& vertex_input_state);
    ~Scoped_vertex_input_state() noexcept;
    Scoped_vertex_input_state(const Scoped_vertex_input_state&) = delete;
    void operator=            (const Scoped_vertex_input_state&) = delete;
    Scoped_vertex_input_state(Scoped_vertex_input_state&&)      = delete;
    void operator=            (Scoped_vertex_input_state&&)      = delete;

    [[nodiscard]] auto gl_name() const -> unsigned int;

private:
    unsigned int m_gl_name{0};
};

class Scoped_framebuffer final
{
public:
    Scoped_framebuffer(Device& device, const Render_pass& render_pass);
    ~Scoped_framebuffer() noexcept;
    Scoped_framebuffer(const Scoped_framebuffer&) = delete;
    void operator=     (const Scoped_framebuffer&) = delete;
    Scoped_framebuffer(Scoped_framebuffer&&)      = delete;
    void operator=     (Scoped_framebuffer&&)      = delete;

    [[nodiscard]] auto gl_name                    () const -> unsigned int;
    [[nodiscard]] auto gl_multisample_resolve_name() const -> unsigned int;

private:
    unsigned int m_gl_name                    {0};
    unsigned int m_gl_multisample_resolve_name{0};
};

} // namespace erhe::graphics
