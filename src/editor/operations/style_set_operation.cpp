#include "operations/style_set_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"

#include <fmt/format.h>

namespace editor {

Style_set_operation::Style_set_operation(
    const std::shared_ptr<erhe::Item_base>&               item,
    std::shared_ptr<const erhe::property::Property_style> before,
    std::shared_ptr<const erhe::property::Property_style> after
)
    : m_item  {item}
    , m_before{std::move(before)}
    , m_after {std::move(after)}
{
    set_description(
        fmt::format(
            "Set {} '{}' style = {}",
            item->get_type_name(),
            item->get_name(),
            m_after ? fmt::format("'{}'", m_after->get_name()) : "none"
        )
    );
}

Style_set_operation::~Style_set_operation() noexcept = default;

void Style_set_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    apply(context, m_after);
}

void Style_set_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    apply(context, m_before);
}

void Style_set_operation::apply(App_context& context, const std::shared_ptr<const erhe::property::Property_style>& style)
{
    if (!m_item) {
        return;
    }
    // Consequences (D11) for every property either style names; the store
    // itself notified only the ones whose effective value changed.
    const std::shared_ptr<const erhe::property::Property_style> old_style = m_item->get_style();
    if (!m_item->set_style(style)) {
        log_operations->warn("style on '{}' not applied", m_item->get_name());
        return;
    }
    for (const std::shared_ptr<const erhe::property::Property_style>& touched : {old_style, style}) {
        if (!touched) {
            continue;
        }
        for (const erhe::property::Property_set::Entry& entry : touched->get_values().entries()) {
            context.on_item_property_changed(*m_item, *entry.property);
        }
    }
}

void Style_set_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    if (m_item) {
        out_items.insert(m_item.get());
    }
}

} // namespace editor
