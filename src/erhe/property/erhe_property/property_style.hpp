#pragma once

#include "erhe_property/property_set.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace erhe::property {

// A named bag of values applied to an object as the style layer (D25 in
// doc/property-system.md; WPF Style setters): coerced > local > style
// > inherited > default. Immutable after construction and shared between
// objects through std::shared_ptr<const Property_style>; a changed preset
// is a new style re-applied to its users.
class Property_style
{
public:
    Property_style(std::string name, Property_set values)
        : m_name  {std::move(name)}
        , m_values{std::move(values)}
    {
    }

    [[nodiscard]] auto get_name  () const -> std::string_view    { return m_name; }
    [[nodiscard]] auto get_values() const -> const Property_set& { return m_values; }

private:
    std::string  m_name;
    Property_set m_values;
};

} // namespace erhe::property
