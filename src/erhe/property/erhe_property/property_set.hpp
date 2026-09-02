#pragma once

#include "erhe_property/property_value.hpp"

#include <optional>
#include <vector>

namespace erhe::property {

class Dependency_object;
class Dependency_property;

// A bag of (property, value) pairs sorted by property index (R19): the
// local values of an object, a clipboard payload, a diff.
class Property_set
{
public:
    struct Entry
    {
        const Dependency_property* property;
        Property_value             value;
        [[nodiscard]] auto operator==(const Entry& other) const -> bool
        {
            return (property == other.property) && (value == other.value);
        }
    };

    [[nodiscard]] static auto read_local_values(const Dependency_object& object) -> Property_set;

    // Entries of `after` whose value differs from `before` or that `before`
    // does not have.
    [[nodiscard]] static auto diff(const Property_set& before, const Property_set& after) -> Property_set;

    void set   (const Dependency_property& property, Property_value value);
    void remove(const Dependency_property& property);
    void clear ();

    [[nodiscard]] auto find    (const Dependency_property& property) const -> std::optional<Property_value>;
    [[nodiscard]] auto contains(const Dependency_property& property) const -> bool;
    [[nodiscard]] auto empty   () const -> bool                      { return m_entries.empty(); }
    [[nodiscard]] auto size    () const -> std::size_t               { return m_entries.size(); }
    [[nodiscard]] auto entries () const -> const std::vector<Entry>& { return m_entries; }

    // Applies every entry to the object as a local value, in one change
    // batch. Entries whose property the object rejects (read-only, failed
    // validate) are skipped by set_value itself.
    void apply(Dependency_object& object) const;

    [[nodiscard]] auto operator==(const Property_set& other) const -> bool { return m_entries == other.m_entries; }

private:
    std::vector<Entry> m_entries;
};

} // namespace erhe::property
