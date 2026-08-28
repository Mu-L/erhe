#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_verify/verify.hpp"

#include <new>
#include <utility>

namespace erhe::graphics {

// Gl_texture

Gl_texture::Gl_texture(GLuint gl_name, bool owned, Device_impl* device_impl)
    : m_device_impl{device_impl}
    , m_gl_name    {gl_name}
    , m_owned      {owned}
{
}

Gl_texture::Gl_texture(Gl_texture&& old) noexcept
    : m_device_impl{std::exchange(old.m_device_impl, nullptr)}
    , m_gl_name    {std::exchange(old.m_gl_name, 0)}
    , m_owned      {std::exchange(old.m_owned, false)}
{
}

auto Gl_texture::operator=(Gl_texture&& old) noexcept -> Gl_texture&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_texture();
    return *new (this) Gl_texture(std::move(old));
}

Gl_texture::~Gl_texture() noexcept
{
    if (m_owned && (m_gl_name != 0)) {
        if (m_device_impl != nullptr) {
            m_device_impl->on_shared_object_deleted(Gl_shared_object_kind::texture, m_gl_name);
        }
        gl::delete_textures(1, &m_gl_name);
    }
}

auto Gl_texture::gl_name() const -> GLuint
{
    return m_gl_name;
}

// Gl_program

Gl_program::Gl_program(GLuint gl_name, Device_impl* device_impl)
    : m_device_impl{device_impl}
    , m_gl_name    {gl_name}
{
}

Gl_program::Gl_program(Gl_program&& old) noexcept
    : m_device_impl{std::exchange(old.m_device_impl, nullptr)}
    , m_gl_name    {std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_program::operator=(Gl_program&& old) noexcept -> Gl_program&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_program();
    return *new (this) Gl_program(std::move(old));
}

Gl_program::~Gl_program() noexcept
{
    if (m_gl_name != 0) {
        if (m_device_impl != nullptr) {
            m_device_impl->on_shared_object_deleted(Gl_shared_object_kind::program, m_gl_name);
        }
        gl::delete_program(m_gl_name);
    }
}

auto Gl_program::gl_name() const -> GLuint
{
    return m_gl_name;
}

// Gl_shader

Gl_shader::Gl_shader(GLuint gl_name)
    : m_gl_name{gl_name}
{
}

Gl_shader::~Gl_shader() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_shader(m_gl_name);
    }
}

Gl_shader::Gl_shader(Gl_shader&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_shader::operator=(Gl_shader&& old) noexcept -> Gl_shader&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_shader();
    return *new (this) Gl_shader(std::move(old));
}

auto Gl_shader::gl_name() const -> unsigned int
{
    return m_gl_name;
}

// Gl_sampler

Gl_sampler::Gl_sampler(GLuint gl_name, Device_impl* device_impl)
    : m_device_impl{device_impl}
    , m_gl_name    {gl_name}
{
}

Gl_sampler::~Gl_sampler() noexcept
{
    if (m_gl_name != 0) {
        if (m_device_impl != nullptr) {
            m_device_impl->on_shared_object_deleted(Gl_shared_object_kind::sampler, m_gl_name);
        }
        gl::delete_samplers(1, &m_gl_name);
    }
}

Gl_sampler::Gl_sampler(Gl_sampler&& old) noexcept
    : m_device_impl{std::exchange(old.m_device_impl, nullptr)}
    , m_gl_name    {std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_sampler::operator=(Gl_sampler&& old) noexcept -> Gl_sampler&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_sampler();
    return *new (this) Gl_sampler(std::move(old));
}

auto Gl_sampler::gl_name() const -> unsigned int
{
    return m_gl_name;
}

// Gl_renderbuffer

Gl_renderbuffer::Gl_renderbuffer(GLuint gl_name, Device_impl* device_impl)
    : m_device_impl{device_impl}
    , m_gl_name    {gl_name}
{
}

