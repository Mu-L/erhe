#pragma once

#include "erhe_property/enum_info.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_value.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace erhe::property {

class Property_registry;

// One registered property: immutable after registration except for
// metadata overrides added by other owner types (override_metadata /
// add_owner), which also happen during static initialization.
class Dependency_property
{
public:
    class Registration
    {
    public:
        std::string_view  name;
        Property_type     type;
        Owner_type        owner_type;        // id of the registering class (owner_type.hpp)
        Property_metadata metadata;
        Validate_callback validate  {};
        const Enum_info*  enum_info {nullptr}; // required when type == enumeration
        bool              read_only {false};
        bool              attached  {false};
    };

    [[nodiscard]] auto get_index      () const -> uint16_t         { return m_index; }
    [[nodiscard]] auto get_name       () const -> std::string_view { return m_name; }
    [[nodiscard]] auto get_type       () const -> Property_type    { return m_type; }
    [[nodiscard]] auto get_owner_type () const -> Owner_type       { return m_owner_type; }
    [[nodiscard]] auto is_read_only   () const -> bool             { return m_read_only; }
    [[nodiscard]] auto is_attached    () const -> bool             { return m_attached; }
    [[nodiscard]] auto get_enum_info  () const -> const Enum_info* { return m_enum_info; }

    // Metadata resolution for an object whose get_property_owner_type() is
    // object_type: the override registered for the nearest owner type on
    // the object's ancestor chain (object_type itself first), else the
    // default metadata. Override lists are short (usually empty), so each
    // level is a linear scan.
    [[nodiscard]] auto get_metadata        (Owner_type object_type) const -> const Property_metadata&;
    [[nodiscard]] auto get_default_metadata() const -> const Property_metadata& { return m_default_metadata; }
    [[nodiscard]] auto get_default_value   (Owner_type object_type) const -> const Property_value&;

    // Type check plus the validate callback. False means the write is dropped.
    [[nodiscard]] auto validate(const Property_value& value) const -> bool;

    // Registers metadata for another owner type (WPF OverrideMetadata). A
    // missing default_value keeps the default metadata's default.
    void override_metadata(Owner_type owner_type, Property_metadata metadata);

    // override_metadata plus lookup of this property under (owner_type, name)
    // (WPF AddOwner).
    void add_owner(Owner_type owner_type, Property_metadata metadata);

private:
    friend class Property_registry;
    Dependency_property(uint16_t index, Registration&& registration);

    class Override
    {
    public:
        Owner_type        owner_type;
        Property_metadata metadata;
    };

    uint16_t              m_index;
    std::string           m_name;
    Property_type         m_type;
    Owner_type            m_owner_type;
    bool                  m_read_only;
    bool                  m_attached;
    const Enum_info*      m_enum_info;
    Validate_callback     m_validate;
    Property_metadata     m_default_metadata;
    std::vector<Override> m_overrides;
};

class Property_registry
{
public:
    [[nodiscard]] static auto get() -> Property_registry&;

    // Registration happens from static initializers of the owning classes
    // and from single-threaded early startup (descriptor-driven
    // registrations whose tables are function-local statics, e.g. the
    // texture graph's register_texture_graph_properties), before any
    // thread other than main exists. (owner_type, name) must be unique.
    auto register_property(Dependency_property::Registration&& registration) -> const Dependency_property&;
    void add_owner        (const Dependency_property& property, Owner_type owner_type);

    // Exact (owner type, name) lookup: the registration made for that id.
    [[nodiscard]] auto find(Owner_type owner_type, std::string_view name) const -> const Dependency_property*;

    // What an object whose get_property_owner_type() is object_type means
    // by `name`: find() on object_type, then on each ancestor up to the
    // root; the nearest registration wins (WPF derived-DType shadowing).
    [[nodiscard]] auto find_for_object(Owner_type object_type, std::string_view name) const -> const Dependency_property*;
    [[nodiscard]] auto get      (uint16_t index) const -> const Dependency_property&;
    [[nodiscard]] auto get_count() const -> std::size_t;

    // Non-attached properties an object whose get_property_owner_type() is
    // object_type has: the root's registrations first, then each level down
    // to object_type's own, each level in registration order. A name
    // registered at several levels is visited once, at the nearest level;
    // a property owned by several levels (add_owner) likewise.
    void for_each_property_of_object(Owner_type object_type, const std::function<void(const Dependency_property&)>& callback) const;

    // Owner type id table (owner_type.hpp): entry 0 is the root.
    auto               allocate_owner_type(Owner_type parent, std::string_view name) -> Owner_type;
    [[nodiscard]] auto get_owner_parent   (Owner_type id) const -> Owner_type;
    [[nodiscard]] auto get_owner_name     (Owner_type id) const -> std::string_view;

private:
    Property_registry();

    // A deque keeps the names at stable addresses, so a string_view handed
    // out by get_owner_name stays valid across later allocations.
    class Owner_entry
    {
    public:
        Owner_type  parent;
        std::string name;
    };

