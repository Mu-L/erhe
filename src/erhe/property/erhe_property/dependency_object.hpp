#pragma once

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_value.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace erhe::property {

class Dependency_object;

// Subscription to one property on one object. Move-only; unsubscribes on
// destruction or release(). Outlives the object safely: the object's
// destruction turns the token into a no-op.
class Observer_token
{
public:
    Observer_token() = default;
    Observer_token(const Observer_token&) = delete;
    Observer_token& operator=(const Observer_token&) = delete;
    Observer_token(Observer_token&& other) noexcept;
    Observer_token& operator=(Observer_token&& other) noexcept;
    ~Observer_token() noexcept;

    [[nodiscard]] auto is_active() const -> bool;
    void release();

private:
    friend class Dependency_object;
    class Observer_list;
    Observer_token(std::weak_ptr<Observer_list> list, uint64_t id);

    std::weak_ptr<Observer_list> m_list{};
    uint64_t                     m_id  {0};
};

using Observer_callback = std::function<void(Dependency_object&, const Property_changed_args&)>;

// Values of inherits-flagged properties over a subtree, captured before a
// tree change so the change notifications after it carry the right old
// values (see capture_inheritance_snapshot).
class Inheritance_snapshot
{
public:
    struct Entry
    {
        Dependency_object*         object;
        const Dependency_property* property;
        Property_value             value;
        Value_source               source;
    };
    std::vector<Entry> entries;
};

// WPF DependencyObject: a sparse store of per-property values with
// precedence coerced > local > inherited > default, validate / coerce /
// changed callbacks from the property metadata, a virtual changed hook,
// per-object observers and change batching.
class Dependency_object
{
public:
    Dependency_object();
    // Copies local values (and their coerced values); observers and pending
    // batches are not copied.
    Dependency_object(const Dependency_object& other);
    Dependency_object& operator=(const Dependency_object& other);
    virtual ~Dependency_object() noexcept;

    // Item_type bits used for metadata resolution (Item_base returns
    // get_type()).
    [[nodiscard]] virtual auto get_property_owner_type() const -> uint64_t { return 0; }

    // Tree for inherits-flagged properties (Hierarchy overrides both).
    [[nodiscard]] virtual auto get_inheritance_parent() const -> const Dependency_object* { return nullptr; }
    virtual void for_each_inheritance_child(const std::function<void(Dependency_object&)>& callback) { static_cast<void>(callback); }

    // Typed access
    template <Property_storable T>
    [[nodiscard]] auto get_value(const Property<T>& property) const -> T
    {
        return get_as<T>(get_value(property.get()));
    }

    template <Property_storable T>
    void set_value(const Property<T>& property, const T& value)
    {
        set_value_internal(property.get(), make_value<T>(value), false);
    }

    template <Property_storable T>
    void set_value(const Property_key<T>& key, const T& value)
    {
        set_value_internal(key.get(), make_value<T>(value), true);
    }

    template <Property_storable T>
    void clear_value(const Property<T>& property)
    {
        clear_value_internal(property.get(), false);
    }

    template <Property_storable T>
    void clear_value(const Property_key<T>& key)
    {
        clear_value_internal(key.get(), true);
    }

    template <Property_storable T>
    [[nodiscard]] auto read_local_value(const Property<T>& property) const -> std::optional<T>
    {
        const std::optional<Property_value> value = read_local_value(property.get());
        if (!value.has_value()) {
            return std::nullopt;
        }
        return get_as<T>(value.value());
    }

    // Untyped access (editor, undo, serialization, MCP). Writes to a
    // read-only property are rejected here; the typed key overloads are the
    // only write path for those.
    [[nodiscard]] auto get_value       (const Dependency_property& property) const -> Property_value;
    void               set_value       (const Dependency_property& property, const Property_value& value);
    void               clear_value     (const Dependency_property& property);
    [[nodiscard]] auto read_local_value(const Dependency_property& property) const -> std::optional<Property_value>;
    [[nodiscard]] auto has_local_value (const Dependency_property& property) const -> bool;
    [[nodiscard]] auto get_value_source(const Dependency_property& property) const -> Value_source;
    [[nodiscard]] auto is_coerced      (const Dependency_property& property) const -> bool;

    // Re-runs the coerce callback against the current local value and
    // notifies if the effective value changed (WPF CoerceValue). A property
    // without a local value on this object is coerced on every read and has
    // nothing to re-run.
    void coerce_value(const Dependency_property& property);

    void for_each_local_value(const std::function<void(const Dependency_property&, const Property_value&)>& callback) const;

    // Observers (R16). The overload without a property observes every
    // property of the object (D21).
    [[nodiscard]] auto add_observer(const Dependency_property& property, Observer_callback callback) -> Observer_token;
    [[nodiscard]] auto add_observer(Observer_callback callback) -> Observer_token;

    // Inheritance support for tree changes: capture on the subtree root
    // before the tree changes, apply after. apply notifies every object in
    // the snapshot whose effective value or source changed.
    [[nodiscard]] auto capture_inheritance_snapshot() -> Inheritance_snapshot;
    void               apply_inheritance_snapshot(const Inheritance_snapshot& snapshot);

    // While one is alive, changed notifications on the object are queued and
    // delivered once when the outermost batch ends, one per property with
    // the value before the batch and the value after.
    class Change_batch
    {
    public:
        explicit Change_batch(Dependency_object& object);
        Change_batch(const Change_batch&) = delete;
        Change_batch& operator=(const Change_batch&) = delete;
        ~Change_batch() noexcept;
    private:
        Dependency_object& m_object;
    };

protected:
    virtual void on_property_changed(const Property_changed_args& args) { static_cast<void>(args); }

private:
    struct Effective_value_entry
    {
        uint16_t                      index;
        Property_value                local;
        std::optional<Property_value> coerced;
    };

    struct Pending_change
    {
        uint16_t       index;
        Property_value old_value;
        Value_source   old_source;
        Property_value new_value;
        Value_source   new_source;
    };

    [[nodiscard]] auto find_entry          (uint16_t index) const -> const Effective_value_entry*;
    [[nodiscard]] auto find_entry          (uint16_t index) -> Effective_value_entry*;
    [[nodiscard]] auto find_or_create_entry(uint16_t index) -> Effective_value_entry&;
    void               remove_entry        (uint16_t index);

    [[nodiscard]] auto get_metadata       (const Dependency_property& property) const -> const Property_metadata&;
    [[nodiscard]] auto get_base_value     (const Dependency_property& property, Value_source& out_source) const -> Property_value;
    [[nodiscard]] auto get_effective_value(const Dependency_property& property, Value_source& out_source) const -> Property_value;
    [[nodiscard]] auto get_inherited_value(const Dependency_property& property) const -> std::optional<Property_value>;

    void set_value_internal  (const Dependency_property& property, const Property_value& value, bool allow_read_only);
    void clear_value_internal(const Dependency_property& property, bool allow_read_only);
    void store_coerced       (const Dependency_property& property, Effective_value_entry& entry);

    void notify(
        const Dependency_property& property,
        const Property_value&      old_value,
        Value_source               old_source,
        const Property_value&      new_value,
        Value_source               new_source
    );
    void deliver(const Property_changed_args& args);
    [[nodiscard]] auto add_observer_entry(uint16_t index, Observer_callback callback) -> Observer_token;
    void flush_batch();
    void propagate_to_descendants(
        const Dependency_property& property,
        const Property_value&      old_value,
        const Property_value&      new_value
    );
    void capture_inheritance_snapshot_recursive(Inheritance_snapshot& snapshot);

    std::vector<Effective_value_entry>              m_entries;     // sorted by index
    std::shared_ptr<Observer_token::Observer_list>  m_observers;   // allocated on first add_observer
    std::vector<Pending_change>                     m_pending;
    int                                             m_batch_depth{0};
};

} // namespace erhe::property
