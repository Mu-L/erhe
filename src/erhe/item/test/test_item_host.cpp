#include "erhe_item/item.hpp"
#include "erhe_item/item_host.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

class Concrete_item_host : public erhe::Item_host
{
public:
    auto get_host_name() const -> const char* override { return "test_host"; }
};

class Hosted_item : public erhe::Item<erhe::Item_base, erhe::Item_base, Hosted_item>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Hosted_item"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::mesh; }

    auto get_item_host() const -> erhe::Item_host* override { return m_host; }

    erhe::Item_host* m_host{nullptr};
};

class Plain_item : public erhe::Item<erhe::Item_base, erhe::Item_base, Plain_item>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Plain_item"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::camera; }
};

TEST(ItemHost, ResolveItemHost_FirstNonNull)
{
    Concrete_item_host host;
    auto a = std::make_shared<Hosted_item>("a");
    auto b = std::make_shared<Plain_item>("b");
    a->m_host = &host;

    erhe::Item_host* resolved = erhe::resolve_item_host(a.get(), b.get(), nullptr);
    EXPECT_EQ(resolved, &host);
}

TEST(ItemHost, ResolveItemHost_SecondItem)
{
    Concrete_item_host host;
    auto a = std::make_shared<Plain_item>("a");
    auto b = std::make_shared<Hosted_item>("b");
    b->m_host = &host;

    erhe::Item_host* resolved = erhe::resolve_item_host(a.get(), b.get(), nullptr);
    EXPECT_EQ(resolved, &host);
}

TEST(ItemHost, ResolveItemHost_AllNull)
{
    auto a = std::make_shared<Plain_item>("a");
    auto b = std::make_shared<Plain_item>("b");

    erhe::Item_host* resolved = erhe::resolve_item_host(a.get(), b.get(), nullptr);
    EXPECT_EQ(resolved, nullptr);
}

TEST(ItemHost, ResolveItemHost_NullptrItems)
{
    erhe::Item_host* resolved = erhe::resolve_item_host(nullptr, nullptr, nullptr);
    EXPECT_EQ(resolved, nullptr);
}

TEST(ItemHost, LockGuardConstructs)
{
    Concrete_item_host host;
    auto a = std::make_shared<Hosted_item>("a");
    a->m_host = &host;

    // Should construct and destruct without deadlock or crash
    { erhe::Item_host_lock_guard guard(a.get()); }
}

TEST(ItemHost, LockGuardWithNoHost)
{
    auto a = std::make_shared<Plain_item>("a");

    // Uses orphan_item_host_mutex; should not crash
    { erhe::Item_host_lock_guard guard(a.get()); }
}

TEST(ItemHost, SetItemHost_DefaultGetterReturnsIt)
{
    Concrete_item_host host;
    auto a = std::make_shared<Plain_item>("a");
    EXPECT_EQ(a->get_item_host(), nullptr);

    a->set_item_host(&host);
    EXPECT_EQ(a->get_item_host(), &host);
    EXPECT_EQ(erhe::resolve_item_host(a.get(), nullptr, nullptr), &host);

    a->set_item_host(nullptr);
    EXPECT_EQ(a->get_item_host(), nullptr);
}

// An item that owns further items outside its container's view forwards
// the host to them (Graph_asset forwards to its graph nodes).
class Forwarding_item : public erhe::Item<erhe::Item_base, erhe::Item_base, Forwarding_item>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Forwarding_item"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::light; }

    void set_item_host(erhe::Item_host* item_host) override
    {
        erhe::Item_base::set_item_host(item_host);
        for (const std::shared_ptr<Plain_item>& child : children) {
            child->set_item_host(item_host);
        }
    }

    std::vector<std::shared_ptr<Plain_item>> children;
};

TEST(ItemHost, SetItemHost_VirtualForwarding)
{
    Concrete_item_host host;
    auto parent = std::make_shared<Forwarding_item>("parent");
    auto child  = std::make_shared<Plain_item>("child");
    parent->children.push_back(child);

    // Through the base pointer: the container calls set_item_host on an
    // Item_base and the override must run (Content_library does this).
    erhe::Item_base* base = parent.get();
    base->set_item_host(&host);
    EXPECT_EQ(parent->get_item_host(), &host);
    EXPECT_EQ(child->get_item_host(), &host);

    base->set_item_host(nullptr);
    EXPECT_EQ(parent->get_item_host(), nullptr);
    EXPECT_EQ(child->get_item_host(), nullptr);
}

TEST(ItemHost, SetItemHost_NotCopied)
{
    Concrete_item_host host;
    auto a = std::make_shared<Plain_item>("a");
    a->set_item_host(&host);

    // A copy starts outside any owning container.
    auto b = std::make_shared<Plain_item>(*a);
    EXPECT_EQ(b->get_item_host(), nullptr);
    EXPECT_EQ(a->get_item_host(), &host);
}

} // namespace
