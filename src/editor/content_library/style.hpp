#pragma once

#include "erhe_item/item.hpp"
#include "erhe_property/owner_type.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

class Content_library_node;

// A style item of the content library's Styles category
// (doc/style-library.md D2): a named holder of property values of one item
// class - its target owner type, which is also its secondary owner type
// (doc/property-system.md D30), so the Add Property picker offers that
// class's properties by qualified name and the values live in this item's
// own store. Items use it through their `style` property
// (Item_base::style_property); the style layer of every user reads this
// item's local values, live (D25).
class Style : public erhe::Item<erhe::Item_base, erhe::Item_base, Style>
{
public:
    Style(std::string_view name, erhe::property::Owner_type target_owner_type);
    explicit Style(const Style& other) = default;
    ~Style() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Style"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::style; }

    // Overrides Dependency_object: the target class's properties are this
    // item's secondary properties.
    [[nodiscard]] auto get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type> override;

    [[nodiscard]] auto get_target_owner_type() const -> erhe::property::Owner_type;

private:
    erhe::property::Owner_type m_target_owner_type;
};

// The owner types a style can target: every type with own value
// properties (Property_registry::has_own_value_properties), sorted by name.
// Clears and fills `out`.
void collect_style_target_owner_types(std::vector<erhe::property::Owner_type>& out);

// A name no style in the folder uses: `base_name`, else `base_name (N)`.
[[nodiscard]] auto make_unique_style_name(const Content_library_node& styles_folder, std::string_view base_name) -> std::string;

}
