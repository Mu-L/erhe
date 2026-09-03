#include "test_object.hpp"
#include "erhe_property/property_set.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<int>   set_int   = Property<int>::register_property("set_int", type_a());
const Property<float> set_float = Property<float>::register_property("set_float", type_a(), Property_metadata{.default_value = 1.0f});
const Property<bool>  set_bool  = Property<bool>::register_property("set_bool", type_a());

} // anonymous namespace

TEST(Property_set, read_apply_and_equality)
{
    Test_object a;
    a.set_value(set_int, 3);
    a.set_value(set_float, 2.0f);
    const Property_set bag = Property_set::read_local_values(a);
    EXPECT_EQ(bag.size(), std::size_t{2});
    EXPECT_EQ(bag.find(set_int.get()).value(), Property_value{3});
    EXPECT_FALSE(bag.contains(set_bool.get()));

    Test_object b;
    bag.apply(b);
    EXPECT_EQ(b.get_value(set_int), 3);
    EXPECT_EQ(b.get_value(set_float), 2.0f);
    EXPECT_EQ(Property_set::read_local_values(b), bag);
    // apply runs in one batch: one notification per property
    EXPECT_EQ(b.change_count("set_int"), std::size_t{1});
    EXPECT_EQ(b.change_count("set_float"), std::size_t{1});
}

TEST(Property_set, set_remove_keep_index_order)
{
    Property_set bag;
    bag.set(set_bool.get(), true);
    bag.set(set_int.get(), 1);
    bag.set(set_float.get(), 4.0f);
    bag.set(set_int.get(), 2); // replaces
    ASSERT_EQ(bag.size(), std::size_t{3});
    const std::vector<Property_set::Entry>& entries = bag.entries();
    EXPECT_LT(entries[0].property->get_index(), entries[1].property->get_index());
    EXPECT_LT(entries[1].property->get_index(), entries[2].property->get_index());
    EXPECT_EQ(bag.find(set_int.get()).value(), Property_value{2});
    bag.remove(set_int.get());
    EXPECT_FALSE(bag.contains(set_int.get()));
    bag.remove(set_int.get()); // no-op
    bag.clear();
    EXPECT_TRUE(bag.empty());
}

TEST(Property_set, diff_lists_changed_and_new_entries)
{
    Property_set before, after;
    before.set(set_int.get(), 1);
    before.set(set_float.get(), 1.0f);
    before.set(set_bool.get(), true);
    after.set(set_int.get(), 1);      // unchanged
    after.set(set_float.get(), 2.0f); // changed
    // set_bool missing in after: not listed (diff is "what after has")
    const Property_set d = Property_set::diff(before, after);
    ASSERT_EQ(d.size(), std::size_t{1});
    EXPECT_EQ(d.entries()[0].property, set_float.get_ptr());

    Property_set empty;
    const Property_set all_new = Property_set::diff(empty, after);
    EXPECT_EQ(all_new, after);
    EXPECT_TRUE(Property_set::diff(after, after).empty());
}
