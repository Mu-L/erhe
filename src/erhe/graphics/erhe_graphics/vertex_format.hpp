#pragma once

#include "erhe_graphics/vertex_attribute.hpp"

#include <functional>
#include <vector>

namespace erhe::graphics {

class Shader_resource;

/// Describes vertex data layout in buffer / memory
class Vertex_format final
{
public:
    Vertex_format();
    Vertex_format(std::size_t binding, const std::initializer_list<Vertex_attribute> attributes);

    void add_attribute(const Vertex_attribute& attribute);

    [[nodiscard]] auto get_binding         () const -> std::size_t;
    [[nodiscard]] auto has_attribute       (Vertex_attribute::Usage_type usage_type, unsigned int index = 0) const -> bool;
    [[nodiscard]] auto find_attribute_maybe(Vertex_attribute::Usage_type usage_type, unsigned int index = 0) const -> const Vertex_attribute*;
    [[nodiscard]] auto find_attribute      (Vertex_attribute::Usage_type usage_type, unsigned int index = 0) const -> const Vertex_attribute*;
    [[nodiscard]] auto stride              () const -> size_t { return m_stride; }
    [[nodiscard]] auto match               (const Vertex_format& other) const -> bool;
    [[nodiscard]] auto get_attributes      () const -> const std::vector<Vertex_attribute>& { return m_attributes; }

    void add_to(Shader_resource& vertex_struct, Shader_resource& vertices_block);

private:
    void align_to(std::size_t alignment);

    std::size_t                   m_binding{0};
    std::vector<Vertex_attribute> m_attributes;
    std::size_t                   m_stride{0};
};

} // namespace erhe::graphics
