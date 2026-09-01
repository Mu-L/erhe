#include "renderers/material_set_factory.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace editor {

Material_set_factory::Material_set_factory(
    erhe::graphics::Device&                  graphics_device,
    erhe::graphics::Command_buffer&          init_command_buffer,
    erhe::scene_renderer::Program_interface& program_interface
)
    : m_graphics_device  {graphics_device}
    , m_program_interface{program_interface}
    , m_fallback_texture {graphics_device.create_dummy_texture(init_command_buffer, erhe::dataformat::Format::format_8_vec4_srgb)}
    , m_fallback_sampler {
        std::make_unique<erhe::graphics::Sampler>(
            graphics_device,
            erhe::graphics::Sampler_create_info{
                // Texelfetch sampling makes the sampler irrelevant for the
                // dummy texture; nearest is what the per-renderer pairs used.
                .min_filter        = erhe::graphics::Filter::nearest,
                .mag_filter        = erhe::graphics::Filter::nearest,
                .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .address_mode      = {
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge,
                    erhe::graphics::Sampler_address_mode::clamp_to_edge
                },
                .compare_enable    = false,
                .compare_operation = erhe::graphics::Compare_operation::always,
                .debug_label       = "Material_set_factory fallback sampler"
            }
        )
    }
    , m_empty_material_set{
        std::make_unique<erhe::scene_renderer::Material_set>(
            erhe::scene_renderer::Material_set_create_info{
                .graphics_device        = &graphics_device,
                .material_interface     = &program_interface.material_interface,
                .bind_group_layout      = program_interface.bind_group_layout.get(),
                .fallback_texture       = m_fallback_texture.get(),
                .fallback_sampler       = m_fallback_sampler.get(),
                .max_textures           = 1,
                .initial_material_count = 1,
                .debug_label            = erhe::utility::Debug_label{"Empty material set"}
            }
        )
    }
{
}

Material_set_factory::~Material_set_factory() noexcept = default;

auto Material_set_factory::make_create_info(
    const char*       debug_label,
    const std::size_t max_textures,
    const std::size_t initial_material_count
) const -> erhe::scene_renderer::Material_set_create_info
{
    return erhe::scene_renderer::Material_set_create_info{
        .graphics_device        = &m_graphics_device,
        .material_interface     = &m_program_interface.material_interface,
        .bind_group_layout      = m_program_interface.bind_group_layout.get(),
        .fallback_texture       = m_fallback_texture.get(),
        .fallback_sampler       = m_fallback_sampler.get(),
        .max_textures           = max_textures,
        .initial_material_count = initial_material_count,
        .debug_label            = erhe::utility::Debug_label{debug_label}
    };
}

auto Material_set_factory::get_empty_material_set() -> erhe::scene_renderer::Material_set&
{
    return *m_empty_material_set.get();
}

void Material_set_factory::update_empty_material_set(erhe::graphics::Command_buffer& command_buffer)
{
    m_empty_material_set->update(command_buffer);
}

} // namespace editor
