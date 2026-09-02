#pragma once

#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace erhe {

class Item_base;

class Item_host
{
public:
    virtual ~Item_host() noexcept;

    [[nodiscard]] virtual auto get_host_name() const -> const char* = 0;

    // The hosted item named `name`, for expression references
    // (doc/property-system.md D22); nullptr when the host has no item
    // of that name or does no lookup. Scene_host walks the scene's nodes
    // and attachments; the editor's Scene_root adds the content library.
    [[nodiscard]] virtual auto find_hosted_item(std::string_view name) -> Item_base* { static_cast<void>(name); return nullptr; }

    ERHE_PROFILE_MUTEX(std::mutex, item_host_mutex);
    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, orphan_item_host_mutex);

    // Per-host selection bucket: the items of the editor selection hosted by
    // this Item_host. Not continuously maintained - the editor's Selection
    // clears and refills it (capacity retained) each time a per-host view is
    // requested, deriving the contents from the authoritative selection at
    // query time so a host change while selected (e.g. a cross-scene
    // reparent) can never leave a stale bucket here. Always access through
    // Selection::get_hosted_selection().
    std::vector<std::shared_ptr<Item_base>> hosted_selection;
};

auto resolve_item_host(const Item_base* a, const Item_base* b, const Item_base* c) -> Item_host*;

auto resolve_item_host_mutex(const Item_base* a, const Item_base* b, const Item_base* c) -> ERHE_PROFILE_LOCKABLE_BASE(std::mutex)&;

class Item_host_lock_guard
{
public:
    explicit Item_host_lock_guard(const Item_base* a, const Item_base* b = nullptr, const Item_base* c = nullptr);

private:
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> m_lock;
};

} // namespace erhe
