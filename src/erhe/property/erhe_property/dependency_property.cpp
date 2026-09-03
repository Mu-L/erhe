#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <atomic>
#include <limits>

namespace erhe::property {

Dependency_property::Dependency_property(const uint16_t index, Registration&& registration)
    : m_index           {index}
    , m_name            {registration.name}
    , m_type            {registration.type}
    , m_owner_type      {registration.owner_type}
    , m_owner_subtype   {registration.owner_subtype}
    , m_read_only       {registration.read_only}
    , m_attached        {registration.attached}
    , m_enum_info       {registration.enum_info}
    , m_validate        {std::move(registration.validate)}
    , m_default_metadata{std::move(registration.metadata)}
{
    if (m_type == Property_type::enumeration) {
        ERHE_VERIFY(m_enum_info != nullptr);
        if (!m_default_metadata.default_value.has_value()) {
            const std::span<const Enum_entry> entries = m_enum_info->get_entries();
            m_default_metadata.default_value = Enum_value{entries.empty() ? 0 : entries.front().value};
        }
    } else if (!m_default_metadata.default_value.has_value()) {
        m_default_metadata.default_value = zero_value(m_type);
    }
    ERHE_VERIFY(type_of(m_default_metadata.default_value.value()) == m_type);
    ERHE_VERIFY(validate(m_default_metadata.default_value.value()));
}

auto Dependency_property::get_metadata(const uint64_t owner_type_bits) const -> const Property_metadata&
{
    const Property_metadata* any_bit_match = nullptr;
    for (const Override& entry : m_overrides) {
        if (entry.owner_type == owner_type_bits) {
            return entry.metadata;
        }
        if ((entry.owner_type & owner_type_bits) != 0) {
            any_bit_match = &entry.metadata;
        }
    }
    return (any_bit_match != nullptr) ? *any_bit_match : m_default_metadata;
}

auto Dependency_property::get_default_value(const uint64_t owner_type_bits) const -> const Property_value&
{
    return get_metadata(owner_type_bits).default_value.value();
}

auto Dependency_property::validate(const Property_value& value) const -> bool
{
    if (type_of(value) != m_type) {
        log->error(
            "property '{}': value type {} does not match property type {}",
            m_name, c_str(type_of(value)), c_str(m_type)
        );
        return false;
    }
    if (m_type == Property_type::enumeration) {
        const int32_t raw = std::get<Enum_value>(value).value;
        if (!m_enum_info->contains(raw)) {
            log->error("property '{}': {} is not a {} enumerator", m_name, raw, m_enum_info->get_type_name());
            return false;
        }
    }
    if (m_validate && !m_validate(value)) {
        log->error("property '{}': value rejected by validate callback", m_name);
        return false;
    }
    return true;
}

void Dependency_property::override_metadata(const uint64_t owner_type, Property_metadata metadata)
{
    if (!metadata.default_value.has_value()) {
        metadata.default_value = m_default_metadata.default_value;
    }
    ERHE_VERIFY(type_of(metadata.default_value.value()) == m_type);
    ERHE_VERIFY(validate(metadata.default_value.value()));
    m_overrides.push_back(Override{.owner_type = owner_type, .metadata = std::move(metadata)});
}

void Dependency_property::add_owner(const uint64_t owner_type, Property_metadata metadata)
{
    override_metadata(owner_type, std::move(metadata));
    Property_registry::get().add_owner(*this, owner_type);
}

//

Property_registry::Property_registry()
{
    m_owner_types.push_back(Owner_entry{.parent = root_owner_type, .name = "root"});
}

auto Property_registry::get() -> Property_registry&
{
    static Property_registry s_registry;
    return s_registry;
}

auto Property_registry::allocate_owner_type(const Owner_type parent, const std::string_view name) -> Owner_type
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(parent < m_owner_types.size());
    const Owner_type id = static_cast<Owner_type>(m_owner_types.size());
    m_owner_types.push_back(Owner_entry{.parent = parent, .name = std::string{name}});
    return id;
}

auto Property_registry::get_owner_parent(const Owner_type id) const -> Owner_type
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(id < m_owner_types.size());
    return m_owner_types[id].parent;
}

auto Property_registry::get_owner_name(const Owner_type id) const -> std::string_view
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(id < m_owner_types.size());
    return m_owner_types[id].name;
}

auto allocate_owner_type(const Owner_type parent, const std::string_view name) -> Owner_type
{
    return Property_registry::get().allocate_owner_type(parent, name);
}

