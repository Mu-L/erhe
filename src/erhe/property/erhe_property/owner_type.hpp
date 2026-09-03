#pragma once

#include <cstdint>
#include <string_view>

namespace erhe::property {

// Per-class identity of a property owner (WPF DependencyObjectType). Ids
// form a tree: every id names its parent, and id 0 is the root that every
// Dependency_object belongs to. A property registered for an id applies to
// every descendant id; a registration on a descendant shadows an
// ancestor's registration of the same name.
//
// Ids are allocated in the registry's write window (static initialization
// and single-threaded early startup) and are run-local: nothing persists
// them, properties serialize by name. The owning class caches its id in a
// function-local static so static-initialization order across translation
// units cannot bite; a runtime-defined kind allocates one id per kind under
// its class id.
using Owner_type = uint32_t;

constexpr Owner_type root_owner_type = 0;

// Appends (parent, name) to the id table and returns the new id. `name`
// serves logs and tooling only.
[[nodiscard]] auto allocate_owner_type(Owner_type parent, std::string_view name) -> Owner_type;

// The parent of `id`; the root is its own parent.
[[nodiscard]] auto get_owner_type_parent(Owner_type id) -> Owner_type;

[[nodiscard]] auto get_owner_type_name(Owner_type id) -> std::string_view;

// True when `ancestor` is `id` or on the parent chain of `id`.
[[nodiscard]] auto is_owner_type_or_descendant(Owner_type id, Owner_type ancestor) -> bool;

} // namespace erhe::property
