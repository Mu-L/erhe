#include "content_library/style.hpp"

#include "content_library/content_library.hpp"

#include <set>

namespace editor {

Style::Style(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

Style::~Style() noexcept = default;

auto Style::get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type>
{
    return erhe::property::root_owner_type;
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