auto get_owner_type_parent(const Owner_type id) -> Owner_type
{
    return Property_registry::get().get_owner_parent(id);
}

auto get_owner_type_name(const Owner_type id) -> std::string_view
{
    return Property_registry::get().get_owner_name(id);
}

auto is_owner_type_or_descendant(Owner_type id, const Owner_type ancestor) -> bool
{
    for (;;) {
        if (id == ancestor) {
            return true;
        }
        if (id == root_owner_type) {
            return false;
        }
        id = get_owner_type_parent(id);
    }
}

auto Property_registry::register_property(Dependency_property::Registration&& registration) -> const Dependency_property&
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(m_properties.size() < std::numeric_limits<uint16_t>::max());
    const Owner_name_key key{.owner_type = registration.owner_type, .owner_subtype = registration.owner_subtype, .name = std::string{registration.name}};
    if (m_by_owner_and_name.contains(key)) {
        ERHE_FATAL("property '%s' is already registered for owner type 0x%llx subtype %u", key.name.c_str(), static_cast<unsigned long long>(key.owner_type), static_cast<unsigned int>(key.owner_subtype));
    }
    const uint16_t index = static_cast<uint16_t>(m_properties.size());
    m_properties.push_back(std::unique_ptr<Dependency_property>{new Dependency_property{index, std::move(registration)}});
    m_by_owner_and_name.emplace(key, index);
    return *m_properties.back();
}

void Property_registry::add_owner(const Dependency_property& property, const uint64_t owner_type)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    const Owner_name_key key{.owner_type = owner_type, .owner_subtype = property.get_owner_subtype(), .name = std::string{property.get_name()}};
    if (m_by_owner_and_name.contains(key)) {
        ERHE_FATAL("property '%s' is already registered for owner type 0x%llx subtype %u", key.name.c_str(), static_cast<unsigned long long>(owner_type), static_cast<unsigned int>(key.owner_subtype));
    }
    m_by_owner_and_name.emplace(key, property.get_index());
}

auto Property_registry::find(const uint64_t owner_type, const uint32_t owner_subtype, const std::string_view name) const -> const Dependency_property*
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = m_by_owner_and_name.find(Owner_name_key{.owner_type = owner_type, .owner_subtype = owner_subtype, .name = std::string{name}});
    if (i == m_by_owner_and_name.end()) {
        return nullptr;
    }
    return m_properties[i->second].get();
}

auto Property_registry::find_for_type(const uint64_t type_bits, const uint32_t owner_subtype, const std::string_view name) const -> const Dependency_property*
{
    const Dependency_property* subtype_match = nullptr;
    const Dependency_property* untyped_match = nullptr;
    for_each_property_of_type(
        type_bits,
        owner_subtype,
        [&subtype_match, &untyped_match, owner_subtype, name](const Dependency_property& property) {
            if (property.get_name() != name) {
                return;
            }
            if ((owner_subtype != 0) && (property.get_owner_subtype() == owner_subtype)) {
                if (subtype_match == nullptr) {
                    subtype_match = &property;
                }
            } else if (untyped_match == nullptr) {
                untyped_match = &property;
            }
        }
    );
    return (subtype_match != nullptr) ? subtype_match : untyped_match;
}

auto Property_registry::get(const uint16_t index) const -> const Dependency_property&
{
    ERHE_VERIFY(index < m_properties.size());
    return *m_properties[index];
}

auto Property_registry::get_count() const -> std::size_t
{
    return m_properties.size();
}

void Property_registry::for_each_property_of_type(
    const uint64_t                                             type_bits,
    const uint32_t                                             owner_subtype,
    const std::function<void(const Dependency_property&)>&     callback
) const
{
    for (const std::unique_ptr<Dependency_property>& property : m_properties) {
        if (property->is_attached()) {
            continue;
        }
        const uint32_t subtype = property->get_owner_subtype();
        if ((subtype != 0) && (subtype != owner_subtype)) {
            continue;
        }
        const uint64_t owner = property->get_owner_type();
        if ((owner == 0) || ((owner & type_bits) != 0)) {
            callback(*property);
            continue;
        }
        // add_owner() registrations of a base-type property for a derived type
        for (const Dependency_property::Override& entry : property->m_overrides) {
            if ((entry.owner_type & type_bits) != 0) {
                callback(*property);
                break;
            }
        }
    }
}

auto allocate_property_owner_subtype() -> uint32_t
{
    static std::atomic<uint32_t> s_next_owner_subtype{1};
    return s_next_owner_subtype.fetch_add(1);
}

} // namespace erhe::property
