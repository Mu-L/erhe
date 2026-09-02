#include "operations/property_set_operation.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"

#include <fmt/format.h>

namespace editor {

void apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
)
{
    item.apply_local_state(property, state);
    context.on_item_property_changed(item, property);
}

auto to_local_state(const std::optional<erhe::property::Property_value>& value) -> std::optional<erhe::property::Local_state>
{
    if (!value.has_value()) {
        return std::nullopt;
    }
    return erhe::property::Local_state{value.value()};
}

auto describe_local_state(const erhe::property::Dependency_property& property, const std::optional<erhe::property::Local_state>& state) -> std::string
{
    if (!state.has_value()) {
        return "<default>";
    }
    if (const erhe::property::Expression_text* text = std::get_if<erhe::property::Expression_text>(&state.value()); text != nullptr) {
        return "expression '" + text->text + "'";
    }
    return erhe::property::to_string(property, std::get<erhe::property::Property_value>(state.value()));
}

Property_set_operation::Property_set_operation(
    const std::shared_ptr<erhe::Item_base>&      item,
    const erhe::property::Dependency_property&   property,
    std::optional<erhe::property::Local_state>   before,
    std::optional<erhe::property::Local_state>   after
)
    : m_item    {item}
    , m_property{property}
    , m_before  {std::move(before)}
    , m_after   {std::move(after)}
{
    set_description(
        fmt::format(
            "Set {} '{}' {} = {}",
            item->get_type_name(),
            item->get_name(),
            property.get_name(),
            describe_local_state(property, m_after)
        )
    );
}

Property_set_operation::Property_set_operation(
    const std::shared_ptr<erhe::Item_base>&       item,
    const erhe::property::Dependency_property&    property,
    std::optional<erhe::property::Property_value> before,
    std::optional<erhe::property::Property_value> after
)
    : Property_set_operation{item, property, to_local_state(before), to_local_state(after)}
{
}

Property_set_operation::~Property_set_operation() noexcept = default;

void Property_set_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    apply(context, m_after);
}

void Property_set_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    apply(context, m_before);
}

void Property_set_operation::apply(App_context& context, const std::optional<erhe::property::Local_state>& state)
{
    if (!m_item) {
        return;
    }
    apply_item_property(context, *m_item, m_property, state);
}

void Property_set_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    if (m_item) {
        out_items.insert(m_item.get());
    }
}

//

Property_set_apply_operation::Property_set_apply_operation(
    const std::vector<std::shared_ptr<erhe::Item_base>>& items,
    erhe::property::Property_set                         values
)
    : m_values{std::move(values)}
{
    m_targets.reserve(items.size());
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (!item) {
            continue;
        }
        Target target{.item = item, .before = {}};
        target.before.reserve(m_values.size());
        for (const erhe::property::Property_set::Entry& entry : m_values.entries()) {
            target.before.push_back(item->read_local_value(*entry.property));
        }
        m_targets.push_back(std::move(target));
    }
    set_description(fmt::format("Set {} properties on {} items", m_values.size(), m_targets.size()));
}

Property_set_apply_operation::~Property_set_apply_operation() noexcept = default;

void Property_set_apply_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    for (const Target& target : m_targets) {
        const erhe::property::Dependency_object::Change_batch batch{*target.item};
        for (const erhe::property::Property_set::Entry& entry : m_values.entries()) {
            apply_item_property(context, *target.item, *entry.property, erhe::property::Local_state{entry.value});
        }
    }
}

void Property_set_apply_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    for (const Target& target : m_targets) {
        const erhe::property::Dependency_object::Change_batch batch{*target.item};
        const std::vector<erhe::property::Property_set::Entry>& entries = m_values.entries();
        for (std::size_t i = 0, end = entries.size(); i < end; ++i) {
            apply_item_property(context, *target.item, *entries[i].property, to_local_state(target.before[i]));
        }
    }
}

void Property_set_apply_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    for (const Target& target : m_targets) {
        out_items.insert(target.item.get());
    }
}

}
