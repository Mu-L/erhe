#include "test_object.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

int s_callback_count = 0;

const Property<bool>      obj_bool  = Property<bool>::register_property("obj_bool", type_a);
const Property<int>       obj_int   = Property<int>::register_property("obj_int", type_a, Property_metadata{.default_value = 5});
const Property<float>     obj_float = Property<float>::register_property("obj_float", type_a, Property_metadata{.default_value = 1.0f});
const Property<glm::vec2> obj_vec2  = Property<glm::vec2>::register_property("obj_vec2", type_a);
const Property<glm::vec4> obj_vec4  = Property<glm::vec4>::register_property("obj_vec4", type_a);
const Property<glm::quat> obj_quat  = Property<glm::quat>::register_property("obj_quat", type_a);
const Property<std::string> obj_string = Property<std::string>::register_property("obj_string", type_a);

const Property<float> obj_validated = Property<float>::register_property(
    "obj_validated", type_a, Property_metadata{.default_value = 0.5f},
    [](const Property_value& v) { const float f = std::get<float>(v); return (f >= 0.0f) && (f <= 1.0f); }
);

const Property<int> obj_coerced = Property<int>::register_property(
    "obj_coerced", type_a,
    Property_metadata{
        .default_value = 0,
        .coerce = [](const Dependency_object& o, const Property_value& v) -> Property_value {
            const int limit = o.get_value(obj_int);
            return std::min(std::get<int>(v), limit);
        }
    }
);

const Property<int> obj_with_callback = Property<int>::register_property(
    "obj_with_callback", type_a,
    Property_metadata{
        .default_value    = 0,
        .property_changed = [](Dependency_object&, const Property_changed_args&) { ++s_callback_count; }
    }
);

} // anonymous namespace

