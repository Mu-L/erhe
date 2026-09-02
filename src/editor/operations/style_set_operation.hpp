#pragma once

#include "operations/operation.hpp"

#include "erhe_property/property_style.hpp"

#include <memory>

namespace erhe { class Item_base; }

namespace editor {

// Undoable swap of one item's style (doc/property-system-plan.md D25):
// `after` replaces the item's style on execute, `before` on undo; nullptr
// is "no style". Local values are untouched by either direction.
class Style_set_operation : public Operation
{
public:
    Style_set_operation(
        const std::shared_ptr<erhe::Item_base>&               item,
        std::shared_ptr<const erhe::property::Property_style> before,
        std::shared_ptr<const erhe::property::Property_style> after
    );
    ~Style_set_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    void apply(App_context& context, const std::shared_ptr<const erhe::property::Property_style>& style);

    std::shared_ptr<erhe::Item_base>                      m_item;
    std::shared_ptr<const erhe::property::Property_style> m_before;
    std::shared_ptr<const erhe::property::Property_style> m_after;
};

} // namespace editor
