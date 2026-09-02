#pragma once

#include "graph_editor/graph_editor_node.hpp"

#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_value.hpp"

namespace editor {

// Property_bridge over a graph node's own member (property-system D18 /
// D27): the member stays the storage, so evaluate(), write_parameters /
// read_parameters and the shadow-clone evaluation snapshot are untouched,
// and every property write (generic row, MCP set_item_property, an
// expression delivery) lands in the member and re-evaluates the graph
// through mark_dirty(). Use from the static Property registration of the
// node class (a static member initializer has class access, so a private
// member pointer works - the Node transform bridge pattern).
template <typename NodeT, typename T>
[[nodiscard]] auto make_node_member_bridge(T NodeT::* member) -> erhe::property::Property_bridge
{
    return erhe::property::Property_bridge{
        .get = [member](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
            return erhe::property::make_value<T>(static_cast<const NodeT&>(object).*member);
        },
        .set = [member](erhe::property::Dependency_object& object, const erhe::property::Property_value& value) {
            NodeT& node = static_cast<NodeT&>(object);
            node.*member = erhe::property::get_as<T>(value);
            node.mark_dirty();
        }
    };
}

} // namespace editor