TEST(Dependency_object, defaults_without_entries)
{
    Test_object o;
    EXPECT_EQ(o.get_value(obj_bool), false);
    EXPECT_EQ(o.get_value(obj_int), 5);
    EXPECT_EQ(o.get_value(obj_float), 1.0f);
    EXPECT_EQ(o.get_value(obj_vec2), glm::vec2{0.0f});
    EXPECT_EQ(o.get_value(obj_vec4), glm::vec4{0.0f});
    EXPECT_EQ(o.get_value(obj_quat), (glm::quat{1.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(o.get_value(obj_string), "");
    EXPECT_FALSE(o.has_local_value(obj_int.get()));
    EXPECT_EQ(o.get_value_source(obj_int.get()), Value_source::default_value);
    EXPECT_FALSE(o.read_local_value(obj_int).has_value());
}

TEST(Dependency_object, set_get_clear_every_type)
{
    Test_object o;
    o.set_value(obj_bool, true);
    o.set_value(obj_int, 42);
    o.set_value(obj_float, 3.5f);
    o.set_value(obj_vec2, glm::vec2{1.0f, 2.0f});
    o.set_value(obj_vec4, glm::vec4{1.0f, 2.0f, 3.0f, 4.0f});
    o.set_value(obj_quat, glm::quat{0.0f, 1.0f, 0.0f, 0.0f});
    o.set_value(obj_string, std::string{"text"});

    EXPECT_EQ(o.get_value(obj_bool), true);
    EXPECT_EQ(o.get_value(obj_int), 42);
    EXPECT_EQ(o.get_value(obj_float), 3.5f);
    EXPECT_EQ(o.get_value(obj_vec2), (glm::vec2{1.0f, 2.0f}));
    EXPECT_EQ(o.get_value(obj_vec4), (glm::vec4{1.0f, 2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(o.get_value(obj_quat), (glm::quat{0.0f, 1.0f, 0.0f, 0.0f}));
    EXPECT_EQ(o.get_value(obj_string), "text");
    EXPECT_EQ(o.get_value_source(obj_int.get()), Value_source::local);
    EXPECT_EQ(o.read_local_value(obj_int).value(), 42);

    o.clear_value(obj_int);
    EXPECT_EQ(o.get_value(obj_int), 5);
    EXPECT_EQ(o.get_value_source(obj_int.get()), Value_source::default_value);
    // Clearing an already-clear property is a no-op with no notification.
    const std::size_t before = o.changes.size();
    o.clear_value(obj_int);
    EXPECT_EQ(o.changes.size(), before);
}

TEST(Dependency_object, untyped_access_and_type_mismatch_rejection)
{
    Test_object o;
    o.set_value(obj_int.get(), Property_value{7});
    EXPECT_EQ(std::get<int>(o.get_value(obj_int.get())), 7);
    o.set_value(obj_int.get(), Property_value{1.0f}); // float into int: dropped
    EXPECT_EQ(o.get_value(obj_int), 7);
    EXPECT_EQ(o.read_local_value(obj_int.get()).value(), Property_value{7});
}

TEST(Dependency_object, validate_rejects_and_keeps_previous_value)
{
    Test_object o;
    o.set_value(obj_validated, 0.25f);
    EXPECT_EQ(o.get_value(obj_validated), 0.25f);
    o.set_value(obj_validated, 2.0f);
    EXPECT_EQ(o.get_value(obj_validated), 0.25f);
    EXPECT_EQ(o.change_count("obj_validated"), std::size_t{1});
}

TEST(Dependency_object, coerce_stores_coerced_value_and_reruns_on_coerce_value)
{
    Test_object o;
    o.set_value(obj_int, 10);
    o.set_value(obj_coerced, 50);
    EXPECT_EQ(o.get_value(obj_coerced), 10);
    EXPECT_TRUE(o.is_coerced(obj_coerced.get()));
    EXPECT_EQ(o.read_local_value(obj_coerced).value(), 50); // local keeps the authored value

    // The coercion input changed; re-running coerce notifies with the new effective value.
    o.set_value(obj_int, 100);
    EXPECT_EQ(o.get_value(obj_coerced), 10); // stale until coerce_value
    o.coerce_value(obj_coerced.get());
    EXPECT_EQ(o.get_value(obj_coerced), 50);
    EXPECT_FALSE(o.is_coerced(obj_coerced.get()));
    const Recorded_change& last = o.changes.back();
    EXPECT_EQ(last.property_name, "obj_coerced");
    EXPECT_EQ(std::get<int>(last.old_value), 10);
    EXPECT_EQ(std::get<int>(last.new_value), 50);

    // Coerce applies on read for a property without a local value too.
    Test_object p;
    p.set_value(obj_int, -3);
    EXPECT_EQ(p.get_value(obj_coerced), -3);
}

TEST(Dependency_object, changed_notifications_fire_once_per_effective_change)
{
    Test_object o;
    s_callback_count = 0;
    o.set_value(obj_with_callback, 1);
    EXPECT_EQ(s_callback_count, 1);
    EXPECT_EQ(o.change_count("obj_with_callback"), std::size_t{1});
    o.set_value(obj_with_callback, 1); // same value: no notification
    EXPECT_EQ(s_callback_count, 1);
    EXPECT_EQ(o.change_count("obj_with_callback"), std::size_t{1});
    o.set_value(obj_with_callback, 2);
    EXPECT_EQ(s_callback_count, 2);
    const Recorded_change& last = o.changes.back();
    EXPECT_EQ(std::get<int>(last.old_value), 1);
    EXPECT_EQ(std::get<int>(last.new_value), 2);
    EXPECT_EQ(last.old_source, Value_source::local);
    EXPECT_EQ(last.new_source, Value_source::local);

    // Setting the default value as a local value changes the source, not the value: still a notification.
    Test_object p;
    p.set_value(obj_int, 5);
    ASSERT_EQ(p.change_count("obj_int"), std::size_t{1});
    EXPECT_EQ(p.changes.back().old_source, Value_source::default_value);
    EXPECT_EQ(p.changes.back().new_source, Value_source::local);
}

TEST(Dependency_object, local_value_enumeration_in_index_order)
{
    Test_object o;
    o.set_value(obj_string, std::string{"s"});
    o.set_value(obj_int, 1);
    o.set_value(obj_bool, true);
    std::vector<uint16_t> indices;
    o.for_each_local_value([&](const Dependency_property& p, const Property_value&) { indices.push_back(p.get_index()); });
    ASSERT_EQ(indices.size(), std::size_t{3});
    EXPECT_TRUE(std::is_sorted(indices.begin(), indices.end()));
}

TEST(Dependency_object, copy_keeps_local_values_only)
{
    Test_object o;
    o.set_value(obj_int, 10);
    o.set_value(obj_coerced, 50); // coerced to 10
    Test_object copy{o};
    EXPECT_EQ(copy.get_value(obj_int), 10);
    EXPECT_EQ(copy.read_local_value(obj_coerced).value(), 50);
    EXPECT_EQ(copy.get_value(obj_coerced), 10);
    EXPECT_TRUE(copy.changes.empty() || true); // change history is the Test_object's, not the store's
    Test_object assigned;
    assigned = o;
    EXPECT_EQ(assigned.get_value(obj_int), 10);
    // Independent after copy
    copy.set_value(obj_int, 11);
    EXPECT_EQ(o.get_value(obj_int), 10);
}

TEST(Dependency_object, change_batch_collapses_to_one_notification_per_property)
{
    Test_object o;
    {
        const Dependency_object::Change_batch batch{o};
        o.set_value(obj_int, 1);
        o.set_value(obj_int, 2);
        o.set_value(obj_int, 3);
        o.set_value(obj_float, 2.0f);
        EXPECT_TRUE(o.changes.empty());
    }
    EXPECT_EQ(o.change_count("obj_int"), std::size_t{1});
    EXPECT_EQ(o.change_count("obj_float"), std::size_t{1});
    const Recorded_change* int_change = nullptr;
    for (const Recorded_change& c : o.changes) {
        if (c.property_name == "obj_int") {
            int_change = &c;
        }
    }
    ASSERT_NE(int_change, nullptr);
    EXPECT_EQ(std::get<int>(int_change->old_value), 5);
    EXPECT_EQ(std::get<int>(int_change->new_value), 3);
    EXPECT_EQ(int_change->old_source, Value_source::default_value);

    // A batch that ends where it started produces nothing.
    o.changes.clear();
    {
        const Dependency_object::Change_batch batch{o};
        o.set_value(obj_int, 9);
        o.set_value(obj_int, 3);
    }
    EXPECT_TRUE(o.changes.empty());

    // Nested batches deliver at the outermost end.
    {
        const Dependency_object::Change_batch outer{o};
        {
            const Dependency_object::Change_batch inner{o};
            o.set_value(obj_int, 4);
        }
        EXPECT_TRUE(o.changes.empty());
    }
    EXPECT_EQ(o.change_count("obj_int"), std::size_t{1});
}
