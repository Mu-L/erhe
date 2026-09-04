#include "content_library/style.hpp"

#include "content_library/content_library.hpp"

#include <algorithm>
#include <set>

namespace editor {

Style::Style(const std::string_view name, const erhe::property::Owner_type target_owner_type)
    : Item               {name}
    , m_target_owner_type{target_owner_type}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

Style::~Style() noexcept = default;

auto Style::get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type>
{
    return m_target_owner_type;
}

auto Style::get_target_owner_type() const -> erhe::property::Owner_type
{
    return m_target_owner_type;
}

void collect_style_target_owner_types(std::vector<erhe::property::Owner_type>& out)
{
    out.clear();
    const erhe::property::Property_registry& registry = erhe::property::Property_registry::get();
    const std::size_t count = registry.get_owner_count();
    for (std::size_t id = 0; id < count; ++id) {
        const erhe::property::Owner_type owner_type = static_cast<erhe::property::Owner_type>(id);
        if (registry.has_own_value_properties(owner_type)) {
            out.push_back(owner_type);
        }
    }
    std::sort(
        out.begin(), out.end(),
        [&registry](const erhe::property::Owner_type lhs, const erhe::property::Owner_type rhs) {
            return registry.get_owner_name(lhs) < registry.get_owner_name(rhs);
        }
    );
}

auto make_unique_style_name(const Content_library_node& styles_folder, const std::string_view base_name) -> std::string
{
    std::set<std::string> used_names;
    styles_folder.for_each_const<Content_library_node>(
        [&used_names](const Content_library_node& node) -> bool {
            if (node.item) {
                used_names.insert(node.item->get_name());
            }
            return true;
        }
    );
    std::string final_name{base_name};
    for (std::size_t number = 2; used_names.contains(final_name); ++number) {
        final_name = std::string{base_name} + " (" + std::to_string(number) + ")";
    }
    return final_name;
}

}
