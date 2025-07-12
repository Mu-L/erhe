#pragma once

#include "erhe_graphics/buffer.hpp"

#include <span>

namespace erhe::graphics {

template <typename T>
class Scoped_buffer_mapping
{
public:
    Scoped_buffer_mapping(
        Buffer&                buffer,
        const std::size_t      element_offset,
        const std::size_t      element_count,
        const Buffer_map_flags map_flags
    )
        : m_buffer{buffer}
        , m_span  {buffer.map_elements<T>(element_offset, element_count, map_flags)}
    {
    }

    ~Scoped_buffer_mapping() noexcept
    {
        m_buffer.unmap();
    }

    Scoped_buffer_mapping(const Scoped_buffer_mapping&) = delete;
    auto operator=       (const Scoped_buffer_mapping&) -> Scoped_buffer_mapping& = delete;

    auto span() const -> const std::span<T>&
    {
        return m_span;
    }

private:
    Buffer&      m_buffer;
    std::span<T> m_span;
};

} // namespace erhe::graphics
