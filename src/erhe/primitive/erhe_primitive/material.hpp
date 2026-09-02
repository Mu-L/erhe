#pragma once

#include "erhe_item/item.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_set.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace erhe::graphics {
    class Sampler;
    class Texture_reference;
}

namespace erhe::primitive {

class Material_texture_sampler
{
public:
    std::shared_ptr<erhe::graphics::Texture_reference> texture_reference{};
    std::shared_ptr<erhe::graphics::Sampler>           sampler          {};
    Texgen_mode                                        texgen_mode      {Texgen_mode::uv0};
    float                                              rotation         {0.0f};
    glm::vec2                                          offset           {0.0f, 0.0f};
    glm::vec2                                          scale            {1.0f, 1.0f};
};

[[nodiscard]] auto operator==(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs);
[[nodiscard]] auto operator!=(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs);

class Material_texture_samplers
{
public:
    Material_texture_sampler base_color;
    Material_texture_sampler metallic_roughness;
    Material_texture_sampler normal;
    Material_texture_sampler occlusion;
    Material_texture_sampler emissive;
};

[[nodiscard]] auto operator==(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs);
[[nodiscard]] auto operator!=(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs);

// True when the mode needs framebuffer blending (so the host mesh must
// route through the translucent composition-pass family). alpha_test and
// screen_door render with depth write enabled and discard for masked
// pixels, so they stay in the opaque pass.
[[nodiscard]] inline auto needs_translucent_pass(const Material_blending_mode mode) -> bool
{
    switch (mode) {
        case Material_blending_mode::alpha_blend:
        case Material_blending_mode::multiply:
        case Material_blending_mode::add:
        case Material_blending_mode::subtract:
            return true;
        case Material_blending_mode::opaque:
        case Material_blending_mode::screen_door:
        case Material_blending_mode::alpha_test:
            return false;
    }
    return false;
}

// The material state that is NOT a registered property: the texture slots.
// Everything else lives in the Material's property store (see Material).
class Material_data
{
public:
    Material_texture_samplers texture_samplers{};
};

// Plain snapshot of every registered Material property, for code that reads
// or writes the whole set at once (renderer upload, glTF import / export,
// MCP edits). Material::get_values() / set_values() convert; the property
// store is the source of truth.
class Material_values
{
public:
    glm::vec3                 base_color                        {1.0f, 1.0f, 1.0f};
    float                     opacity                           {1.0f};
    glm::vec2                 roughness                         {0.5f, 0.5f};
    float                     metallic                          {0.0f};
    float                     reflectance                       {0.5f};
    glm::vec3                 emissive                          {0.0f, 0.0f, 0.0f};
    float                     ior                               {1.5f};
    float                     transmission                      {0.0f};
    float                     normal_texture_scale              {1.0f};
    // Storage encoding of the bound normal texture (handedness and, for
    // X+Y maps, the channel layout). Authorable in the Properties window;
    // a KTX2 normal-mode texture overrides the channel layout at shader
    // variant derivation (the flag travels on the texture), keeping the
    // authored handedness.
    Normalmap_encoding        normalmap_encoding                {Normalmap_encoding::right_handed_three_channel};
    float                     occlusion_texture_strength        {1.0f};
    Bxdf_model                bxdf_model                        {Bxdf_model::isotropic_brdf};
    Material_blending_mode    blending_mode                     {Material_blending_mode::opaque};
    bool                      double_sided                      {false};
    float                     alpha_cutoff                      {0.5f};
    bool                      use_circular_brushed_metal        {false};
    // Texgen source for the in-shader circular brushed metal block
    // (T/B derivation and isotropy falloff). Defaults to uv1, matching the
    // default target of generate_mesh_facet_texture_coordinates().
    Texgen_mode               circular_brushed_metal_texgen_mode{Texgen_mode::uv1};
    bool                      use_aniso_control                 {false};
};

[[nodiscard]] auto operator==(const Material_values& lhs, const Material_values& rhs) -> bool;
[[nodiscard]] auto operator!=(const Material_values& lhs, const Material_values& rhs) -> bool;

class Material_create_info
{
public:
    std::string     name  {};
    Material_values values{};
    Material_data   data  {};
};

class Material : public erhe::Item<erhe::Item_base, erhe::Item_base, Material>
{
public:
    Material();
    explicit Material(const Material&);
    Material& operator=(const Material&);
    ~Material() noexcept override;

    explicit Material(const Material_create_info& create_info);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Material"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::material; }

    // Registered properties (erhe::property, doc/property-system-plan.md
    // section 4.1). The typed accessors below read and write these on the
    // item's property store.
    static const erhe::property::Property<glm::vec3>              base_color_property;
    static const erhe::property::Property<float>                  opacity_property;
    static const erhe::property::Property<glm::vec2>              roughness_property;
    static const erhe::property::Property<float>                  metallic_property;
    static const erhe::property::Property<float>                  reflectance_property;
    static const erhe::property::Property<glm::vec3>              emissive_property;
    static const erhe::property::Property<float>                  ior_property;
    static const erhe::property::Property<float>                  transmission_property;
    static const erhe::property::Property<float>                  normal_texture_scale_property;
    static const erhe::property::Property<Normalmap_encoding>     normalmap_encoding_property;
    static const erhe::property::Property<float>                  occlusion_texture_strength_property;
    static const erhe::property::Property<Bxdf_model>             bxdf_model_property;
    static const erhe::property::Property<Material_blending_mode> blending_mode_property;
    static const erhe::property::Property<bool>                   double_sided_property;
    static const erhe::property::Property<float>                  alpha_cutoff_property;
    static const erhe::property::Property<bool>                   use_circular_brushed_metal_property;
    static const erhe::property::Property<Texgen_mode>            circular_brushed_metal_texgen_mode_property;
    static const erhe::property::Property<bool>                   use_aniso_control_property;

