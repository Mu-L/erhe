#include "erhe_primitive/material.hpp"

namespace erhe::primitive {

namespace {

using erhe::property::Property;
using erhe::property::Property_flags;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

constexpr uint64_t c_owner = erhe::Item_type::material;

auto unit_range(const Property_value& v) -> bool
{
    const float f = std::get<float>(v);
    return (f >= 0.0f) && (f <= 1.0f);
}

auto slider(const float min, const float max, const std::string_view tooltip = {}) -> Property_ui
{
    return Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::slider, .tooltip = tooltip};
}

// Flags: the draw lists partition by blending class, double-sidedness, the
// unlit shadow filter and the aniso control (Draw_list_scene, Draw_list_key);
// the shader variant key reads blending mode, BxDF model, normal map
// encoding and the brushed-metal block (Shader_key).
constexpr uint32_t c_partition = Property_flags::serialize | Property_flags::affects_draw_list_partition;
constexpr uint32_t c_variant   = Property_flags::serialize | Property_flags::affects_shader_variant;

} // anonymous namespace

const Property<glm::vec3> Material::base_color_property = Property<glm::vec3>::register_property(
    "base_color", c_owner, Property_metadata{.default_value = glm::vec3{1.0f, 1.0f, 1.0f}, .ui = Property_ui{.presentation = Property_ui::Presentation::color}}
);
const Property<float> Material::opacity_property = Property<float>::register_property(
    "opacity", c_owner, Property_metadata{.default_value = 1.0f, .ui = slider(0.0f, 1.0f)}, unit_range
);
const Property<glm::vec2> Material::roughness_property = Property<glm::vec2>::register_property(
    "roughness", c_owner, Property_metadata{.default_value = glm::vec2{0.5f, 0.5f}, .ui = Property_ui{.min = 0.001f, .max = 1.0f, .step = 0.005f, .tooltip = "X and Y roughness; Y is used by anisotropic BxDF models"}}
);
const Property<float> Material::metallic_property = Property<float>::register_property(
    "metallic", c_owner, Property_metadata{.default_value = 0.0f, .ui = slider(0.0f, 1.0f)}, unit_range
);
const Property<float> Material::reflectance_property = Property<float>::register_property(
    "reflectance", c_owner, Property_metadata{.default_value = 0.5f, .ui = slider(0.35f, 1.0f)}
);
const Property<glm::vec3> Material::emissive_property = Property<glm::vec3>::register_property(
    "emissive", c_owner, Property_metadata{.default_value = glm::vec3{0.0f, 0.0f, 0.0f}, .ui = Property_ui{.presentation = Property_ui::Presentation::color}}
);
const Property<float> Material::ior_property = Property<float>::register_property(
    "ior", c_owner, Property_metadata{.default_value = 1.5f, .ui = slider(1.0f, 3.0f, "Index of refraction")}
);
const Property<float> Material::transmission_property = Property<float>::register_property(
    "transmission", c_owner, Property_metadata{.default_value = 0.0f, .ui = slider(0.0f, 1.0f)}, unit_range
);
const Property<float> Material::normal_texture_scale_property = Property<float>::register_property(
    "normal_texture_scale", c_owner, Property_metadata{.default_value = 1.0f, .ui = slider(0.0f, 1.0f, "Strength of the bound normal texture")}
);
const Property<Normalmap_encoding> Material::normalmap_encoding_property = Property<Normalmap_encoding>::register_property(
    "normalmap_encoding", c_owner, c_normalmap_encoding_enum_info,
    Property_metadata{
        .default_value = erhe::property::make_value(Normalmap_encoding::right_handed_three_channel),
        .flags         = c_variant,
        .ui            = Property_ui{.tooltip = "Storage encoding of the bound normal texture. A KTX2 normal-mode texture overrides the channel layout; the handedness is always honored"}
    }
);
const Property<float> Material::occlusion_texture_strength_property = Property<float>::register_property(
    "occlusion_texture_strength", c_owner, Property_metadata{.default_value = 1.0f, .ui = slider(0.0f, 1.0f)}, unit_range
);
const Property<Bxdf_model> Material::bxdf_model_property = Property<Bxdf_model>::register_property(
    "bxdf_model", c_owner, c_bxdf_model_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Bxdf_model::isotropic_brdf), .flags = c_partition | c_variant}
);
const Property<Material_blending_mode> Material::blending_mode_property = Property<Material_blending_mode>::register_property(
    "blending_mode", c_owner, c_material_blending_mode_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Material_blending_mode::opaque), .flags = c_partition | c_variant}
);
const Property<bool> Material::double_sided_property = Property<bool>::register_property(
    "double_sided", c_owner, Property_metadata{.default_value = false, .flags = c_partition}
);
const Property<float> Material::alpha_cutoff_property = Property<float>::register_property(
    "alpha_cutoff", c_owner, Property_metadata{.default_value = 0.5f, .ui = slider(0.0f, 1.0f, "Used by the Alpha Test blending mode")}, unit_range
);
const Property<bool> Material::use_circular_brushed_metal_property = Property<bool>::register_property(
    "use_circular_brushed_metal", c_owner, Property_metadata{.default_value = false, .flags = c_variant, .ui = Property_ui{.tooltip = "Anisotropic BxDF models only"}}
);
const Property<Texgen_mode> Material::circular_brushed_metal_texgen_mode_property = Property<Texgen_mode>::register_property(
    "circular_brushed_metal_texgen_mode", c_owner, c_texgen_mode_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Texgen_mode::uv1), .flags = c_variant, .ui = Property_ui{.tooltip = "Texgen source for the circular brushed metal block"}}
);
const Property<bool> Material::use_aniso_control_property = Property<bool>::register_property(
    "use_aniso_control", c_owner, Property_metadata{.default_value = false, .flags = c_partition | c_variant, .ui = Property_ui{.tooltip = "Anisotropic BxDF models only"}}
);

