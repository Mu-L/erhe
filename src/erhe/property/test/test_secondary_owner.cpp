// Secondary owner type (doc/property-system.md D30): an object that names
// a second owner type holds that type's non-bridged properties by qualified
// name, and its inheritance descendants of that type read them.

#include "test_object.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float> sec_tint  = Property<float>::register_property("sec_tint", type_b(), Property_metadata{.default_value = 1.0f, .inherits = true});
const Property<float> sec_plain = Property<float>::register_property("sec_plain", type_b(), Property_metadata{.default_value = 3.0f});

// A type_a object that also holds type_b properties (a folder of type_b entries).
class Folder_object : public Test_object
{
public:
    Folder_object() : Test_object{type_a()} {}
    auto get_secondary_property_owner_type() const -> std::optional<Owner_type> override { return type_b(); }
};

} // anonymous namespace

TEST(Secondary_owner, lookup_naming_and_listing)
{
    const Property_registry& registry = Property_registry::get();
    Folder_object folder;
    Test_object   plain{type_a()};

    EXPECT_TRUE (registry.is_secondary_property(folder, sec_tint.get()));
    EXPECT_FALSE(registry.is_secondary_property(plain,  sec_tint.get()));

    // Qualified on the folder, plain on a type_b object.
    Test_object entry{type_b()};
    EXPECT_EQ(registry.qualified_name(folder, sec_tint.get()), "type_b.sec_tint");
    EXPECT_EQ(registry.qualified_name(entry,  sec_tint.get()), "sec_tint");

    EXPECT_EQ(registry.find_for_object(folder, "type_b.sec_tint"), sec_tint.get_ptr());
    EXPECT_EQ(registry.find_for_object(folder, "sec_tint"),        nullptr); // not on the folder's own chain
    EXPECT_EQ(registry.find_for_object(plain,  "type_b.sec_tint"), nullptr); // no secondary type

    std::vector<const Dependency_property*> listed;
    registry.for_each_secondary_property(folder, [&listed](const Dependency_property& p) { listed.push_back(&p); });
    EXPECT_NE(std::find(listed.begin(), listed.end(), sec_tint.get_ptr()),  listed.end());
    EXPECT_NE(std::find(listed.begin(), listed.end(), sec_plain.get_ptr()), listed.end());
    listed.clear();
    registry.for_each_secondary_property(plain, [&listed](const Dependency_property& p) { listed.push_back(&p); });
    EXPECT_TRUE(listed.empty());
}

TEST(Secondary_owner, folder_value_is_inherited_by_entries)
{
    Folder_object folder;
    Test_object   entry{type_b()};
    entry.set_parent(&folder);

    EXPECT_EQ(entry.get_value(sec_tint), 1.0f);
    folder.set_value(sec_tint, 0.25f);
    EXPECT_EQ(entry.get_value(sec_tint), 0.25f);
    EXPECT_EQ(entry.get_value_source(sec_tint.get()), Value_source::inherited);
    EXPECT_EQ(folder.get_value_source(sec_tint.get()), Value_source::local);

    // A local value on the entry shadows the folder; clearing it re-reads.
    entry.set_value(sec_tint, 0.5f);
    EXPECT_EQ(entry.get_value(sec_tint), 0.5f);
    entry.clear_value(sec_tint);
    EXPECT_EQ(entry.get_value(sec_tint), 0.25f);
    folder.clear_value(sec_tint);
    EXPECT_EQ(entry.get_value(sec_tint), 1.0f);
}
