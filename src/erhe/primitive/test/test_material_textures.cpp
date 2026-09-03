// Material texture slots as member-backed object properties
// (doc/property-system.md D28): the slot member is the value, the
// property notifies, the traits reject a pointee that is not a
// Texture_reference.

#include "erhe_primitive/material.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

#include <memory>

using erhe::primitive::Material;
using erhe::primitive::Material_data;
using namespace erhe::property;

namespace {

// A Texture_reference that is an item but not a GPU texture.
class Fake_texture
    : public erhe::Item<erhe::Item_base, erhe::Item_base, Fake_texture, erhe::Item_kind::not_clonable>
    , public erhe::graphics::Texture_reference
{
public:
    explicit Fake_texture(const std::string_view name) : Item{name} {}
    static constexpr std::string_view static_type_name{"Fake_texture"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::texture; }
    auto get_referenced_texture() const -> const erhe::graphics::Texture* override { return nullptr; }
};

} // anonymous namespace

TEST(Material_textures, member_is_the_value_and_property_notifies)
{
    std::shared_ptr<Material>     material = std::make_shared<Material>("m");
    std::shared_ptr<Fake_texture> texture  = std::make_shared<Fake_texture>("t");

    EXPECT_FALSE(material->get_value(Material::base_color_texture_property).object);
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_property.get()), Value_source::local); // member-backed

    // A member write (construction / import time) is what the property reads.
    material->data.texture_samplers.base_color.texture_reference = texture;
    EXPECT_EQ(material->get_value(Material::base_color_texture_property).object.get(), static_cast<erhe::Item_base*>(texture.get()));
    EXPECT_EQ(to_string(Material::base_color_texture_property.get(), material->get_value(Material::base_color_texture_property)), "t");

    // A property write reaches the member and observers.
    int notifications = 0;
    const Observer_token token = material->add_observer([&notifications](Dependency_object&, const Property_changed_args&) { ++notifications; });
    material->set_normal_texture(texture);
    EXPECT_EQ(material->data.texture_samplers.normal.texture_reference.get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(material->get_normal_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(notifications, 1);
    material->set_normal_texture(texture); // unchanged: no notification
    EXPECT_EQ(notifications, 1);
    material->set_normal_texture({});
    EXPECT_FALSE(material->data.texture_samplers.normal.texture_reference);
    EXPECT_EQ(notifications, 2);
}

TEST(Material_textures, traits_reject_a_material_as_a_texture)
{
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    std::shared_ptr<Material> other    = std::make_shared<Material>("other");
    material->set_value(Material::emissive_texture_property, Object_reference{other});
    EXPECT_FALSE(material->data.texture_samplers.emissive.texture_reference);
}

TEST(Material_textures, set_data_writes_slots_through_the_properties)
{
    std::shared_ptr<Material>     material = std::make_shared<Material>("m");
    std::shared_ptr<Fake_texture> texture  = std::make_shared<Fake_texture>("t");

    int notifications = 0;
    const Observer_token token = material->add_observer([&notifications](Dependency_object&, const Property_changed_args&) { ++notifications; });

    Material_data after{};
    after.texture_samplers.occlusion.texture_reference = texture;
    after.texture_samplers.occlusion.scale             = glm::vec2{2.0f, 2.0f};
    material->set_data(after);
    EXPECT_EQ(material->get_occlusion_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(material->data.texture_samplers.occlusion.scale, (glm::vec2{2.0f, 2.0f}));
    EXPECT_EQ(notifications, 1);
    EXPECT_TRUE(material->data == after);

    material->set_data(Material_data{});
    EXPECT_FALSE(material->get_occlusion_texture());
    EXPECT_EQ(notifications, 2);
}

TEST(Material_textures, equality_and_property_set_see_the_slot)
{
    std::shared_ptr<Material>     a       = std::make_shared<Material>("a");
    std::shared_ptr<Material>     b       = std::make_shared<Material>("a");
    std::shared_ptr<Fake_texture> texture = std::make_shared<Fake_texture>("t");
    EXPECT_TRUE(*a == *b);
    a->set_base_color_texture(texture);
    EXPECT_FALSE(*a == *b);

    // Member-backed: listed among the local values, so a copied bag carries it.
    const Property_set bag = Property_set::read_local_values(*a);
    const std::optional<Property_value> entry = bag.find(Material::base_color_texture_property.get());
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(std::get<Object_reference>(entry.value()).object.get(), static_cast<erhe::Item_base*>(texture.get()));
}
