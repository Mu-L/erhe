#include "brushes/brush_placement.hpp"
#include "brushes/brush.hpp"

#include <glm/glm.hpp>

namespace editor {

namespace {

using erhe::property::Dependency_object;
using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

// GEO::index_t <-> the int property value; NO_INDEX is -1.
auto to_index_value(const GEO::index_t index) -> int
{
    return (index == GEO::NO_INDEX) ? -1 : static_cast<int>(index);
}

auto from_index_value(const int value) -> GEO::index_t
{
    return (value < 0) ? GEO::NO_INDEX : static_cast<GEO::index_t>(value);
}

// The brush reference is null or names a Brush.
auto validate_brush(const Property_value& value) -> bool
{
    const Object_reference& reference = std::get<Object_reference>(value);
    return !reference.object || (dynamic_cast<const Brush*>(reference.object.get()) != nullptr);
}

} // anonymous namespace

// Entry-stored, inheriting (section 4.11): the members are a mirror
// refreshed by Brush_placement::on_property_changed.
const Property<Object_reference> Brush_placement::brush_property = Property<Object_reference>::register_property(
    "brush", Brush_placement::property_owner_type(),
    Property_metadata{.inherits = true, .ui = Property_ui{.tooltip = "The brush this placement was made with", .label = "Brush", .reference_item_types = erhe::Item_type::brush}},
    validate_brush
);
const Property<int> Brush_placement::facet_property = Property<int>::register_property(
    "facet", Brush_placement::property_owner_type(),
    Property_metadata{.default_value = -1, .inherits = true, .ui = Property_ui{.developer_only = true, .label = "Facet"}}
);
const Property<int> Brush_placement::corner_property = Property<int>::register_property(
    "corner", Brush_placement::property_owner_type(),
    Property_metadata{.default_value = -1, .inherits = true, .ui = Property_ui{.developer_only = true, .label = "Corner"}}
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
    set_value(corner_property, to_index_value(corner));
}

void Brush_placement::on_property_changed(const erhe::property::Property_changed_args& args)
{
    if (erhe::property::is_owner_type_or_descendant(Brush_placement::property_owner_type(), args.property.get_owner_type())) {
        refresh_mirror();
    }
}

void Brush_placement::refresh_mirror()
{
    m_brush  = std::dynamic_pointer_cast<Brush>(get_value(brush_property).object);
    m_facet  = from_index_value(get_value(facet_property));
    m_corner = from_index_value(get_value(corner_property));
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
    // Local values only where the arguments differ from the property
    // defaults, so a default-constructed placement stays open to a holder.
    if (brush) {
        set_value(brush_property, Object_reference{brush});
    }
    if (facet != GEO::NO_INDEX) {
        set_value(facet_property, to_index_value(facet));
    }
    if (corner != GEO::NO_INDEX) {
        set_value(corner_property, to_index_value(corner));
    }
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
