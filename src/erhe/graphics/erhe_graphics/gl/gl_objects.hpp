#pragma once

#include "erhe_gl/wrapper_enums.hpp"

typedef unsigned int GLuint;

namespace erhe::graphics {

class Device_impl;

class Gl_texture final
{
public:
    explicit Gl_texture(GLuint gl_name, bool owned = true, Device_impl* device_impl = nullptr);
    ~Gl_texture   () noexcept;
    Gl_texture    (const Gl_texture&) = delete;
    void operator=(const Gl_texture&) = delete;
    Gl_texture    (Gl_texture&& old) noexcept;
    auto operator=(Gl_texture&& old) noexcept -> Gl_texture&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    Device_impl* m_device_impl{nullptr};
    GLuint       m_gl_name    {0};
    bool         m_owned      {true};
};

class Gl_program final
{
public:
    Gl_program() = default;
    explicit Gl_program(GLuint gl_name, Device_impl* device_impl = nullptr);
    ~Gl_program   () noexcept;
    Gl_program    (const Gl_program&) = delete;
    void operator=(const Gl_program&) = delete;
    Gl_program    (Gl_program&& other) noexcept;
    auto operator=(Gl_program&& other) noexcept -> Gl_program&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    Device_impl* m_device_impl{nullptr};
    GLuint       m_gl_name    {0};
};

class Gl_shader final
{
public:
    explicit Gl_shader(GLuint gl_name);
    ~Gl_shader    () noexcept;
    Gl_shader     (const Gl_shader&) = delete;
    void operator=(const Gl_shader&) = delete;
    Gl_shader     (Gl_shader&& other) noexcept;
    auto operator=(Gl_shader&& other) noexcept -> Gl_shader&;

    [[nodiscard]] auto gl_name() const -> unsigned int;

private:
    GLuint m_gl_name{0};
};

class Gl_sampler final
{
public:
    Gl_sampler() = default;
    explicit Gl_sampler(GLuint gl_name, Device_impl* device_impl = nullptr);
    ~Gl_sampler   () noexcept;
    Gl_sampler    (const Gl_sampler&) = delete;
    void operator=(const Gl_sampler&) = delete;
    Gl_sampler    (Gl_sampler&& other) noexcept;
    auto operator=(Gl_sampler&& other) noexcept -> Gl_sampler&;

    [[nodiscard]] auto gl_name() const -> unsigned int;

private:
    Device_impl* m_device_impl{nullptr};
    GLuint       m_gl_name    {0};
};

class Gl_renderbuffer final
{
public:
    explicit Gl_renderbuffer(GLuint gl_name, Device_impl* device_impl = nullptr);
    ~Gl_renderbuffer() noexcept;
    Gl_renderbuffer (const Gl_renderbuffer&) = delete;
    void operator=  (const Gl_renderbuffer&) = delete;
    Gl_renderbuffer (Gl_renderbuffer&& other) noexcept;
    auto operator=  (Gl_renderbuffer&& other) noexcept -> Gl_renderbuffer&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    Device_impl* m_device_impl{nullptr};
    GLuint       m_gl_name    {0};
};

class Gl_buffer final
{
public:
    Gl_buffer() = default;
    explicit Gl_buffer(GLuint gl_name, Device_impl* device_impl = nullptr);
    ~Gl_buffer    () noexcept;
    Gl_buffer     (const Gl_buffer&) = delete;
    void operator=(const Gl_buffer&) = delete;
    Gl_buffer     (Gl_buffer&& other) noexcept;
    auto operator=(Gl_buffer&& other) noexcept -> Gl_buffer&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    Device_impl* m_device_impl{nullptr};
    GLuint       m_gl_name    {0};
};

// Move-only holder for a cross-context publication fence (GLsync).
// publish_from_worker() is the producer half - fence-then-flush on the
// creating worker context, replacing any unconsumed earlier sync (a fence
// covers every command issued before it on its context). wait_and_consume()
// is the consumer half - a server-side glWaitSync (no CPU stall) followed
// by delete; fast no-op when empty. An unconsumed sync is deleted on
// destruction.
class Gl_publication_sync final
{
public:
    Gl_publication_sync() = default;
    ~Gl_publication_sync() noexcept;
    Gl_publication_sync(const Gl_publication_sync&) = delete;
    void operator=     (const Gl_publication_sync&) = delete;
    Gl_publication_sync(Gl_publication_sync&& old) noexcept;
    auto operator=     (Gl_publication_sync&& old) noexcept -> Gl_publication_sync&;

    void publish_from_worker();
    void wait_and_consume   ();

private:
    void* m_sync{nullptr};
};

class Gl_query final
{
public:
    explicit Gl_query(GLuint gl_name);
    ~Gl_query     () noexcept;
    Gl_query      (const Gl_query&) = delete;
    void operator=(const Gl_query&) = delete;
    Gl_query      (Gl_query&& other) noexcept;
    auto operator=(Gl_query&& other) noexcept -> Gl_query&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
};

} // namespace erhe::graphics
