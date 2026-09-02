#include "test_object.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float>       reg_float  = Property<float>::register_property("reg_float", type_a, Property_metadata{.default_value = 2.5f});
const Property<glm::vec3>   reg_vec3   = Property<glm::vec3>::register_property("reg_vec3", type_a);
const Property<std::string> reg_string = Property<std::string>::register_property("reg_string", type_b, Property_metadata{.default_value = std::string{"hello"}});
const Property<int>         reg_attached = Property<int>::register_attached("reg_attached", type_c);

} // anonymous namespace

TEST(Property_registry, registered_properties_have_expected_records)
{
    EXPECT_EQ(reg_float.get().get_name(), "reg_float");
    EXPECT_EQ(reg_float.get().get_type(), Property_type::floating);
    EXPECT_EQ(reg_float.get().get_owner_type(), type_a);
    EXPECT_FALSE(reg_float.get().is_read_only());
    EXPECT_FALSE(reg_float.get().is_attached());
    EXPECT_TRUE(reg_attached.get().is_attached());
    EXPECT_EQ(reg_vec3.get().get_type(), Property_type::vec3);
    EXPECT_EQ(reg_string.get().get_type(), Property_type::string);
}

TEST(Property_registry, indices_are_distinct_and_resolve_back)
{
    Property_registry& registry = Property_registry::get();
    EXPECT_NE(reg_float.get().get_index(), reg_vec3.get().get_index());
    EXPECT_EQ(&registry.get(reg_float.get().get_index()), reg_float.get_ptr());
    EXPECT_EQ(&registry.get(reg_vec3.get().get_index()), reg_vec3.get_ptr());
    EXPECT_GE(registry.get_count(), std::size_t{4});
}

TEST(Property_registry, find_by_owner_and_name)
{
    Property_registry& registry = Property_registry::get();
    EXPECT_EQ(registry.find(type_a, "reg_float"), reg_float.get_ptr());
    EXPECT_EQ(registry.find(type_b, "reg_string"), reg_string.get_ptr());
    EXPECT_EQ(registry.find(type_b, "reg_float"), nullptr);
    EXPECT_EQ(registry.find(type_a, "no_such_property"), nullptr);
}

TEST(Property_registry, default_values)
{
    EXPECT_EQ(std::get<float>(reg_float.get().get_default_value(type_a)), 2.5f);
    EXPECT_EQ(std::get<glm::vec3>(reg_vec3.get().get_default_value(type_a)), glm::vec3{0.0f});
    EXPECT_EQ(std::get<std::string>(reg_string.get().get_default_value(type_b)), "hello");
}

TEST(Property_registry, for_each_property_of_type_lists_non_attached_owners_only)
{
    std::vector<std::string> names_a;
    Property_registry::get().for_each_property_of_type(type_a, [&](const Dependency_property& p) { names_a.emplace_back(p.get_name()); });
    EXPECT_NE(std::find(names_a.begin(), names_a.end(), "reg_float"), names_a.end());
    EXPECT_NE(std::find(names_a.begin(), names_a.end(), "reg_vec3"), names_a.end());
    EXPECT_EQ(std::find(names_a.begin(), names_a.end(), "reg_string"), names_a.end());

    std::vector<std::string> names_c;
    Property_registry::get().for_each_property_of_type(type_c, [&](const Dependency_property& p) { names_c.emplace_back(p.get_name()); });
    EXPECT_EQ(std::find(names_c.begin(), names_c.end(), "reg_attached"), names_c.end());
}

TEST(Property_registry, override_metadata_per_owner_type)
{
    const Property<float> p = Property<float>::register_property("reg_override", type_a, Property_metadata{.default_value = 1.0f});
    Property_metadata override_metadata{};
    override_metadata.default_value = 7.0f;
    override_metadata.inherits      = true;
    override_metadata.flags         = Property_flags::affects_transform;
    override_metadata.ui.min        = -1.0f;
    const_cast<Dependency_property&>(p.get()).override_metadata(type_b, override_metadata);

    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_a)), 1.0f);
    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_b)), 7.0f);
    EXPECT_FALSE(p.get().get_metadata(type_a).inherits);
    EXPECT_TRUE (p.get().get_metadata(type_b).inherits);
    EXPECT_EQ(p.get().get_metadata(type_b).flags, Property_flags::affects_transform);
    EXPECT_EQ(p.get().get_metadata(type_b).ui.min, -1.0f);
    // An object of type_b sees the override; type_a the default.
    Test_object a{type_a};
    Test_object b{type_b};
    EXPECT_EQ(a.get_value(p), 1.0f);
    EXPECT_EQ(b.get_value(p), 7.0f);
    // The override's missing fields keep the default metadata's default value.
    const_cast<Dependency_property&>(p.get()).override_metadata(type_c, Property_metadata{.inherits = true});
    EXPECT_EQ(std::get<float>(p.get().get_default_value(type_c)), 1.0f);
}

TEST(Property_registry, add_owner_makes_property_findable_and_listed_for_new_owner)
{
    const Property<int> p = Property<int>::register_property("reg_add_owner", type_a, Property_metadata{.default_value = 3});
    const_cast<Dependency_property&>(p.get()).add_owner(type_b, Property_metadata{.default_value = 4});
    EXPECT_EQ(Property_registry::get().find(type_b, "reg_add_owner"), p.get_ptr());
    std::vector<std::string> names_b;
    Property_registry::get().for_each_property_of_type(type_b, [&](const Dependency_property& q) { names_b.emplace_back(q.get_name()); });
    EXPECT_NE(std::find(names_b.begin(), names_b.end(), "reg_add_owner"), names_b.end());
    Test_object b{type_b};
    EXPECT_EQ(b.get_value(p), 4);
}

TEST(Property_registry, read_only_key)
{
    const Property_key<int> key = Property_key<int>::register_read_only("reg_read_only", type_a, Property_metadata{.default_value = 10});
    const Property<int> p = key.get_property();
    EXPECT_TRUE(p.get().is_read_only());
    Test_object o;
    o.set_value(p.get(), Property_value{20}); // untyped write: rejected
    EXPECT_EQ(o.get_value(p), 10);
    o.set_value(key, 30);                     // key write: accepted
    EXPECT_EQ(o.get_value(p), 30);
    o.clear_value(p);                         // clear without key: rejected
    EXPECT_EQ(o.get_value(p), 30);
    o.clear_value(key);
    EXPECT_EQ(o.get_value(p), 10);
}
