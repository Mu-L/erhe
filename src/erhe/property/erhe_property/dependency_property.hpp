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
        uint64_t          owner_type;        // Item_type bits of the registering class, 0 for none
        Property_metadata metadata;
        Validate_callback validate  {};
        const Enum_info*  enum_info {nullptr}; // required when type == enumeration
        bool              read_only {false};
        bool              attached  {false};
        // Second key dimension for classes that share owner type bits (all
        // graph node kinds are Item_type::graph_node): properties registered
        // with a non-zero subtype are listed and found only for objects whose
        // get_property_owner_subtype() matches (WPF keys per class through
        // DType; the subtype is that per-class identity). 0 = keyed by the
        // owner type bits alone. Subtype ids come from
        // allocate_property_owner_subtype() and are stable within a run only;
        // properties serialize by name.
        uint32_t          owner_subtype {0};
    };

    [[nodiscard]] auto get_index      () const -> uint16_t         { return m_index; }
    [[nodiscard]] auto get_name       () const -> std::string_view { return m_name; }
    [[nodiscard]] auto get_type       () const -> Property_type    { return m_type; }
    [[nodiscard]] auto get_owner_type () const -> uint64_t         { return m_owner_type; }
    [[nodiscard]] auto get_owner_subtype() const -> uint32_t       { return m_owner_subtype; }
    [[nodiscard]] auto is_read_only   () const -> bool             { return m_read_only; }
    [[nodiscard]] auto is_attached    () const -> bool             { return m_attached; }
    [[nodiscard]] auto get_enum_info  () const -> const Enum_info* { return m_enum_info; }

    // Metadata resolution for an object whose get_property_owner_type() is
    // owner_type_bits: an override whose owner type mask equals the bits
    // wins, then the last-registered override sharing any bit, then the
    // default metadata. Override lists are short (usually empty), so this is
    // a linear scan.
    [[nodiscard]] auto get_metadata        (uint64_t owner_type_bits) const -> const Property_metadata&;
    [[nodiscard]] auto get_default_metadata() const -> const Property_metadata& { return m_default_metadata; }
    [[nodiscard]] auto get_default_value   (uint64_t owner_type_bits) const -> const Property_value&;

    // Type check plus the validate callback. False means the write is dropped.
    [[nodiscard]] auto validate(const Property_value& value) const -> bool;

    // Registers metadata for another owner type (WPF OverrideMetadata). A
    // missing default_value keeps the default metadata's default.
    void override_metadata(uint64_t owner_type, Property_metadata metadata);

    // override_metadata plus lookup of this property under (owner_type, name)
    // (WPF AddOwner).
    void add_owner(uint64_t owner_type, Property_metadata metadata);

