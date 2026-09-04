#pragma once

#include <vector>

namespace erhe::property { class Dependency_object; class Dependency_property; }

namespace editor {

enum class Developer_mode : unsigned int {
    hidden = 0, // developer_only registrations are left out
    shown  = 1
};

// The one place for the attached-property rules of the Properties window
// and the MCP property tools (doc/property-system.md R7, D12, D13).

// D12 listing rule for one object: the attached property is listed when
// the registering type's visible_when holds for the object, or the object
// holds a local value for it (a stale hint stays visible and resettable).
[[nodiscard]] auto is_attached_property_listed(
    const erhe::property::Dependency_object&   object,
    const erhe::property::Dependency_property& property
) -> bool;

// The attached registrations "Add Property" offers for the object: every
// attached property the D12 rule does not list for it, in registry order;
// developer_only ones only in developer mode. Appends to `out` (the caller
// keeps the capacity).
void collect_addable_attached_properties(
    const erhe::property::Dependency_object&                 object,
    Developer_mode                                           developer_mode,
    std::vector<const erhe::property::Dependency_property*>& out
);

}
