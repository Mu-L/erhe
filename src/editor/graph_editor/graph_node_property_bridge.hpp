#pragma once

namespace editor {

// The after_set hook of a graph node's member-backed property
// (Property<T>::register_member, property-system D18 / D27): the member
// stays the storage, so evaluate(), write_parameters / read_parameters and
// the shadow-clone evaluation snapshot are untouched, and every property
// write (generic row, MCP set_item_property, an expression delivery) lands
// in the member and re-evaluates the graph through mark_dirty(). Use from
// the static Property registration of the node class (a static member
// initializer has class access, so a private member pointer works - the
// Node transform bridge pattern).
inline constexpr auto mark_node_dirty = [](auto& node) { node.mark_dirty(); };

} // namespace editor
