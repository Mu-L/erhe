#include "brushes/brush_placement.hpp"
#include "brushes/brush.hpp"

#include <glm/glm.hpp>

namespace editor {

namespace {

using erhe::property::Dependency_object;
using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_bridge;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

// A GEO::index_t member as an int property; NO_INDEX reads as -1.
auto index_bridge(GEO::index_t Brush_placement::*member) -> Property_bridge
{
    return Property_bridge{
        .get = [member](const Dependency_object& object) -> Property_value {
            const GEO::index_t index = static_cast<const Brush_placement&>(object).*member;
            return (index == GEO::NO_INDEX) ? -1 : static_cast<int>(index);
        },
        .set = [member](Dependency_object& object, const Property_value& value) {
            const int index = std::get<int>(value);
            static_cast<Brush_placement&>(object).*member = (index < 0) ? GEO::NO_INDEX : static_cast<GEO::index_t>(index);
        }
    };
}

} // anonymous namespace

const Property<Object_reference> Brush_placement::brush_property = Property<Object_reference>::register_member(
    "brush", Brush_placement::property_owner_type(), &Brush_placement::m_brush,
    Property_metadata{.ui = Property_ui{.tooltip = "The brush this placement was made with", .label = "Brush", .reference_item_types = erhe::Item_type::brush}}
);
const Property<int> Brush_placement::facet_property = Property<int>::register_property(
    "facet", Brush_placement::property_owner_type(),
    Property_metadata{.default_value = -1, .ui = Property_ui{.developer_only = true, .label = "Facet"}, .bridge = index_bridge(&Brush_placement::m_facet)}
);
const Property<int> Brush_placement::corner_property = Property<int>::register_property(
    "corner", Brush_placement::property_owner_type(),
    Property_metadata{.default_value = -1, .ui = Property_ui{.developer_only = true, .label = "Corner"}, .bridge = index_bridge(&Brush_placement::m_corner)}
);

auto Brush_placement::get_brush() const -> std::shared_ptr<Brush>
{
    return m_brush;
}

auto Brush_placement::get_facet() const -> GEO::index_t
{
    return m_facet;
}

auto Brush_placement::get_corner() const -> GEO::index_t
{
    return m_corner;
}

void Brush_placement::set_corner(const GEO::index_t corner)
{
    set_value(corner_property, (corner == GEO::NO_INDEX) ? -1 : static_cast<int>(corner));
}

Brush_placement::Brush_placement(
    const std::shared_ptr<Brush>& brush,
    const GEO::index_t            facet,
    const GEO::index_t            corner
)
    : Item    {"brush placement"}
    , m_brush {brush}
    , m_facet {facet}
    , m_corner{corner}
{
}

Brush_placement::Brush_placement()
    : Item    {"brush placement"}
    , m_brush {}
    , m_facet {GEO::NO_INDEX}
    , m_corner{GEO::NO_INDEX}
{
}

Brush_placement::Brush_placement(const Brush_placement&) = default;
Brush_placement& Brush_placement::operator=(const Brush_placement&) = default;
Brush_placement::~Brush_placement() noexcept = default;

auto Brush_placement::clone() const -> std::shared_ptr<erhe::Item_base>
{
    // It doesn't make sense copy Brush_placement - does it?
    return std::shared_ptr<erhe::Item_base>{};
}

}
