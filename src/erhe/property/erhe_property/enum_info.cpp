#include "erhe_property/enum_info.hpp"

namespace erhe::property {

auto Enum_info::contains(const int32_t value) const -> bool
{
    return index_of(value).has_value();
}

auto Enum_info::label_for(const int32_t value) const -> std::string_view
{
    for (const Enum_entry& entry : m_entries) {
        if (entry.value == value) {
            return entry.label;
        }
    }
    return {};
}

auto Enum_info::value_for(const std::string_view label) const -> std::optional<int32_t>
{
    for (const Enum_entry& entry : m_entries) {
        if (entry.label == label) {
            return entry.value;
        }
    }
    return std::nullopt;
}

auto Enum_info::index_of(const int32_t value) const -> std::optional<std::size_t>
{
    for (std::size_t i = 0, end = m_entries.size(); i < end; ++i) {
        if (m_entries[i].value == value) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace erhe::property
