#include "erhe_property/dependency_object.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <limits>

namespace erhe::property {

// Observers

class Observer_token::Observer_list
{
public:
    // index == any_property matches every property (D21)
    static constexpr uint16_t any_property = std::numeric_limits<uint16_t>::max();

    struct Entry
    {
        uint16_t          index;
        uint64_t          id;
        Observer_callback callback;
    };
    std::vector<Entry> entries;
    uint64_t           next_id{1};

    void remove(const uint64_t id)
    {
        std::erase_if(entries, [id](const Entry& entry) { return entry.id == id; });
    }
};

Observer_token::Observer_token(std::weak_ptr<Observer_list> list, const uint64_t id)
    : m_list{std::move(list)}
    , m_id  {id}
{
}

Observer_token::Observer_token(Observer_token&& other) noexcept
    : m_list{std::move(other.m_list)}
    , m_id  {other.m_id}
{
    other.m_id = 0;
}

Observer_token& Observer_token::operator=(Observer_token&& other) noexcept
{
    if (this != &other) {
        release();
        m_list     = std::move(other.m_list);
        m_id       = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

Observer_token::~Observer_token() noexcept
{
    release();
}

auto Observer_token::is_active() const -> bool
{
    return (m_id != 0) && !m_list.expired();
}

void Observer_token::release()
{
    if (m_id != 0) {
        if (const std::shared_ptr<Observer_list> list = m_list.lock()) {
            list->remove(m_id);
        }
    }
    m_list.reset();
    m_id = 0;
}

// Dependency_object

Dependency_object::Dependency_object() = default;

Dependency_object::Dependency_object(const Dependency_object& other)
    : m_entries{other.m_entries}
{
}

Dependency_object& Dependency_object::operator=(const Dependency_object& other)
{
    if (this != &other) {
        m_entries = other.m_entries;
    }
    return *this;
}

Dependency_object::~Dependency_object() noexcept = default;

auto Dependency_object::find_entry(const uint16_t index) const -> const Effective_value_entry*
{
    const auto i = std::lower_bound(
        m_entries.begin(), m_entries.end(), index,
        [](const Effective_value_entry& entry, const uint16_t value) { return entry.index < value; }
    );
    if ((i == m_entries.end()) || (i->index != index)) {
        return nullptr;
    }
    return &*i;
}

auto Dependency_object::find_entry(const uint16_t index) -> Effective_value_entry*
{
    return const_cast<Effective_value_entry*>(static_cast<const Dependency_object*>(this)->find_entry(index));
}

auto Dependency_object::find_or_create_entry(const uint16_t index) -> Effective_value_entry&
{
    const auto i = std::lower_bound(
        m_entries.begin(), m_entries.end(), index,
        [](const Effective_value_entry& entry, const uint16_t value) { return entry.index < value; }
    );
    if ((i != m_entries.end()) && (i->index == index)) {
        return *i;
    }
    return *m_entries.insert(i, Effective_value_entry{.index = index, .local = {}, .coerced = {}});
}

void Dependency_object::remove_entry(const uint16_t index)
{
    std::erase_if(m_entries, [index](const Effective_value_entry& entry) { return entry.index == index; });
}

auto Dependency_object::get_metadata(const Dependency_property& property) const -> const Property_metadata&
{
    return property.get_metadata(get_property_owner_type());
}

auto Dependency_object::get_inherited_value(const Dependency_property& property) const -> std::optional<Property_value>
{
    for (const Dependency_object* ancestor = get_inheritance_parent(); ancestor != nullptr; ancestor = ancestor->get_inheritance_parent()) {
        if (ancestor->has_local_value(property)) {
            Value_source source{};
            return ancestor->get_effective_value(property, source);
        }
    }
    return std::nullopt;
}

auto Dependency_object::get_base_value(const Dependency_property& property, Value_source& out_source) const -> Property_value
{
    if (const Effective_value_entry* entry = find_entry(property.get_index()); entry != nullptr) {
        out_source = Value_source::local;
        return entry->local;
    }
    const Property_metadata& metadata = get_metadata(property);
    if (metadata.bridge.is_bound()) {
        out_source = Value_source::local;
        return metadata.bridge.get(*this);
    }
    if (metadata.inherits) {
        if (std::optional<Property_value> inherited = get_inherited_value(property); inherited.has_value()) {
            out_source = Value_source::inherited;
            return std::move(inherited.value());
        }
    }
    out_source = Value_source::default_value;
    return metadata.default_value.value();
}

auto Dependency_object::get_effective_value(const Dependency_property& property, Value_source& out_source) const -> Property_value
{
    if (const Effective_value_entry* entry = find_entry(property.get_index()); entry != nullptr) {
        out_source = Value_source::local;
        return entry->coerced.has_value() ? entry->coerced.value() : entry->local;
    }
    Property_value base = get_base_value(property, out_source);
    const Property_metadata& metadata = get_metadata(property);
    if (metadata.coerce) {
        return metadata.coerce(*this, base);
    }
    return base;
}

auto Dependency_object::get_value(const Dependency_property& property) const -> Property_value
{
    Value_source source{};
    return get_effective_value(property, source);
}

auto Dependency_object::get_value_source(const Dependency_property& property) const -> Value_source
{
    Value_source source{};
    static_cast<void>(get_base_value(property, source));
    return source;
}

auto Dependency_object::is_coerced(const Dependency_property& property) const -> bool
{
    const Effective_value_entry* entry = find_entry(property.get_index());
    return (entry != nullptr) && entry->coerced.has_value();
}

auto Dependency_object::read_local_value(const Dependency_property& property) const -> std::optional<Property_value>
{
    const Effective_value_entry* entry = find_entry(property.get_index());
    if (entry != nullptr) {
        return entry->local;
    }
    const Property_metadata& metadata = get_metadata(property);
    if (metadata.bridge.is_bound()) {
        return metadata.bridge.get(*this);
    }
    return std::nullopt;
}

auto Dependency_object::has_local_value(const Dependency_property& property) const -> bool
{
    return (find_entry(property.get_index()) != nullptr) || get_metadata(property).bridge.is_bound();
}

void Dependency_object::store_coerced(const Dependency_property& property, Effective_value_entry& entry)
{
    const Property_metadata& metadata = get_metadata(property);
    entry.coerced.reset();
    if (metadata.coerce) {
        Property_value coerced = metadata.coerce(*this, entry.local);
        if (!(coerced == entry.local)) {
            ERHE_VERIFY(type_of(coerced) == property.get_type());
            entry.coerced = std::move(coerced);
        }
    }
}

void Dependency_object::set_value(const Dependency_property& property, const Property_value& value)
{
    set_value_internal(property, value, false);
}

void Dependency_object::set_value_internal(const Dependency_property& property, const Property_value& value, const bool allow_read_only)
{
    if (property.is_read_only() && !allow_read_only) {
        log->error("property '{}' is read-only", property.get_name());
        return;
    }
    if (!property.validate(value)) {
        return;
    }

    Value_source   old_source{};
    Property_value old_value = get_effective_value(property, old_source);

    const Property_metadata& metadata = get_metadata(property);
    if (metadata.bridge.is_bound()) {
        metadata.bridge.set(*this, value);
    } else {
        Effective_value_entry& entry = find_or_create_entry(property.get_index());
        entry.local = value;
        store_coerced(property, entry);
    }

    Value_source   new_source{};
    Property_value new_value = get_effective_value(property, new_source);
    notify(property, old_value, old_source, new_value, new_source);
}

void Dependency_object::clear_value(const Dependency_property& property)
{
    clear_value_internal(property, false);
}

void Dependency_object::clear_value_internal(const Dependency_property& property, const bool allow_read_only)
{
    if (property.is_read_only() && !allow_read_only) {
        log->error("property '{}' is read-only", property.get_name());
        return;
    }
    const Property_metadata& metadata = get_metadata(property);
    if (metadata.bridge.is_bound()) {
        // A bridged property has no "unset" state: clearing writes the default.
        set_value_internal(property, metadata.default_value.value(), allow_read_only);
        return;
    }
    if (find_entry(property.get_index()) == nullptr) {
        return;
    }

    Value_source   old_source{};
    Property_value old_value = get_effective_value(property, old_source);

    remove_entry(property.get_index());

    Value_source   new_source{};
    Property_value new_value = get_effective_value(property, new_source);
    notify(property, old_value, old_source, new_value, new_source);
}

void Dependency_object::coerce_value(const Dependency_property& property)
{
    Effective_value_entry* entry = find_entry(property.get_index());
    if (entry == nullptr) {
        return;
    }
    Value_source   old_source{};
    Property_value old_value = get_effective_value(property, old_source);
    store_coerced(property, *entry);
    Value_source   new_source{};
    Property_value new_value = get_effective_value(property, new_source);
    notify(property, old_value, old_source, new_value, new_source);
}

void Dependency_object::for_each_local_value(const std::function<void(const Dependency_property&, const Property_value&)>& callback) const
{
    const Property_registry& registry = Property_registry::get();
    // Entries and bridged properties, merged in index order (both are sorted).
    std::vector<const Dependency_property*> bridged;
    const uint64_t owner_type = get_property_owner_type();
    registry.for_each_property_of_type(
        owner_type,
        [&bridged, owner_type](const Dependency_property& property) {
            if (property.get_metadata(owner_type).bridge.is_bound()) {
                bridged.push_back(&property);
            }
        }
    );
    std::size_t b = 0;
    for (const Effective_value_entry& entry : m_entries) {
        while ((b < bridged.size()) && (bridged[b]->get_index() < entry.index)) {
            callback(*bridged[b], bridged[b]->get_metadata(owner_type).bridge.get(*this));
            ++b;
        }
        callback(registry.get(entry.index), entry.local);
    }
    for (; b < bridged.size(); ++b) {
        callback(*bridged[b], bridged[b]->get_metadata(owner_type).bridge.get(*this));
    }
}

auto Dependency_object::add_observer(const Dependency_property& property, Observer_callback callback) -> Observer_token
{
    return add_observer_entry(property.get_index(), std::move(callback));
}

auto Dependency_object::add_observer(Observer_callback callback) -> Observer_token
{
    return add_observer_entry(Observer_token::Observer_list::any_property, std::move(callback));
}

auto Dependency_object::add_observer_entry(const uint16_t index, Observer_callback callback) -> Observer_token
{
    if (!m_observers) {
        m_observers = std::make_shared<Observer_token::Observer_list>();
    }
    const uint64_t id = m_observers->next_id++;
    m_observers->entries.push_back(
        Observer_token::Observer_list::Entry{
            .index    = index,
            .id       = id,
            .callback = std::move(callback)
        }
    );
    return Observer_token{m_observers, id};
}

// Notification

void Dependency_object::notify(
    const Dependency_property& property,
    const Property_value&      old_value,
    const Value_source         old_source,
    const Property_value&      new_value,
    const Value_source         new_source
)
{
    const bool inherits = get_metadata(property).inherits;
    if (m_batch_depth > 0) {
        const uint16_t index = property.get_index();
        const auto i = std::find_if(m_pending.begin(), m_pending.end(), [index](const Pending_change& pending) { return pending.index == index; });
        if (i != m_pending.end()) {
            i->new_value  = new_value;
            i->new_source = new_source;
        } else {
            m_pending.push_back(
                Pending_change{
                    .index      = index,
                    .old_value  = old_value,
                    .old_source = old_source,
                    .new_value  = new_value,
                    .new_source = new_source
                }
            );
        }
        // Descendants are outside this object's batch; they learn about the
        // change immediately so their own observers see it.
        if (inherits && !(old_value == new_value)) {
            propagate_to_descendants(property, old_value, new_value);
        }
        return;
    }

    if ((old_value == new_value) && (old_source == new_source)) {
        return;
    }
    deliver(
        Property_changed_args{
            .property   = property,
            .old_value  = old_value,
            .new_value  = new_value,
            .old_source = old_source,
            .new_source = new_source
        }
    );
    if (inherits && !(old_value == new_value)) {
        propagate_to_descendants(property, old_value, new_value);
    }
}

void Dependency_object::deliver(const Property_changed_args& args)
{
    const Property_metadata& metadata = get_metadata(args.property);
    if (metadata.property_changed) {
        metadata.property_changed(*this, args);
    }
    on_property_changed(args);
    if (m_observers) {
        // Observers may unsubscribe (or subscribe) from inside the callback,
        // so iterate over a copy of the matching callbacks.
        const uint16_t index = args.property.get_index();
        std::vector<Observer_callback> callbacks;
        for (const Observer_token::Observer_list::Entry& entry : m_observers->entries) {
            if ((entry.index == index) || (entry.index == Observer_token::Observer_list::any_property)) {
                callbacks.push_back(entry.callback);
            }
        }
        for (const Observer_callback& callback : callbacks) {
            callback(*this, args);
        }
    }
}

void Dependency_object::propagate_to_descendants(
    const Dependency_property& property,
    const Property_value&      old_value,
    const Property_value&      new_value
)
{
    const uint16_t index = property.get_index();
    for_each_inheritance_child(
        [&](Dependency_object& child) {
            if (child.has_local_value(property)) {
                return; // a local value shadows the subtree
            }
            const Property_metadata& child_metadata = child.get_metadata(property);
            Property_value child_old = old_value;
            Property_value child_new = new_value;
            if (child_metadata.coerce) {
                child_old = child_metadata.coerce(child, child_old);
                child_new = child_metadata.coerce(child, child_new);
            }
            child.notify(property, child_old, Value_source::inherited, child_new, Value_source::inherited);
        }
    );
}

Dependency_object::Change_batch::Change_batch(Dependency_object& object)
    : m_object{object}
{
    ++m_object.m_batch_depth;
}

Dependency_object::Change_batch::~Change_batch() noexcept
{
    --m_object.m_batch_depth;
    if (m_object.m_batch_depth == 0) {
        m_object.flush_batch();
    }
}

void Dependency_object::flush_batch()
{
    const Property_registry& registry = Property_registry::get();
    // A delivered callback may set values, which queue nothing now that the
    // depth is zero but may recurse into flush; take the pending list first.
    while (!m_pending.empty()) {
        std::vector<Pending_change> pending = std::move(m_pending);
        m_pending.clear();
        for (const Pending_change& change : pending) {
            if ((change.old_value == change.new_value) && (change.old_source == change.new_source)) {
                continue;
            }
            deliver(
                Property_changed_args{
                    .property   = registry.get(change.index),
                    .old_value  = change.old_value,
                    .new_value  = change.new_value,
                    .old_source = change.old_source,
                    .new_source = change.new_source
                }
            );
        }
    }
}

// Inheritance snapshots

void Dependency_object::capture_inheritance_snapshot_recursive(Inheritance_snapshot& snapshot)
{
    const Property_registry& registry = Property_registry::get();
    const uint64_t owner_type = get_property_owner_type();
    const std::size_t count = registry.get_count();
    for (uint16_t index = 0; index < count; ++index) {
        const Dependency_property& property = registry.get(index);
        if (!property.get_metadata(owner_type).inherits) {
            continue;
        }
        if (has_local_value(property)) {
            continue; // local value: unaffected by the tree
        }
        Value_source source{};
        Property_value value = get_effective_value(property, source);
        snapshot.entries.push_back(
            Inheritance_snapshot::Entry{
                .object   = this,
                .property = &property,
                .value    = std::move(value),
                .source   = source
            }
        );
    }
    for_each_inheritance_child([&snapshot](Dependency_object& child) { child.capture_inheritance_snapshot_recursive(snapshot); });
}

auto Dependency_object::capture_inheritance_snapshot() -> Inheritance_snapshot
{
    Inheritance_snapshot snapshot;
    capture_inheritance_snapshot_recursive(snapshot);
    return snapshot;
}

void Dependency_object::apply_inheritance_snapshot(const Inheritance_snapshot& snapshot)
{
    for (const Inheritance_snapshot::Entry& entry : snapshot.entries) {
        Dependency_object& object = *entry.object;
        Value_source   new_source{};
        Property_value new_value = object.get_effective_value(*entry.property, new_source);
        if ((new_value == entry.value) && (new_source == entry.source)) {
            continue;
        }
        // Not through notify(): descendants have their own snapshot entries.
        object.deliver(
            Property_changed_args{
                .property   = *entry.property,
                .old_value  = entry.value,
                .new_value  = new_value,
                .old_source = entry.source,
                .new_source = new_source
            }
        );
    }
}

} // namespace erhe::property
