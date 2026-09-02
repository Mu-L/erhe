#include "test_object.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float> inh_float = Property<float>::register_property("inh_float", type_a, Property_metadata{.default_value = 1.0f, .inherits = true});
const Property<int>   inh_plain = Property<int>::register_property("inh_plain", type_a, Property_metadata{.default_value = 0});
const Property<float> inh_coerced = Property<float>::register_property(
    "inh_coerced", type_a,
    Property_metadata{
        .default_value = 0.0f,
        .coerce   = [](const Dependency_object&, const Property_value& v) -> Property_value { return std::min(std::get<float>(v), 10.0f); },
        .inherits = true
    }
);

} // anonymous namespace

TEST(Inheritance, reads_through_three_levels)
{
    Test_object root, mid, leaf;
    mid.set_parent(&root);
    leaf.set_parent(&mid);
    EXPECT_EQ(leaf.get_value(inh_float), 1.0f);
    EXPECT_EQ(leaf.get_value_source(inh_float.get()), Value_source::default_value);

    root.set_value(inh_float, 5.0f);
    EXPECT_EQ(mid.get_value(inh_float), 5.0f);
    EXPECT_EQ(leaf.get_value(inh_float), 5.0f);
    EXPECT_EQ(leaf.get_value_source(inh_float.get()), Value_source::inherited);
    EXPECT_FALSE(leaf.has_local_value(inh_float.get()));

    // Non-inherits property does not flow down.
    root.set_value(inh_plain, 7);
    EXPECT_EQ(leaf.get_value(inh_plain), 0);
}

TEST(Inheritance, descendants_are_notified_on_ancestor_set_and_clear)
{
    Test_object root, mid, leaf;
    mid.set_parent(&root);
    leaf.set_parent(&mid);

    root.set_value(inh_float, 5.0f);
    ASSERT_EQ(mid.change_count("inh_float"), std::size_t{1});
    ASSERT_EQ(leaf.change_count("inh_float"), std::size_t{1});
    EXPECT_EQ(std::get<float>(leaf.changes.back().old_value), 1.0f);
    EXPECT_EQ(std::get<float>(leaf.changes.back().new_value), 5.0f);
    EXPECT_EQ(leaf.changes.back().new_source, Value_source::inherited);

    root.clear_value(inh_float);
    ASSERT_EQ(leaf.change_count("inh_float"), std::size_t{2});
    EXPECT_EQ(std::get<float>(leaf.changes.back().new_value), 1.0f);
    EXPECT_EQ(leaf.get_value(inh_float), 1.0f);
}

TEST(Inheritance, local_value_stops_propagation)
{
    Test_object root, mid, leaf;
    mid.set_parent(&root);
    leaf.set_parent(&mid);
    mid.set_value(inh_float, 2.0f);
    mid.changes.clear();
    leaf.changes.clear();

    root.set_value(inh_float, 5.0f);
    EXPECT_EQ(mid.get_value(inh_float), 2.0f);
    EXPECT_EQ(leaf.get_value(inh_float), 2.0f);
    EXPECT_TRUE(mid.changes.empty());
    EXPECT_TRUE(leaf.changes.empty());
}

TEST(Inheritance, reparent_notifies_subtree_with_old_and_new_values)
{
    Test_object a, b, child, grandchild;
    a.set_value(inh_float, 3.0f);
    b.set_value(inh_float, 4.0f);
    child.set_parent(&a);
    grandchild.set_parent(&child);
    EXPECT_EQ(grandchild.get_value(inh_float), 3.0f);
    child.changes.clear();
    grandchild.changes.clear();

    child.set_parent(&b);
    EXPECT_EQ(child.get_value(inh_float), 4.0f);
    EXPECT_EQ(grandchild.get_value(inh_float), 4.0f);
    ASSERT_EQ(child.change_count("inh_float"), std::size_t{1});
    ASSERT_EQ(grandchild.change_count("inh_float"), std::size_t{1});
    EXPECT_EQ(std::get<float>(grandchild.changes.back().old_value), 3.0f);
    EXPECT_EQ(std::get<float>(grandchild.changes.back().new_value), 4.0f);

    // Detaching falls back to the default.
    child.set_parent(nullptr);
    EXPECT_EQ(grandchild.get_value(inh_float), 1.0f);
    EXPECT_EQ(grandchild.changes.back().new_source, Value_source::default_value);

    // Reparenting between parents with equal values notifies nothing.
    Test_object c;
    c.set_value(inh_float, 4.0f);
    child.set_parent(&b);
    child.changes.clear();
    child.set_parent(&c);
    EXPECT_TRUE(child.changes.empty());
}

TEST(Inheritance, coerce_applies_to_inherited_values)
{
    Test_object root, leaf;
    leaf.set_parent(&root);
    root.set_value(inh_coerced, 50.0f);
    EXPECT_EQ(root.get_value(inh_coerced), 10.0f);
    EXPECT_EQ(leaf.get_value(inh_coerced), 10.0f);
    ASSERT_EQ(leaf.change_count("inh_coerced"), std::size_t{1});
    EXPECT_EQ(std::get<float>(leaf.changes.back().new_value), 10.0f);
}

TEST(Inheritance, clone_keeps_local_values_only)
{
    Test_object root, leaf;
    leaf.set_parent(&root);
    root.set_value(inh_float, 5.0f);
    Test_object clone{leaf}; // copy has no parent link in Test_object semantics? It copies m_parent pointer; verify store only.
    EXPECT_FALSE(clone.has_local_value(inh_float.get()));
}
