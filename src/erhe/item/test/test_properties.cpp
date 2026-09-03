// Item_base as an erhe::property::Dependency_object: metadata resolved by
// item type, inheritance through Hierarchy, clone semantics.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using namespace erhe::property;

// Test item types with distinct Item_type bits (far above the real ones).
constexpr uint64_t c_type_widget = uint64_t{1} << 55;
constexpr uint64_t c_type_gadget = uint64_t{1} << 56;

class Widget : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Widget>
{
public:
    explicit Widget(const std::string_view name) : Item{name} {}
    explicit Widget(const Widget& other) = default;
    static constexpr std::string_view static_type_name{"Widget"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return c_type_widget; }

    std::vector<std::string> changed_names;

protected:
    void on_property_changed(const Property_changed_args& args) override
    {
        changed_names.emplace_back(args.property.get_name());
    }
};

class Gadget : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Gadget>
{
public:
    explicit Gadget(const std::string_view name) : Item{name} {}
    explicit Gadget(const Gadget& other) = default;
    static constexpr std::string_view static_type_name{"Gadget"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return c_type_gadget; }
};

const Property<float> widget_tint  = Property<float>::register_property("tint", Widget::property_owner_type(), Property_metadata{.default_value = 1.0f, .inherits = true});
const Property<int>   widget_count = Property<int>::register_property("count", Widget::property_owner_type(), Property_metadata{.default_value = 2});

// The same property name registered for another type is a distinct property.
const Property<float> gadget_tint = Property<float>::register_property("tint", Gadget::property_owner_type(), Property_metadata{.default_value = 9.0f});

} // anonymous namespace

TEST(Item_properties, item_type_drives_metadata_and_lookup)
{
    auto widget = std::make_shared<Widget>("w");
    auto gadget = std::make_shared<Gadget>("g");
    EXPECT_EQ(widget->get_property_owner_type(), Widget::property_owner_type());
    EXPECT_EQ(get_owner_type_parent(Widget::property_owner_type()), erhe::Hierarchy::property_owner_type());
    EXPECT_EQ(get_owner_type_parent(erhe::Hierarchy::property_owner_type()), erhe::Item_base::property_owner_type());
    EXPECT_EQ(widget->get_value(widget_tint), 1.0f);
    EXPECT_EQ(gadget->get_value(gadget_tint), 9.0f);
    EXPECT_EQ(Property_registry::get().find(Widget::property_owner_type(), "tint"), widget_tint.get_ptr());
    EXPECT_EQ(Property_registry::get().find(Gadget::property_owner_type(), "tint"), gadget_tint.get_ptr());
    // A base item property is found for a derived item through the chain.
    EXPECT_EQ(Property_registry::get().find_for_object(Widget::property_owner_type(), "visible"), erhe::Item_base::visible_property.get_ptr());
    EXPECT_EQ(Property_registry::get().find_for_object(Widget::property_owner_type(), "child_count"), erhe::Hierarchy::child_count_property.get_ptr());

    std::vector<std::string> names;
    // Item_base's and Hierarchy's properties (visible, shadow_cast,
    // lightmapped, child_count) are listed before Widget's own; keep only
    // Widget's.
    Property_registry::get().for_each_property_of_object(
        widget->get_property_owner_type(),
        [&](const Dependency_property& p) {
            if (p.get_owner_type() == Widget::property_owner_type()) {
                names.emplace_back(p.get_name());
            }
        }
    );
    ASSERT_EQ(names.size(), std::size_t{2});
    EXPECT_EQ(names[0], "tint");
    EXPECT_EQ(names[1], "count");
}

TEST(Item_properties, inherits_through_hierarchy_three_levels)
{
    auto root = std::make_shared<Widget>("root");
    auto mid  = std::make_shared<Widget>("mid");
    auto leaf = std::make_shared<Widget>("leaf");
    mid->set_parent(root);
    leaf->set_parent(mid);

    root->set_value(widget_tint, 0.5f);
    EXPECT_EQ(mid->get_value(widget_tint), 0.5f);
    EXPECT_EQ(leaf->get_value(widget_tint), 0.5f);
    EXPECT_EQ(leaf->get_value_source(widget_tint.get()), Value_source::inherited);
    EXPECT_EQ(leaf->changed_names.size(), std::size_t{1});

    // A local value in the middle shadows the subtree below it.
    mid->set_value(widget_tint, 0.25f);
    root->set_value(widget_tint, 0.75f);
    EXPECT_EQ(leaf->get_value(widget_tint), 0.25f);
    EXPECT_EQ(leaf->changed_names.size(), std::size_t{2}); // mid's change, not root's second one

    root->clear_value(widget_tint);
    EXPECT_EQ(mid->get_value(widget_tint), 0.25f);
    mid->clear_value(widget_tint);
    EXPECT_EQ(leaf->get_value(widget_tint), 1.0f);
    EXPECT_EQ(leaf->get_value_source(widget_tint.get()), Value_source::default_value);
}

TEST(Item_properties, reparent_re_reads_inherited_values)
{
    auto a     = std::make_shared<Widget>("a");
    auto b     = std::make_shared<Widget>("b");
    auto child = std::make_shared<Widget>("child");
    auto grand = std::make_shared<Widget>("grand");
    a->set_value(widget_tint, 0.1f);
    b->set_value(widget_tint, 0.2f);
    child->set_parent(a);
    grand->set_parent(child);
    child->changed_names.clear();
    grand->changed_names.clear();

    child->set_parent(b);
    EXPECT_EQ(child->get_value(widget_tint), 0.2f);
    EXPECT_EQ(grand->get_value(widget_tint), 0.2f);
    EXPECT_EQ(child->changed_names.size(), std::size_t{1});
    EXPECT_EQ(grand->changed_names.size(), std::size_t{1});

    // Repositioning within the same parent is not a parent change.
    auto sibling = std::make_shared<Widget>("sibling");
    sibling->set_parent(b);
    child->changed_names.clear();
    child->set_parent(b, 0);
    EXPECT_TRUE(child->changed_names.empty());

    // remove() splices the node out and reparents its children to the
    // grandparent: grand now inherits from b directly.
    grand->changed_names.clear();
    child->remove();
    EXPECT_EQ(grand->get_parent().lock().get(), b.get());
    EXPECT_EQ(grand->get_value(widget_tint), 0.2f);
    EXPECT_TRUE(grand->changed_names.empty());

    child->set_parent(nullptr);
    grand->set_parent(nullptr);
    EXPECT_EQ(grand->get_value(widget_tint), 1.0f);
    EXPECT_EQ(grand->changed_names.size(), std::size_t{1});
}

TEST(Item_properties, clone_keeps_local_values_only)
{
    auto root = std::make_shared<Widget>("root");
    auto leaf = std::make_shared<Widget>("leaf");
    leaf->set_parent(root);
    root->set_value(widget_tint, 0.5f);
    leaf->set_value(widget_count, 7);

    std::shared_ptr<erhe::Item_base> clone_base = leaf->clone();
    auto clone = std::dynamic_pointer_cast<Widget>(clone_base);
    ASSERT_TRUE(clone);
    EXPECT_EQ(clone->get_value(widget_count), 7);
    EXPECT_EQ(clone->read_local_value(widget_count).value(), 7);
    EXPECT_FALSE(clone->has_local_value(widget_tint.get()));
    EXPECT_EQ(clone->get_value(widget_tint), 1.0f); // orphan clone: default, not root's value

    clone->set_parent(root);
    EXPECT_EQ(clone->get_value(widget_tint), 0.5f);
}