Gl_renderbuffer::~Gl_renderbuffer() noexcept
{
    if (m_gl_name != 0) {
        if (m_device_impl != nullptr) {
            m_device_impl->on_shared_object_deleted(Gl_shared_object_kind::renderbuffer, m_gl_name);
        }
        gl::delete_renderbuffers(1, &m_gl_name);
    }
}

Gl_renderbuffer::Gl_renderbuffer(Gl_renderbuffer&& old) noexcept
    : m_device_impl{std::exchange(old.m_device_impl, nullptr)}
    , m_gl_name    {std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_renderbuffer::operator=(Gl_renderbuffer&& old) noexcept -> Gl_renderbuffer&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_renderbuffer();
    return *new (this) Gl_renderbuffer(std::move(old));
}

auto Gl_renderbuffer::gl_name() const -> GLuint
{
    return m_gl_name;
}

// Gl_buffer

Gl_buffer::Gl_buffer(GLuint gl_name, Device_impl* device_impl)
    : m_device_impl{device_impl}
    , m_gl_name    {gl_name}
{
}

Gl_buffer::~Gl_buffer() noexcept
{
    if (m_gl_name != 0) {
        if (m_device_impl != nullptr) {
            m_device_impl->on_shared_object_deleted(Gl_shared_object_kind::buffer, m_gl_name);
        }
        gl::delete_buffers(1, &m_gl_name);
    }
}

Gl_buffer::Gl_buffer(Gl_buffer&& old) noexcept
    : m_device_impl{std::exchange(old.m_device_impl, nullptr)}
    , m_gl_name    {std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_buffer::operator=(Gl_buffer&& old) noexcept -> Gl_buffer&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_buffer();
    return *new (this) Gl_buffer(std::move(old));
}

auto Gl_buffer::gl_name() const -> GLuint
{
    return m_gl_name;
}

// Gl_publication_sync

Gl_publication_sync::~Gl_publication_sync() noexcept
{
    if (m_sync != nullptr) {
        gl::delete_sync(static_cast<GLsync>(m_sync));
    }
}

Gl_publication_sync::Gl_publication_sync(Gl_publication_sync&& old) noexcept
    : m_sync{std::exchange(old.m_sync, nullptr)}
{
}

auto Gl_publication_sync::operator=(Gl_publication_sync&& old) noexcept -> Gl_publication_sync&
{
    if (&old == this) {
        return *this;
    }
    if (m_sync != nullptr) {
        gl::delete_sync(static_cast<GLsync>(m_sync));
    }
    m_sync = std::exchange(old.m_sync, nullptr);
    return *this;
}

void Gl_publication_sync::publish_from_worker()
{
    if (m_sync != nullptr) {
        gl::delete_sync(static_cast<GLsync>(m_sync));
    }
    m_sync = gl::fence_sync(gl::Sync_condition::sync_gpu_commands_complete, 0);
    // The flush is what submits the fence: glWaitSync does not flush the
    // producing context, so an unflushed fence may never reach the GL
    // server and the wait would be indefinite.
    gl::flush();
}

void Gl_publication_sync::wait_and_consume()
{
    if (m_sync == nullptr) {
        return;
    }
    // Server-side wait, no CPU stall. One wait orders every subsequent
    // command of this context after the worker's publication, so this is
    // once per object, at the first main-context touch.
    gl::wait_sync(static_cast<GLsync>(m_sync), 0, GL_TIMEOUT_IGNORED);
    gl::delete_sync(static_cast<GLsync>(m_sync));
    m_sync = nullptr;
}

// Gl_query

Gl_query::Gl_query(GLuint gl_name)
    : m_gl_name{gl_name}
{
}

Gl_query::~Gl_query() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_queries(1, &m_gl_name);
    }
}

Gl_query::Gl_query(Gl_query&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_query::operator=(Gl_query&& old) noexcept -> Gl_query&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_query();
    return *new (this) Gl_query(std::move(old));
}

auto Gl_query::gl_name() const -> GLuint
{
    return m_gl_name;
}

} // namespace erhe::graphics
