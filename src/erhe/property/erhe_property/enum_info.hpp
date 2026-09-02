#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace erhe::property {

struct Enum_entry
{
    std::string_view label;
    int32_t          value;
};

// Enumerator table of one C++ enumeration. One static const table per
// enumeration lives next to the enumeration's c_str() so labels have one
// source; the table is referenced by every enumeration property of that
// type.
class Enum_info
{
public:
    constexpr Enum_info(const std::string_view type_name, const std::span<const Enum_entry> entries)
        : m_type_name{type_name}
        , m_entries  {entries}
    {
    }

    [[nodiscard]] auto get_type_name() const -> std::string_view                { return m_type_name; }
    [[nodiscard]] auto get_entries  () const -> std::span<const Enum_entry>      { return m_entries; }
    [[nodiscard]] auto contains     (int32_t value) const -> bool;
    [[nodiscard]] auto label_for    (int32_t value) const -> std::string_view;  // "" when absent
    [[nodiscard]] auto value_for    (std::string_view label) const -> std::optional<int32_t>;
    [[nodiscard]] auto index_of     (int32_t value) const -> std::optional<std::size_t>;

private:
    std::string_view            m_type_name;
    std::span<const Enum_entry> m_entries;
};

} // namespace erhe::property