Material::Material()                           = default;
Material::Material(const Material&)            = default;
Material& Material::operator=(const Material&) = default;
Material::~Material() noexcept                 = default;

Material::Material(const Material_create_info& create_info)
    : Item{create_info.name}
    , data{create_info.data}
{
    set_values(create_info.values);
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

auto Material::get_values() const -> Material_values
{
    return Material_values{
        .base_color                         = get_base_color(),
        .opacity                            = get_opacity(),
        .roughness                          = get_roughness(),
        .metallic                           = get_metallic(),
        .reflectance                        = get_reflectance(),
        .emissive                           = get_emissive(),
        .ior                                = get_ior(),
        .transmission                       = get_transmission(),
        .normal_texture_scale               = get_normal_texture_scale(),
        .normalmap_encoding                 = get_normalmap_encoding(),
        .occlusion_texture_strength         = get_occlusion_texture_strength(),
        .bxdf_model                         = get_bxdf_model(),
        .blending_mode                      = get_blending_mode(),
        .double_sided                       = get_double_sided(),
        .alpha_cutoff                       = get_alpha_cutoff(),
        .use_circular_brushed_metal         = get_use_circular_brushed_metal(),
        .circular_brushed_metal_texgen_mode = get_circular_brushed_metal_texgen_mode(),
        .use_aniso_control                  = get_use_aniso_control()
    };
}

void Material::set_values(const Material_values& values)
{
    const erhe::property::Dependency_object::Change_batch batch{*this};
    set_base_color                        (values.base_color);
    set_opacity                           (values.opacity);
    set_roughness                         (values.roughness);
    set_metallic                          (values.metallic);
    set_reflectance                       (values.reflectance);
    set_emissive                          (values.emissive);
    set_ior                               (values.ior);
    set_transmission                      (values.transmission);
    set_normal_texture_scale              (values.normal_texture_scale);
    set_normalmap_encoding                (values.normalmap_encoding);
    set_occlusion_texture_strength        (values.occlusion_texture_strength);
    set_bxdf_model                        (values.bxdf_model);
    set_blending_mode                     (values.blending_mode);
    set_double_sided                      (values.double_sided);
    set_alpha_cutoff                      (values.alpha_cutoff);
    set_use_circular_brushed_metal        (values.use_circular_brushed_metal);
    set_circular_brushed_metal_texgen_mode(values.circular_brushed_metal_texgen_mode);
    set_use_aniso_control                 (values.use_aniso_control);
}

auto Material::to_property_set(const Material_values& values) -> erhe::property::Property_set
{
    using erhe::property::make_value;
    erhe::property::Property_set result;
    result.set(base_color_property,                         make_value(values.base_color));
    result.set(opacity_property,                            make_value(values.opacity));
    result.set(roughness_property,                          make_value(values.roughness));
    result.set(metallic_property,                           make_value(values.metallic));
    result.set(reflectance_property,                        make_value(values.reflectance));
    result.set(emissive_property,                           make_value(values.emissive));
    result.set(ior_property,                                make_value(values.ior));
    result.set(transmission_property,                       make_value(values.transmission));
    result.set(normal_texture_scale_property,               make_value(values.normal_texture_scale));
    result.set(normalmap_encoding_property,                 make_value(values.normalmap_encoding));
    result.set(occlusion_texture_strength_property,         make_value(values.occlusion_texture_strength));
    result.set(bxdf_model_property,                         make_value(values.bxdf_model));
    result.set(blending_mode_property,                      make_value(values.blending_mode));
    result.set(double_sided_property,                       make_value(values.double_sided));
    result.set(alpha_cutoff_property,                       make_value(values.alpha_cutoff));
    result.set(use_circular_brushed_metal_property,         make_value(values.use_circular_brushed_metal));
    result.set(circular_brushed_metal_texgen_mode_property, make_value(values.circular_brushed_metal_texgen_mode));
    result.set(use_aniso_control_property,                  make_value(values.use_aniso_control));
    return result;
}

[[nodiscard]] auto operator==(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs)
{
    return
        (lhs.texture_reference == rhs.texture_reference) &&
        (lhs.sampler           == rhs.sampler          ) &&
        (lhs.texgen_mode       == rhs.texgen_mode      ) &&
        (lhs.rotation          == rhs.rotation         ) &&
        (lhs.offset            == rhs.offset           ) &&
        (lhs.scale             == rhs.scale            );

}
[[nodiscard]] auto operator!=(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs)
{
    return !(lhs == rhs);
}

[[nodiscard]] auto operator==(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs)
{
    return
        (lhs.base_color         == rhs.base_color        ) &&
        (lhs.metallic_roughness == rhs.metallic_roughness) &&
        (lhs.normal             == rhs.normal            ) &&
        (lhs.occlusion          == rhs.occlusion         ) &&
        (lhs.emissive           == rhs.emissive          );
}

[[nodiscard]] auto operator!=(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs)
{
    return !(lhs == rhs);
}

[[nodiscard]] auto operator==(const Material_values& lhs, const Material_values& rhs) -> bool
{
    return
        (lhs.base_color                         == rhs.base_color                        ) &&
        (lhs.opacity                            == rhs.opacity                           ) &&
        (lhs.roughness                          == rhs.roughness                         ) &&
        (lhs.metallic                           == rhs.metallic                          ) &&
        (lhs.reflectance                        == rhs.reflectance                       ) &&
        (lhs.emissive                           == rhs.emissive                          ) &&
        (lhs.ior                                == rhs.ior                               ) &&
        (lhs.transmission                       == rhs.transmission                      ) &&
        (lhs.normal_texture_scale               == rhs.normal_texture_scale              ) &&
        (lhs.normalmap_encoding                 == rhs.normalmap_encoding                ) &&
        (lhs.occlusion_texture_strength         == rhs.occlusion_texture_strength        ) &&
        (lhs.bxdf_model                         == rhs.bxdf_model                        ) &&
        (lhs.blending_mode                      == rhs.blending_mode                     ) &&
        (lhs.double_sided                       == rhs.double_sided                      ) &&
        (lhs.alpha_cutoff                       == rhs.alpha_cutoff                      ) &&
        (lhs.use_circular_brushed_metal         == rhs.use_circular_brushed_metal        ) &&
        (lhs.circular_brushed_metal_texgen_mode == rhs.circular_brushed_metal_texgen_mode) &&
        (lhs.use_aniso_control                  == rhs.use_aniso_control                 );
}

[[nodiscard]] auto operator!=(const Material_values& lhs, const Material_values& rhs) -> bool
{
    return !(lhs == rhs);
}

[[nodiscard]] auto operator==(const Material_data& lhs, const Material_data& rhs) -> bool
{
    return lhs.texture_samplers == rhs.texture_samplers;
}

[[nodiscard]] auto operator!=(const Material_data& lhs, const Material_data& rhs) -> bool
{
    return !(lhs == rhs);
}

[[nodiscard]] auto operator==(const Material& lhs, const Material& rhs) -> bool
{
    return
        (lhs.get_name() == rhs.get_name()) &&
        (lhs.data       == rhs.data      ) &&
        (erhe::property::Property_set::read_local_values(lhs) == erhe::property::Property_set::read_local_values(rhs));
}

[[nodiscard]] auto operator!=(const Material& lhs, const Material& rhs) -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::primitive
