// Light authored state as erhe::property properties
// (doc/property-system.md section 4.3): defaults match the previous
// field initializers, every property change re-resolves the light set
// through the shared changed callback (D19), clones copy the values.

#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using erhe::scene::Light;
using erhe::scene::Light_type;

namespace {

class Counting_scene_host : public erhe::scene::Scene_host
{
public:
    Counting_scene_host() : scene{"test scene", this} {}

    auto get_host_name   () const -> const char*        override { return "Counting_scene_host"; }
    auto get_hosted_scene()       -> erhe::scene::Scene* override { return &scene; }

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.register_node  (node); }
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.unregister_node(node); }
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>&)        override {}
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>&)        override {}
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void register_light   (const std::shared_ptr<erhe::scene::Light>&)         override {}
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&)         override {}
    void register_layout  (const std::shared_ptr<erhe::scene::Layout>&)        override {}
    void unregister_layout(const std::shared_ptr<erhe::scene::Layout>&)        override {}

    void on_mesh_primitives_changed    (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_material_changed      (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_flags_changed         (const std::shared_ptr<erhe::scene::Mesh>&, uint64_t, uint64_t) override {}
    void on_mesh_transform_changed     (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override { ++light_changed_count; }

    erhe::scene::Scene scene;
    int                light_changed_count{0};
};

} // anonymous namespace

TEST(Light_properties, defaults_match_previous_initializers)
{
    auto light = std::make_shared<Light>("l");
    EXPECT_EQ(light->get_light_type(), Light_type::directional);
    EXPECT_EQ(light->get_color(), glm::vec3{1.0f});
    EXPECT_FLOAT_EQ(light->get_intensity(), 1.0f);
    EXPECT_FLOAT_EQ(light->get_temperature(), 0.0f);
    EXPECT_FLOAT_EQ(light->get_range(), 100.0f);
    EXPECT_FLOAT_EQ(light->get_inner_spot_angle(), glm::pi<float>() * 0.4f);
    EXPECT_FLOAT_EQ(light->get_outer_spot_angle(), glm::pi<float>() * 0.5f);
    EXPECT_TRUE(light->get_cast_shadow());
    EXPECT_EQ(light->get_value_source(Light::color_property), Value_source::default_value);
    EXPECT_TRUE(light->is_active());
    EXPECT_TRUE(light->casts_shadow());
}

TEST(Light_properties, typed_and_untyped_access)
{
    auto light = std::make_shared<Light>("l");
    light->set_light_type(Light_type::spot);
    light->set_outer_spot_angle(0.0f);
    EXPECT_FALSE(light->is_active()); // spot with zero outer angle
    light->set_outer_spot_angle(1.0f);
    EXPECT_TRUE(light->is_active());
    light->set_cast_shadow(false);
    EXPECT_FALSE(light->casts_shadow());

    const Dependency_property* type = Property_registry::get().find_for_type(light->get_type(), "light_type");
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(to_string(*type, light->get_value(*type)), "Spot");
    light->set_value(*type, parse_value(*type, "Point").value());
    EXPECT_EQ(light->get_light_type(), Light_type::point);
    EXPECT_FALSE(parse_value(*type, "Area").has_value());

    const Dependency_property* color = Property_registry::get().find_for_type(light->get_type(), "color");
    ASSERT_NE(color, nullptr);
    light->set_value(*color, parse_value(*color, "0.5 0.25 1").value());
    EXPECT_EQ(light->get_color(), (glm::vec3{0.5f, 0.25f, 1.0f}));
}

TEST(Light_properties, every_change_re_resolves_the_light_set)
{
    Counting_scene_host host;
    auto node  = std::make_shared<erhe::scene::Node>("n");
    auto light = std::make_shared<Light>("l");
    node->attach(light);
    node->set_parent(host.scene.get_root_node());
    host.light_changed_count = 0;

    light->set_intensity(2.0f);
    EXPECT_EQ(host.light_changed_count, 1);
    light->set_intensity(2.0f); // same value: no effective change, no re-resolve
    EXPECT_EQ(host.light_changed_count, 1);
    light->set_light_type(Light_type::spot);
    light->set_cast_shadow(false);
    light->set_range(5.0f);
    light->set_color(glm::vec3{0.5f});
    light->set_temperature(6500.0f);
    light->set_inner_spot_angle(0.1f);
    light->set_outer_spot_angle(0.2f);
    EXPECT_EQ(host.light_changed_count, 8);
    light->clear_value(Light::range_property);
    EXPECT_EQ(host.light_changed_count, 9);
    EXPECT_FLOAT_EQ(light->get_range(), 100.0f);
    {
        // A batch collapses one property's writes into one notification.
        Dependency_object::Change_batch batch{*light};
        light->set_intensity(3.0f);
        light->set_intensity(4.0f);
        EXPECT_EQ(host.light_changed_count, 9);
    }
    EXPECT_EQ(host.light_changed_count, 10);
}

TEST(Light_properties, unhosted_light_changes_are_silent)
{
    auto light = std::make_shared<Light>("l");
    light->set_intensity(2.0f); // no host: nothing to notify, must not crash
    EXPECT_FLOAT_EQ(light->get_intensity(), 2.0f);
}

TEST(Light_properties, clone_copies_values_and_bag_round_trips)
{
    auto light = std::make_shared<Light>("l");
    light->set_light_type(Light_type::point);
    light->set_color(glm::vec3{0.1f, 0.2f, 0.3f});
    light->set_range(7.0f);
    light->layer_id = 3;

    const auto clone = std::static_pointer_cast<Light>(light->clone());
    ASSERT_TRUE(clone);
    EXPECT_EQ(clone->get_light_type(), Light_type::point);
    EXPECT_EQ(clone->get_color(), (glm::vec3{0.1f, 0.2f, 0.3f}));
    EXPECT_FLOAT_EQ(clone->get_range(), 7.0f);
    EXPECT_EQ(clone->layer_id, 3u);
    EXPECT_EQ(clone->get_value_source(Light::intensity_property), Value_source::default_value);

    const Property_set bag = Property_set::read_local_values(*light);
    EXPECT_EQ(bag.size(), 3u);
    auto other = std::make_shared<Light>("o");
    bag.apply(*other);
    EXPECT_EQ(Property_set::read_local_values(*other), bag);
    EXPECT_FLOAT_EQ(other->get_range(), 7.0f);
}

TEST(Light_properties, luminous_flux_writes_intensity)
{
    auto light = std::make_shared<Light>("l");
    light->set_light_type(Light_type::point);
    light->set_luminous_flux(4.0f * glm::pi<float>());
    EXPECT_NEAR(light->get_intensity(), 1.0f, 1e-5f);
    EXPECT_EQ(light->get_value_source(Light::intensity_property), Value_source::local);
}