private:
    friend class Property_registry;
    Dependency_property(uint16_t index, Registration&& registration);

    struct Override
    {
        uint64_t          owner_type;
        Property_metadata metadata;
    };

    uint16_t              m_index;
    std::string           m_name;
    Property_type         m_type;
    uint64_t              m_owner_type;
    uint32_t              m_owner_subtype;
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
    // thread other than main exists; reads after that are lock-free.
    // (owner_type, owner_subtype, name) must be unique.
    auto register_property(Dependency_property::Registration&& registration) -> const Dependency_property&;
    void add_owner        (const Dependency_property& property, uint64_t owner_type);

    [[nodiscard]] auto find     (uint64_t owner_type, uint32_t owner_subtype, std::string_view name) const -> const Dependency_property*;
    [[nodiscard]] auto find     (uint64_t owner_type, std::string_view name) const -> const Dependency_property* { return find(owner_type, 0, name); }
    // The property named `name` among for_each_property_of_type(type_bits,
    // owner_subtype): what an object of that type and subtype means by the
    // name. On a name tie an exact-subtype registration wins over a
    // subtype-0 one (WPF derived-DType shadowing).
    [[nodiscard]] auto find_for_type(uint64_t type_bits, uint32_t owner_subtype, std::string_view name) const -> const Dependency_property*;
    [[nodiscard]] auto find_for_type(uint64_t type_bits, std::string_view name) const -> const Dependency_property* { return find_for_type(type_bits, 0, name); }
    [[nodiscard]] auto get      (uint16_t index) const -> const Dependency_property&;
    [[nodiscard]] auto get_count() const -> std::size_t;

    // Non-attached properties whose owner type shares a bit with type_bits
    // (or whose owner type is 0), and whose owner subtype is 0 or equals
    // owner_subtype, in registration order.
    void for_each_property_of_type(uint64_t type_bits, uint32_t owner_subtype, const std::function<void(const Dependency_property&)>& callback) const;
    void for_each_property_of_type(uint64_t type_bits, const std::function<void(const Dependency_property&)>& callback) const
    {
        for_each_property_of_type(type_bits, 0, callback);
    }

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

    struct Owner_name_key
    {
        uint64_t    owner_type;
        uint32_t    owner_subtype;
        std::string name;
        [[nodiscard]] auto operator==(const Owner_name_key&) const -> bool = default;
    };
    struct Owner_name_hash
    {
        [[nodiscard]] auto operator()(const Owner_name_key& key) const -> std::size_t
        {
            return std::hash<std::string>{}(key.name)
                ^ (std::hash<uint64_t>{}(key.owner_type) * 0x9e3779b97f4a7c15ull)
                ^ (std::hash<uint32_t>{}(key.owner_subtype) * 0xff51afd7ed558ccdull);
        }
    };

    mutable std::mutex                                                 m_mutex;
    std::deque<Owner_entry>                                            m_owner_types;
    std::vector<std::unique_ptr<Dependency_property>>                  m_properties;
    std::unordered_map<Owner_name_key, uint16_t, Owner_name_hash>      m_by_owner_and_name;
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
        uint64_t          owner_type,
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
        uint64_t          owner_type,
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

    // Subtype-keyed registrations (Registration::owner_subtype): the owning
    // class allocates its subtype with allocate_property_owner_subtype()
    // and returns it from get_property_owner_subtype().
    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_property(
        std::string_view  name,
        uint64_t          owner_type,
        uint32_t          owner_subtype,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property<T>
    {
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name          = name,
                    .type          = property_type_of<T>(),
                    .owner_type    = owner_type,
                    .metadata      = std::move(metadata),
                    .validate      = std::move(validate),
                    .owner_subtype = owner_subtype,
                }
            )
        };
    }

    template <Property_storable U = T>
        requires Property_enum_type<U>
    static auto register_property(
        std::string_view  name,
        uint64_t          owner_type,
        uint32_t          owner_subtype,
        const Enum_info&  enum_info,
        Property_metadata metadata = {},
        Validate_callback validate = {}
    ) -> Property<T>
    {
        return Property<T>{
            &Property_registry::get().register_property(
                Dependency_property::Registration{
                    .name          = name,
                    .type          = Property_type::enumeration,
                    .owner_type    = owner_type,
                    .metadata      = std::move(metadata),
                    .validate      = std::move(validate),
                    .enum_info     = &enum_info,
                    .owner_subtype = owner_subtype,
                }
            )
        };
    }

    template <Property_storable U = T>
        requires (!Property_enum_type<U>)
    static auto register_attached(
        std::string_view  name,
        uint64_t          owner_type,
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
        uint64_t          owner_type,
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
        uint64_t          owner_type,
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
        uint64_t          owner_type,
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
        uint64_t          owner_type,
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

// A fresh owner-subtype id (Registration::owner_subtype), monotonic from 1.
// Run-local only: nothing persists subtype ids, properties serialize by
// name. Callers cache it in a function-local static so static-init order
// across translation units cannot bite.
[[nodiscard]] auto allocate_property_owner_subtype() -> uint32_t;

} // namespace erhe::property
