// Style layer (D25 in doc/property-system-plan.md): coerced > local >
// style > inherited > default; set_style notifies the changed non-local
// properties; a style value is inherited by descendants.

#include "test_object.hpp"

#include "erhe_property/property_style.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float> st_a   = Property<float>::register_property("st_a", type_a, Property_metadata{.default_value = 1.0f});
const Property<float> st_b   = Property<float>::register_property("st_b", type_a, Property_metadata{.default_value = 2.0f});
const Property<float> st_inh = Property<float>::register_property("st_inh", type_a, Property_metadata{.default_value = 0.0f, .inherits = true});

auto make_style(const std::string_view name, const float a, const std::optional<float> b = {}, const std::optional<float> inh = {}) -> std::shared_ptr<const Property_style>
{
    Property_set values;
    values.set(st_a.get(), Property_value{a});
    if (b.has_value()) {
        values.set(st_b.get(), Property_value{b.value()});
    }
    if (inh.has_value()) {
        values.set(st_inh.get(), Property_value{inh.value()});
    }
    return std::make_shared<const Property_style>(std::string{name}, std::move(values));
}

} // anonymous namespace

TEST(Style, precedence)
{
    Test_object o;
    EXPECT_TRUE(o.set_style(make_style("s", 10.0f, 20.0f)));
    EXPECT_EQ(o.get_value(st_a), 10.0f);
    EXPECT_EQ(o.get_value_source(st_a.get()), Value_source::style);
    EXPECT_FALSE(o.has_local_value(st_a.get()));
    EXPECT_FALSE(o.read_local_value(st_a).has_value());

    o.set_value(st_a, 5.0f); // local wins
    EXPECT_EQ(o.get_value(st_a), 5.0f);
    EXPECT_EQ(o.get_value_source(st_a.get()), Value_source::local);
    o.clear_value(st_a); // back to the style, not the default
    EXPECT_EQ(o.get_value(st_a), 10.0f);
    EXPECT_EQ(o.get_value_source(st_a.get()), Value_source::style);

    ASSERT_TRUE(o.set_expression(st_b.get(), "7")); // expression over style
    EXPECT_EQ(o.get_value(st_b), 7.0f);
    EXPECT_EQ(o.get_value_source(st_b.get()), Value_source::expression);
}

TEST(Style, set_style_notifies_changed_non_local_properties)
{
    Test_object o;
    o.set_value(st_b, 3.0f);
    o.changes.clear();

    ASSERT_TRUE(o.set_style(make_style("s1", 10.0f, 20.0f)));
    ASSERT_EQ(o.changes.size(), std::size_t{1}); // st_b is local: untouched
    EXPECT_EQ(o.changes[0].property_name, "st_a");
    EXPECT_EQ(std::get<float>(o.changes[0].old_value), 1.0f);
    EXPECT_EQ(std::get<float>(o.changes[0].new_value), 10.0f);
    EXPECT_EQ(o.changes[0].old_source, Value_source::default_value);
    EXPECT_EQ(o.changes[0].new_source, Value_source::style);
    EXPECT_EQ(o.get_value(st_b), 3.0f);

    // Swap: same value for st_a in the new style -> no notification for it.
    o.changes.clear();
    ASSERT_TRUE(o.set_style(make_style("s2", 10.0f)));
    EXPECT_TRUE(o.changes.empty());

    o.changes.clear();
    ASSERT_TRUE(o.set_style(make_style("s3", 11.0f)));
    ASSERT_EQ(o.changes.size(), std::size_t{1});
    EXPECT_EQ(o.changes[0].old_source, Value_source::style);
    EXPECT_EQ(o.changes[0].new_source, Value_source::style);
    EXPECT_EQ(std::get<float>(o.changes[0].new_value), 11.0f);

    // Clear: back to the default.
    o.changes.clear();
    ASSERT_TRUE(o.set_style(nullptr));
    ASSERT_EQ(o.changes.size(), std::size_t{1});
    EXPECT_EQ(std::get<float>(o.changes[0].new_value), 1.0f);
    EXPECT_EQ(o.changes[0].new_source, Value_source::default_value);
    EXPECT_FALSE(o.get_style());
}

TEST(Style, style_value_is_inherited_and_shadows_ancestors)
{
    Test_object root;
    Test_object mid;
    Test_object leaf;
    mid.set_parent(&root);
    leaf.set_parent(&mid);
    root.set_value(st_inh, 1.0f);
    leaf.changes.clear();

    ASSERT_TRUE(mid.set_style(make_style("s", 0.0f, {}, 5.0f)));
    EXPECT_EQ(mid.get_value(st_inh), 5.0f);
    EXPECT_EQ(mid.get_value_source(st_inh.get()), Value_source::style);
    EXPECT_EQ(leaf.get_value(st_inh), 5.0f);
    EXPECT_EQ(leaf.get_value_source(st_inh.get()), Value_source::inherited);
    EXPECT_EQ(leaf.change_count("st_inh"), std::size_t{1});

    // The ancestor's change stops at the styled child.
    leaf.changes.clear();
    root.set_value(st_inh, 2.0f);
    EXPECT_EQ(leaf.get_value(st_inh), 5.0f);
    EXPECT_EQ(leaf.change_count("st_inh"), std::size_t{0});

    // Reparenting the styled object keeps its value; its child follows it.
    Test_object other_root;
    other_root.set_value(st_inh, 9.0f);
    mid.set_parent(&other_root);
    EXPECT_EQ(mid.get_value(st_inh), 5.0f);
    EXPECT_EQ(leaf.get_value(st_inh), 5.0f);
    EXPECT_EQ(leaf.change_count("st_inh"), std::size_t{0});
}

TEST(Style, copy_carries_style_and_sealed_rejects)
{
    Test_object o;
    const std::shared_ptr<const Property_style> style = make_style("s", 10.0f);
    ASSERT_TRUE(o.set_style(style));

    Test_object copy{o};
    EXPECT_EQ(copy.get_style(), style);
    EXPECT_EQ(copy.get_value(st_a), 10.0f);

    o.seal();
    EXPECT_FALSE(o.set_style(nullptr));
    EXPECT_EQ(o.get_style(), style);
    o.unseal();
    EXPECT_TRUE(o.set_style(nullptr));
}
