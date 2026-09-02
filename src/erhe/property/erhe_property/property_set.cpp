#include "erhe_property/property_set.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"

#include <algorithm>

namespace erhe::property {

namespace {

auto lower_bound_for(std::vector<Property_set::Entry>& entries, const Dependency_property& property)
{
    return std::lower_bound(
        entries.begin(), entries.end(), property.get_index(),
        [](const Property_set::Entry& entry, const uint16_t index) { return entry.property->get_index() < index; }
    );
}

auto lower_bound_for(const std::vector<Property_set::Entry>& entries, const Dependency_property& property)
{
    return std::lower_bound(
        entries.begin(), entries.end(), property.get_index(),
        [](const Property_set::Entry& entry, const uint16_t index) { return entry.property->get_index() < index; }
    );
}

} // anonymous namespace

auto Property_set::read_local_values(const Dependency_object& object) -> Property_set
{
    Property_set result;
    object.for_each_local_value(
        [&result](const Dependency_property& property, const Property_value& value) {
            result.m_entries.push_back(Entry{.property = &property, .value = value}); // already in index order
        }
    );
    return result;
}

auto Property_set::diff(const Property_set& before, const Property_set& after) -> Property_set
{
    Property_set result;
    for (const Entry& entry : after.m_entries) {
        const std::optional<Property_value> previous = before.find(*entry.property);
        if (!previous.has_value() || !(previous.value() == entry.value)) {
            result.m_entries.push_back(entry);
        }
    }
    return result;
}

void Property_set::set(const Dependency_property& property, Property_value value)
{
    const auto i = lower_bound_for(m_entries, property);
    if ((i != m_entries.end()) && (i->property == &property)) {
        i->value = std::move(value);
        return;
    }
    m_entries.insert(i, Entry{.property = &property, .value = std::move(value)});
}

void Property_set::remove(const Dependency_property& property)
{
    const auto i = lower_bound_for(m_entries, property);
    if ((i != m_entries.end()) && (i->property == &property)) {
        m_entries.erase(i);
    }
}

void Property_set::clear()
{
    m_entries.clear();
}

auto Property_set::find(const Dependency_property& property) const -> std::optional<Property_value>
{
    const auto i = lower_bound_for(m_entries, property);
    if ((i != m_entries.end()) && (i->property == &property)) {
        return i->value;
    }
    return std::nullopt;
}

auto Property_set::contains(const Dependency_property& property) const -> bool
{
    return find(property).has_value();
}

void Property_set::apply(Dependency_object& object) const
{
    const Dependency_object::Change_batch batch{object};
    for (const Entry& entry : m_entries) {
        object.set_value(*entry.property, entry.value);
    }
}

} // namespace erhe::property