    [[nodiscard]] auto get_base_color                        () const -> glm::vec3              { return get_value(base_color_property); }
    [[nodiscard]] auto get_opacity                           () const -> float                  { return get_value(opacity_property); }
    [[nodiscard]] auto get_roughness                         () const -> glm::vec2              { return get_value(roughness_property); }
    [[nodiscard]] auto get_metallic                          () const -> float                  { return get_value(metallic_property); }
    [[nodiscard]] auto get_reflectance                       () const -> float                  { return get_value(reflectance_property); }
    [[nodiscard]] auto get_emissive                          () const -> glm::vec3              { return get_value(emissive_property); }
    [[nodiscard]] auto get_ior                               () const -> float                  { return get_value(ior_property); }
    [[nodiscard]] auto get_transmission                      () const -> float                  { return get_value(transmission_property); }
    [[nodiscard]] auto get_normal_texture_scale              () const -> float                  { return get_value(normal_texture_scale_property); }
    [[nodiscard]] auto get_normalmap_encoding                () const -> Normalmap_encoding     { return get_value(normalmap_encoding_property); }
    [[nodiscard]] auto get_occlusion_texture_strength        () const -> float                  { return get_value(occlusion_texture_strength_property); }
    [[nodiscard]] auto get_bxdf_model                        () const -> Bxdf_model             { return get_value(bxdf_model_property); }
    [[nodiscard]] auto get_blending_mode                     () const -> Material_blending_mode { return get_value(blending_mode_property); }
    [[nodiscard]] auto get_double_sided                      () const -> bool                   { return get_value(double_sided_property); }
    [[nodiscard]] auto get_alpha_cutoff                      () const -> float                  { return get_value(alpha_cutoff_property); }
    [[nodiscard]] auto get_use_circular_brushed_metal        () const -> bool                   { return get_value(use_circular_brushed_metal_property); }
    [[nodiscard]] auto get_circular_brushed_metal_texgen_mode() const -> Texgen_mode            { return get_value(circular_brushed_metal_texgen_mode_property); }
    [[nodiscard]] auto get_use_aniso_control                 () const -> bool                   { return get_value(use_aniso_control_property); }

    void set_base_color                        (const glm::vec3& value)      { set_value(base_color_property, value); }
    void set_opacity                           (float value)                 { set_value(opacity_property, value); }
    void set_roughness                         (const glm::vec2& value)      { set_value(roughness_property, value); }
    void set_metallic                          (float value)                 { set_value(metallic_property, value); }
    void set_reflectance                       (float value)                 { set_value(reflectance_property, value); }
    void set_emissive                          (const glm::vec3& value)      { set_value(emissive_property, value); }
    void set_ior                               (float value)                 { set_value(ior_property, value); }
    void set_transmission                      (float value)                 { set_value(transmission_property, value); }
    void set_normal_texture_scale              (float value)                 { set_value(normal_texture_scale_property, value); }
    void set_normalmap_encoding                (Normalmap_encoding value)    { set_value(normalmap_encoding_property, value); }
    void set_occlusion_texture_strength        (float value)                 { set_value(occlusion_texture_strength_property, value); }
    void set_bxdf_model                        (Bxdf_model value)            { set_value(bxdf_model_property, value); }
    void set_blending_mode                     (Material_blending_mode value){ set_value(blending_mode_property, value); }
    void set_double_sided                      (bool value)                  { set_value(double_sided_property, value); }
    void set_alpha_cutoff                      (float value)                 { set_value(alpha_cutoff_property, value); }
    void set_use_circular_brushed_metal        (bool value)                  { set_value(use_circular_brushed_metal_property, value); }
    void set_circular_brushed_metal_texgen_mode(Texgen_mode value)           { set_value(circular_brushed_metal_texgen_mode_property, value); }
    void set_use_aniso_control                 (bool value)                  { set_value(use_aniso_control_property, value); }

    // Whole-set snapshot in and out of the property store. set_values()
    // writes every field as a local value in one change batch.
    [[nodiscard]] auto get_values() const -> Material_values;
    void               set_values(const Material_values& values);

    // The Material_values fields as a property bag (D17), e.g. for
    // Property_set::diff() between two snapshots.
    [[nodiscard]] static auto to_property_set(const Material_values& values) -> erhe::property::Property_set;

    // No GPU slot here. A material's slot is a property of the Material_set
    // that issued it (doc/draw_list_material_set_plan.md D0), not of the
    // material: the same Material is normally at a different slot in every set
    // it belongs to, and a single mutable field here is what made "slot 7"
    // mean different materials in different passes.
    Material_data           data;
};

[[nodiscard]] auto operator==(const Material_data& lhs, const Material_data& rhs) -> bool;
[[nodiscard]] auto operator!=(const Material_data& lhs, const Material_data& rhs) -> bool;

[[nodiscard]] auto operator==(const Material& lhs, const Material& rhs) -> bool;
[[nodiscard]] auto operator!=(const Material& lhs, const Material& rhs) -> bool;

} // namespace erhe::primitive
