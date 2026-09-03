// Camera projection fields as bridged erhe::property properties
// (doc/property-system.md section 4.4): the properties and
// Camera::projection() read and write the same Projection; exposure and
// shadow range live in the property store.

#include "erhe_scene/camera.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using erhe::scene::Camera;
using erhe::scene::Projection;

TEST(Camera_properties, bridged_reads_see_projection_writes)
{
    auto camera = std::make_shared<Camera>("c");
    EXPECT_EQ(camera->get_value(Camera::projection_type_property), Projection::Type::perspective_vertical);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::z_near_property), 0.03f);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::z_far_property), 64.0f);
    EXPECT_EQ(camera->get_value_source(Camera::z_far_property), Value_source::local); // bridged: always local

    camera->projection()->projection_type = Projection::Type::orthogonal;
    camera->projection()->z_far           = 200.0f;
    camera->projection()->ortho_width     = 12.0f;
    camera->projection()->infinite_z_far  = true;
    EXPECT_EQ(camera->get_value(Camera::projection_type_property), Projection::Type::orthogonal);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::z_far_property), 200.0f);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::ortho_width_property), 12.0f);
    EXPECT_TRUE(camera->get_value(Camera::infinite_z_far_property));
}

TEST(Camera_properties, bridged_writes_are_visible_through_projection)
{
    auto camera = std::make_shared<Camera>("c");
    camera->set_value(Camera::projection_type_property, Projection::Type::perspective);
    camera->set_value(Camera::fov_x_property, 1.0f);
    camera->set_value(Camera::fov_y_property, 0.5f);
    camera->set_value(Camera::z_near_property, 0.1f);
    camera->set_value(Camera::infinite_z_far_property, true);
    const Projection* projection = camera->projection();
    EXPECT_EQ(projection->projection_type, Projection::Type::perspective);
    EXPECT_FLOAT_EQ(projection->fov_x, 1.0f);
    EXPECT_FLOAT_EQ(projection->fov_y, 0.5f);
    EXPECT_FLOAT_EQ(projection->z_near, 0.1f);
    EXPECT_TRUE(projection->infinite_z_far);

    // clear_value() on a bridged property writes the default.
    camera->clear_value(Camera::fov_x_property);
    EXPECT_FLOAT_EQ(projection->fov_x, Projection{}.fov_x);
}

TEST(Camera_properties, untyped_access_with_enumeration_labels)
{
    auto camera = std::make_shared<Camera>("c");
    const Dependency_property* type = Property_registry::get().find_for_object(camera->get_property_owner_type(), "projection_type");
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(to_string(*type, camera->get_value(*type)), "Perspective Vertical");
    camera->set_value(*type, parse_value(*type, "Generic Frustum").value());
    EXPECT_EQ(camera->projection()->projection_type, Projection::Type::generic_frustum);
    EXPECT_FALSE(parse_value(*type, "Fisheye").has_value());

    const Dependency_property* z_far = Property_registry::get().find_for_object(camera->get_property_owner_type(), "z_far");
    ASSERT_NE(z_far, nullptr);
    camera->set_value(*z_far, parse_value(*z_far, "500").value());
    EXPECT_FLOAT_EQ(camera->projection()->z_far, 500.0f);
}

TEST(Camera_properties, exposure_and_shadow_range_live_in_the_store)
{
    auto camera = std::make_shared<Camera>("c");
    EXPECT_FLOAT_EQ(camera->get_exposure(), 1.0f);
    EXPECT_FLOAT_EQ(camera->get_shadow_range(), 22.0f);
    EXPECT_EQ(camera->get_value_source(Camera::exposure_property), Value_source::default_value);
    camera->set_exposure(3.0f);
    camera->set_shadow_range(40.0f);
    EXPECT_EQ(camera->get_value_source(Camera::exposure_property), Value_source::local);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::exposure_property), 3.0f);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::shadow_range_property), 40.0f);
    camera->clear_value(Camera::exposure_property);
    EXPECT_FLOAT_EQ(camera->get_exposure(), 1.0f);
}

TEST(Camera_properties, clone_copies_projection_and_store)
{
    auto camera = std::make_shared<Camera>("c");
    camera->projection()->z_far = 300.0f;
    camera->set_exposure(2.0f);
    const auto clone = std::static_pointer_cast<Camera>(camera->clone());
    ASSERT_TRUE(clone);
    EXPECT_FLOAT_EQ(clone->projection()->z_far, 300.0f);
    EXPECT_FLOAT_EQ(clone->get_value(Camera::z_far_property), 300.0f);
    EXPECT_FLOAT_EQ(clone->get_exposure(), 2.0f);
    EXPECT_EQ(clone->get_value_source(Camera::shadow_range_property), Value_source::default_value);
}