    class Owner_name_key
    {
    public:
        Owner_type  owner_type;
        std::string name;
        [[nodiscard]] auto operator==(const Owner_name_key&) const -> bool = default;
    };
    class Owner_name_hash
    {
    public:
        [[nodiscard]] auto operator()(const Owner_name_key& key) const -> std::size_t
        {
            return std::hash<std::string>{}(key.name) ^ (std::hash<Owner_type>{}(key.owner_type) * 0x9e3779b97f4a7c15ull);
        }
    };

    // Ancestor chain of object_type, nearest first, ending with the root.
    [[nodiscard]] auto owner_chain(Owner_type object_type) const -> std::vector<Owner_type>;

    mutable std::mutex                                                 m_mutex;
    std::deque<Owner_entry>                                            m_owner_types;
    std::vector<std::unique_ptr<Dependency_property>>                  m_properties;
    std::unordered_map<Owner_name_key, uint16_t, Owner_name_hash>      m_by_owner_and_name;
    std::vector<std::vector<uint16_t>>                                 m_by_owner; // indexed by owner type id, registration order
};

// Typed handle to a registered property. Copyable, trivially cheap.
template <Property_storable T>
class Property
{
public:
    Property() = default;
    explicit Property(const Dependency_property* property) : m_property{property} {}

    [[nodiscard]] auto is_valid() const -> bool                       { return m_property != nullptr; }
    [[nodiscard]] auto get     () const -> const Dependency_property& { return *m_property; }
    [[nodiscard]] auto get_ptr () const -> const Dependency_property* { return m_property; }
    [[nodiscard]] operator const Dependency_property&() const         { return *m_property; }

    // Registration entry points. The enum_info overload is the only one
    // valid for enumeration types; the other only for non-enumeration types.
    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_property(
        std::string_view  name,
        Owner_type        owner_type,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property<T>
    {
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = property_type_of<T>(),
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .validate   = std::move(validate),
                }
            )
        };
    }

    template <Property_storable U = T>
        requires Property_enum_type<U>
    static auto register_property(
        std::string_view  name,
        Owner_type        owner_type,
        const Enum_info&  enum_info,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property<T>
    {
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = Property_type::enumeration,
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .validate   = std::move(validate),
                    .enum_info  = &enum_info,
                }
            )
        };
    }

    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_attached(
        std::string_view  name,
        Owner_type        owner_type,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property<T>
    {
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = property_type_of<T>(),
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .validate   = std::move(validate),
                    .attached   = true,
                }
            )
        };
    }

    // A computed property (D26): read-only, its effective value is
    // `compute(object)` on every read, no local / style / inherited layer,
    // not listed among local values. The owner calls
    // Dependency_object::invalidate_dependents where the inputs change.
    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_computed(
        std::string_view  name,
        Owner_type        owner_type,
        Compute_callback  compute,
        Property_metadata metadata = {}
    ) -> Property<T>
    {
        metadata.compute = std::move(compute);
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = property_type_of<T>(),
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .read_only  = true,
                }
            )
        };
    }

    template <Property_storable U = T>
        requires Property_enum_type<U>
    static auto register_computed(
        std::string_view  name,
        Owner_type        owner_type,
        const Enum_info&  enum_info,
        Compute_callback  compute,
        Property_metadata metadata = {}
    ) -> Property<T>
    {
        metadata.compute = std::move(compute);
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name       = name,
                    .type       = Property_type::enumeration,
                    .owner_type = owner_type,
                    .metadata   = std::move(metadata),
                    .enum_info  = &enum_info,
                    .read_only  = true,
                }
            )
        };
    }

private:
    const Dependency_property* m_property{nullptr};
};

// Write permission for a read-only property (WPF DependencyPropertyKey). The
// owner keeps the key private and hands out get_property() for reading.
template <Property_storable T>
class Property_key
{
public:
    Property_key() = default;

    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_read_only(
        std::string_view  name,
        Owner_type        owner_type,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property_key<T>
    {
        return Property_key<T>{
            Property<T>{
                &Property_registry::get().register_property(
                    Dependency_property::Registration{
                        .name       = name,
                        .type       = property_type_of<T>(),
                        .owner_type = owner_type,
                        .metadata   = std::move(metadata),
                        .validate   = std::move(validate),
                        .read_only  = true,
                    }
                )
            }
        };
    }

    template <Property_storable U = T>
        requires Property_enum_type<U>
    static auto register_read_only(
        std::string_view  name,
        Owner_type        owner_type,
        const Enum_info&  enum_info,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property_key<T>
    {
        return Property_key<T>{
            Property<T>{
                &Property_registry::get().register_property(
                    Dependency_property::Registration{
                        .name       = name,
                        .type       = Property_type::enumeration,
                        .owner_type = owner_type,
                        .metadata   = std::move(metadata),
                        .validate   = std::move(validate),
                        .enum_info  = &enum_info,
                        .read_only  = true,
                    }
                )
            }
        };
    }

    [[nodiscard]] auto get_property() const -> Property<T>               { return m_property; }
    [[nodiscard]] auto get         () const -> const Dependency_property& { return m_property.get(); }

private:
    explicit Property_key(Property<T> property) : m_property{property} {}
    Property<T> m_property;
};

} // namespace erhe::property
