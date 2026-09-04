#include "windows/attached_property_listing.hpp"

#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"

namespace editor {

auto is_extra_property_listed(
    const erhe::property::Dependency_object&   object,
    const erhe::property::Dependency_property& property
) -> bool
{
    if (!property.is_attached()) {
        return
            erhe::property::Property_registry::get().is_secondary_property(object, property) &&
            object.has_local_value(property);
    }
    const erhe::property::Property_ui::Visible_when& visible_when = property.get_metadata(object.get_property_owner_type()).ui.visible_when;
    return (visible_when && visible_when(object)) || object.has_local_value(property);
}

void collect_addable_properties(
    const erhe::property::Dependency_object&                 object,
    const Developer_mode                                     developer_mode,
    std::vector<const erhe::property::Dependency_property*>& out
)
{
    const erhe::property::Owner_type owner_type = object.get_property_owner_type();
    const auto consider = [&object, owner_type, developer_mode, &out](const erhe::property::Dependency_property& property) {
        if ((developer_mode == Developer_mode::hidden) && property.get_metadata(owner_type).ui.developer_only) {
            return;
        }
        if (is_extra_property_listed(object, property)) {
            return;
        }
        out.push_back(&property);
    };
    const erhe::property::Property_registry& registry = erhe::property::Property_registry::get();
    registry.for_each_attached_property(consider);
    registry.for_each_secondary_property(object, consider);
}

}
