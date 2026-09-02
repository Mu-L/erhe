#pragma once

#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace erhe::property::test {

// Owner type bits for test registrations; far from any Item_type bit.
constexpr uint64_t type_a = uint64_t{1} << 50;
constexpr uint64_t type_b = uint64_t{1} << 51;
constexpr uint64_t type_c = uint64_t{1} << 52;
constexpr uint64_t type_d = uint64_t{1} << 53; // bridged-property tests only

struct Recorded_change
{
    std::string    property_name;
    Property_value old_value;
    Property_value new_value;
    Value_source   old_source;
    Value_source   new_source;
};

// Minimal tree-capable Dependency_object: the inheritance virtuals plus a
// record of every on_property_changed() call.
class Test_object : public Dependency_object
{
public:
    explicit Test_object(const uint64_t type = type_a) : m_type{type} {}
    Test_object(const Test_object&) = default;
    Test_object& operator=(const Test_object&) = default;
    ~Test_object() noexcept override
    {
        for (Test_object* child : m_children) {
            child->m_parent = nullptr;
        }
        if (m_parent != nullptr) {
            std::erase(m_parent->m_children, this);
        }
    }

    auto get_property_owner_type() const -> uint64_t override                  { return m_type; }
    auto get_inheritance_parent () const -> const Dependency_object* override  { return m_parent; }
    void for_each_inheritance_child(const std::function<void(Dependency_object&)>& callback) override
    {
        for (Test_object* child : m_children) {
            callback(*child);
        }
    }

    // Reparent with the snapshot protocol Hierarchy::set_parent uses.
    void set_parent(Test_object* parent)
    {
        const Inheritance_snapshot snapshot = capture_inheritance_snapshot();
        if (m_parent != nullptr) {
            std::erase(m_parent->m_children, this);
        }
        m_parent = parent;
        if (m_parent != nullptr) {
            m_parent->m_children.push_back(this);
        }
        apply_inheritance_snapshot(snapshot);
    }

    std::vector<Recorded_change> changes;

    [[nodiscard]] auto change_count(const std::string_view name) const -> std::size_t
    {
        return static_cast<std::size_t>(std::count_if(changes.begin(), changes.end(), [name](const Recorded_change& c) { return c.property_name == name; }));
    }

protected:
    void on_property_changed(const Property_changed_args& args) override
    {
        changes.push_back(
            Recorded_change{
                .property_name = std::string{args.property.get_name()},
                .old_value     = args.old_value,
                .new_value     = args.new_value,
                .old_source    = args.old_source,
                .new_source    = args.new_source
            }
        );
    }

private:
    uint64_t                  m_type;
    Test_object*              m_parent{nullptr};
    std::vector<Test_object*> m_children;
};

} // namespace erhe::property::test
