#pragma once

#include "operations/operation.hpp"

#include "erhe_property/property_set.hpp"
#include "erhe_property/property_value.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace erhe          { class Item_base; }
namespace erhe::property { class Dependency_property; }

namespace editor {

class App_context;

// Undoable write of one property on one item (doc/property-system-plan.md
// D11). `before` / `after` are the item's LOCAL value, nullopt meaning "no
// local value" - so undo restores a cleared property, not merely the previous
// effective value. After each apply the operation runs
// App_context::on_item_property_changed so the property's consequence flags
// take effect.
class Property_set_operation : public Operation
{
public:
    Property_set_operation(
        const std::shared_ptr<erhe::Item_base>&       item,
        const erhe::property::Dependency_property&    property,
        std::optional<erhe::property::Property_value> before,
        std::optional<erhe::property::Property_value> after
    );
    ~Property_set_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

    [[nodiscard]] auto get_item    () const -> const std::shared_ptr<erhe::Item_base>&    { return m_item; }
    [[nodiscard]] auto get_property() const -> const erhe::property::Dependency_property& { return m_property; }

private:
    void apply(App_context& context, const std::optional<erhe::property::Property_value>& value);

    std::shared_ptr<erhe::Item_base>              m_item;
    const erhe::property::Dependency_property&    m_property;
    std::optional<erhe::property::Property_value> m_before;
    std::optional<erhe::property::Property_value> m_after;
};

// Applies a Property_set to several items (paste, multi-selection edit) as
// one undo step: every entry becomes a local value on every item, and undo
// restores each item's previous local value (or clears it).
class Property_set_apply_operation : public Operation
{
public:
    Property_set_apply_operation(
        const std::vector<std::shared_ptr<erhe::Item_base>>& items,
        erhe::property::Property_set                         values
    );
    ~Property_set_apply_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    struct Target
    {
        std::shared_ptr<erhe::Item_base>                           item;
        std::vector<std::optional<erhe::property::Property_value>> before; // one per m_values entry
    };

    std::vector<Target>          m_targets;
    erhe::property::Property_set m_values;
};

// Applies `value` (nullopt = clear) as the item's local value and runs the
// editor consequence hook. Shared by the operations above and by direct
// (non-undoable) callers such as the startup script.
void apply_item_property(
    App_context&                                         context,
    erhe::Item_base&                                     item,
    const erhe::property::Dependency_property&           property,
    const std::optional<erhe::property::Property_value>& value
);

}
