#include "test_object.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<int> obs_int   = Property<int>::register_property("obs_int", type_a);
const Property<int> obs_other = Property<int>::register_property("obs_other", type_a);

} // anonymous namespace

TEST(Observers, observer_receives_only_its_property)
{
    Test_object o;
    int count = 0;
    int last_new = -1;
    Observer_token token = o.add_observer(obs_int.get(), [&](Dependency_object&, const Property_changed_args& args) {
        ++count;
        last_new = std::get<int>(args.new_value);
    });
    EXPECT_TRUE(token.is_active());
    o.set_value(obs_int, 1);
    o.set_value(obs_other, 2);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(last_new, 1);
}

TEST(Observers, token_destruction_and_release_unsubscribe)
{
    Test_object o;
    int count = 0;
    {
        Observer_token token = o.add_observer(obs_int.get(), [&](Dependency_object&, const Property_changed_args&) { ++count; });
        o.set_value(obs_int, 1);
    }
    o.set_value(obs_int, 2);
    EXPECT_EQ(count, 1);

    Observer_token token = o.add_observer(obs_int.get(), [&](Dependency_object&, const Property_changed_args&) { ++count; });
    o.set_value(obs_int, 3);
    token.release();
    EXPECT_FALSE(token.is_active());
    o.set_value(obs_int, 4);
    EXPECT_EQ(count, 2);
}

TEST(Observers, token_moves)
{
    Test_object o;
    int count = 0;
    Observer_token a = o.add_observer(obs_int.get(), [&](Dependency_object&, const Property_changed_args&) { ++count; });
    Observer_token b = std::move(a);
    EXPECT_FALSE(a.is_active());
    EXPECT_TRUE(b.is_active());
    o.set_value(obs_int, 1);
    EXPECT_EQ(count, 1);
    Observer_token c;
    c = std::move(b);
    o.set_value(obs_int, 2);
    EXPECT_EQ(count, 2);
}

TEST(Observers, object_destruction_deactivates_token)
{
    Observer_token token;
    {
        Test_object o;
        token = o.add_observer(obs_int.get(), [](Dependency_object&, const Property_changed_args&) {});
        EXPECT_TRUE(token.is_active());
    }
    EXPECT_FALSE(token.is_active());
    token.release(); // no crash
}

TEST(Observers, observer_may_unsubscribe_from_inside_callback)
{
    Test_object o;
    int count = 0;
    std::unique_ptr<Observer_token> token = std::make_unique<Observer_token>();
    *token = o.add_observer(obs_int.get(), [&](Dependency_object&, const Property_changed_args&) {
        ++count;
        token->release();
    });
    o.set_value(obs_int, 1);
    o.set_value(obs_int, 2);
    EXPECT_EQ(count, 1);
}

TEST(Observers, observers_run_after_virtual_hook_and_under_batches)
{
    Test_object o;
    std::size_t changes_seen_by_observer = 0;
    int calls = 0;
    Observer_token token = o.add_observer(obs_int.get(), [&](Dependency_object&, const Property_changed_args&) {
        ++calls;
        changes_seen_by_observer = o.changes.size();
    });
    o.set_value(obs_int, 1);
    EXPECT_EQ(changes_seen_by_observer, std::size_t{1}); // the hook had already recorded
    {
        const Dependency_object::Change_batch batch{o};
        o.set_value(obs_int, 2);
        o.set_value(obs_int, 3);
        EXPECT_EQ(calls, 1);
    }
    EXPECT_EQ(calls, 2);
}
