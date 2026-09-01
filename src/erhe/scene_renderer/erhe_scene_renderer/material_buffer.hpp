#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace erhe::graphics {
    class Sampler;
    class Texture;
    class Texture_heap;
}
namespace erhe::primitive {
    class Material;
}

namespace erhe::scene_renderer {

class Program_interface;
class Shader_resources;

class Material_struct
{
public:
    std::size_t roughness;                  // vec2
    std::size_t metallic;                   // float
    std::size_t reflectance;                // float

    std::size_t base_color;                 // vec4
    std::size_t emissive;                   // vec4

    std::size_t base_color_texture;         // uvec2
    std::size_t metallic_roughness_texture; // uvec2

    std::size_t normal_texture;             // uvec2
    std::size_t occlusion_texture;          // uvec2

    std::size_t emissive_texture;           // uvec2
    std::size_t opacity;                    // float
    std::size_t normal_texture_scale;       // float
    std::size_t alpha_cutoff;               // float

    std::size_t base_color_rotation_scale;         // uvec4
    std::size_t metallic_roughness_rotation_scale; // uvec4
    std::size_t normal_rotation_scale;             // uvec4
    std::size_t occlusion_rotation_scale;          // uvec4
    std::size_t emissive_rotation_scale;           // uvec4

    std::size_t base_color_offset;                 // uvec2
    std::size_t metallic_roughness_offset;         // uvec2

    std::size_t normal_offset;                     // uvec2
    std::size_t occlusion_offset;                  // uvec2

    std::size_t emissive_offset;                   // uvec2
    std::size_t occlusion_texture_strength;        // float

    std::size_t ior;                               // float
    std::size_t transmission;                      // float
    // Bxdf_model as uint, for shaders that select the BxDF at runtime (the
    // ray tracer); the raster path keeps its compile-time variant axis.
    std::size_t bxdf_model;                        // uint
};

// The texture half of one material record, resolved: the exact texture and
// sampler the heap allocation is made from, and the packed rotation / scale /
// offset the record carries.
class Material_texture_record_inputs
{
public:
    const erhe::graphics::Texture* texture          {nullptr};
    const erhe::graphics::Sampler* sampler          {nullptr};
    float                          rotation_scale[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float                          offset        [2]{0.0f, 0.0f};
};

// Everything one material record is written from, and nothing else.
//
// The record writer and the content hash (doc/draw_list_material_set_plan.md
// D10) both read this struct and only this struct, so the hash covers exactly
// the bytes the writer reads by construction rather than by a comment asking
// two lists to be kept in step. A field that dirties the buffer is a field
// that appears here; a material field that does not - the shader-variant axes
// (blending mode, double_sided, normalmap encoding, the texgen modes,
// use_aniso_control) - reaches the shader by another route and is the draw
// list's identity hash to notice.
//
// Value-initialization zeroes padding as well as members, which is what makes
// hashing the whole object well defined.
class Material_record_inputs
{
public:
    glm::vec2 roughness                 {0.0f, 0.0f};
    float     metallic                  {0.0f};
    float     reflectance               {0.0f};
    glm::vec3 base_color                {0.0f, 0.0f, 0.0f};
    float     opacity                   {0.0f};
    glm::vec3 emissive                  {0.0f, 0.0f, 0.0f};
    float     normal_texture_scale      {0.0f};
    float     alpha_cutoff              {0.0f};
    float     occlusion_texture_strength{0.0f};
    float     ior                       {0.0f};
    float     transmission              {0.0f};
    uint32_t  bxdf_model                {0};

    Material_texture_record_inputs base_color_texture        {};
    Material_texture_record_inputs metallic_roughness_texture{};
    Material_texture_record_inputs normal_texture            {};
    Material_texture_record_inputs occlusion_texture         {};
    Material_texture_record_inputs emissive_texture          {};
};

// Resolves a material to its record inputs. Texture references are resolved
// here, so a re-baked editor Graph_texture yields a different Texture pointer
// and therefore both a different record and a different content hash.
[[nodiscard]] auto gather_material_record_inputs(
    const erhe::primitive::Material& material,
    const erhe::graphics::Sampler&   fallback_sampler
) -> Material_record_inputs;

class Material_interface
{
public:
    Material_interface(erhe::graphics::Device& graphics_device, int max_material_count);

    erhe::graphics::Shader_resource material_block;
    erhe::graphics::Shader_resource material_struct;
    Material_struct                 offsets;
    std::size_t                     max_material_count;
};

class Material_buffer : public erhe::graphics::Ring_buffer_client
{
public:
    Material_buffer(erhe::graphics::Device& graphics_device, Material_interface& material_interface);

    auto update(erhe::graphics::Texture_heap& texture_heap, const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials) -> erhe::graphics::Ring_buffer_range;

    // Slot-table-driven record writer (doc/draw_list_material_set_plan.md D2),
    // alongside the ring-based update() above until phase 6 removes that one.
    // Writes one record per entry of slot_materials, in slot order, into
    // storage the caller owns; a null entry is a hole and is zero-filled.
    // Unlike update() it assigns no slot and writes nothing on the Material -
    // the slot IS the index into slot_materials, issued by the Material_set
    // that owns this buffer.
    void write_records(
        std::span<std::byte>                              gpu_data,
        erhe::graphics::Texture_heap&                     texture_heap,
        std::span<const erhe::primitive::Material* const> slot_materials
    );

    // Hash of everything write_records() reads for this material (D10). Both
    // go through gather_material_record_inputs(), so neither can drift from
    // the other.
    [[nodiscard]] auto get_content_hash    (const erhe::primitive::Material* material) const -> uint64_t;
    [[nodiscard]] auto get_record_byte_count() const -> std::size_t;

private:
    void write_record(
        std::span<std::byte>          gpu_data,
        std::size_t                   write_offset,
        const Material_record_inputs& inputs,
        erhe::graphics::Texture_heap& texture_heap
    );

    erhe::graphics::Device& m_graphics_device;
    Material_interface&     m_material_interface;

    erhe::graphics::Sampler m_fallback_sampler;
};

} // namespace erhe::scene_renderer
