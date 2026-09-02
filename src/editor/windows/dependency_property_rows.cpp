#include "windows/dependency_property_rows.hpp"
#include "windows/property_editor.hpp"

#include "app_context.hpp"
#include "operations/compound_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "tools/clipboard.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <glm/gtc/quaternion.hpp>
#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace editor {

using erhe::property::Dependency_property;
using erhe::property::Property_metadata;
using erhe::property::Property_type;
using erhe::property::Property_ui;
using erhe::property::Property_value;
using erhe::property::Value_source;

Dependency_property_rows::Dependency_property_rows(App_context& context)
    : m_context{context}
{
}

void Dependency_property_rows::add_rows(Property_editor& editor, const std::vector<std::shared_ptr<erhe::Item_base>>& items)
{
    m_items.clear();
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (item) {
            m_items.push_back(item);
        }
    }
    if (m_items.empty()) {
        return;
    }

    // Properties common to every selected item's type, in the first item's
    // registration order.
    const erhe::property::Property_registry& registry = erhe::property::Property_registry::get();
    std::vector<const Dependency_property*> properties;
    registry.for_each_property_of_type(
        m_items.front()->get_type(),
        [&properties](const Dependency_property& property) { properties.push_back(&property); }
    );
    for (std::size_t i = 1; i < m_items.size(); ++i) {
        const uint64_t type_bits = m_items[i]->get_type();
        std::erase_if(
            properties,
            [&registry, type_bits](const Dependency_property* property) {
                return registry.find_for_type(type_bits, property->get_name()) != property;
            }
        );
    }
    if (!m_context.developer_mode) {
        const uint64_t owner_type = m_items.front()->get_property_owner_type();
        std::erase_if(properties, [owner_type](const Dependency_property* property) { return property->get_metadata(owner_type).ui.developer_only; });
    }
    if (properties.empty()) {
        return;
    }

    // Ungrouped rows first, then each group in order of first appearance.
    const uint64_t owner_type = m_items.front()->get_property_owner_type();
    for (const Dependency_property* property : properties) {
        if (property->get_metadata(owner_type).ui.group.empty()) {
            row(editor, *property);
        }
    }
    std::vector<std::string_view> groups_done;
    for (const Dependency_property* property : properties) {
        const std::string_view group = property->get_metadata(owner_type).ui.group;
        if (group.empty() || (std::find(groups_done.begin(), groups_done.end(), group) != groups_done.end())) {
            continue;
        }
        groups_done.push_back(group);
        editor.push_group(std::string{group}, ImGuiTreeNodeFlags_DefaultOpen);
        for (const Dependency_property* grouped : properties) {
            if (grouped->get_metadata(owner_type).ui.group == group) {
                row(editor, *grouped);
            }
        }
        editor.pop_group();
    }
}

void Dependency_property_rows::row(Property_editor& editor, const Dependency_property& property)
{
    const erhe::Item_base&   first    = *m_items.front();
    const Property_metadata& metadata = property.get_metadata(first.get_property_owner_type());

    // Label: "*" marks a local value that differs from the default.
    const bool differs_from_default = first.has_local_value(property) && !(first.get_value(property) == metadata.default_value.value());
    std::string label = differs_from_default ? std::string{"* "} + std::string{property.get_name()} : std::string{property.get_name()};

    std::string tooltip{metadata.ui.tooltip};
    if (!tooltip.empty()) {
        tooltip += "\n";
    }
    tooltip += "Source: ";
    tooltip += erhe::property::c_str(first.get_value_source(property));
    if (first.is_coerced(property)) {
        tooltip += " (coerced)";
    }
    tooltip += "\nDefault: ";
    tooltip += erhe::property::to_string(property, metadata.default_value.value());
    if (property.get_type() == Property_type::quat) {
        tooltip += "\nx y z w: ";
        tooltip += erhe::property::to_string(property, first.get_value(property));
    }

    editor.add_entry(
        std::move(label),
        [this, &property, &metadata]() {
            Property_value value = m_items.front()->get_value(property);
            bool mixed = false;
            for (std::size_t i = 1; i < m_items.size(); ++i) {
                if (!(m_items[i]->get_value(property) == value)) {
                    mixed = true;
                    break;
                }
            }
            if (mixed) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.45f, 0.35f, 0.15f, 0.6f});
            }
            const bool read_only = property.is_read_only();
            ImGui::BeginDisabled(read_only);
            bool immediate = false;
            const bool changed = draw_widget(property, metadata, value, immediate);
            ImGui::EndDisabled();
            if (mixed) {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Mixed values across the selection");
                }
            }
            if (read_only) {
                return;
            }
            if (immediate) {
                if (changed) {
                    begin_edit(property);
                    queue_set(property, value);
                    m_edit_property = nullptr;
                }
            } else {
                if (ImGui::IsItemActivated()) {
                    begin_edit(property);
                }
                if (changed) {
                    if (m_edit_property != &property) {
                        begin_edit(property); // keyboard edits can change without a separate activation frame
                    }
                    for (const std::shared_ptr<erhe::Item_base>& item : m_items) {
                        item->set_value(property, value);
                    }
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    end_edit(property);
                } else if ((m_edit_property == &property) && ImGui::IsItemDeactivated()) {
                    m_edit_property = nullptr; // activated, never edited
                }
            }
            context_menu(property, metadata);
        },
        std::move(tooltip)
    );
}

auto Dependency_property_rows::draw_widget(
    const Dependency_property& property,
    const Property_metadata&   metadata,
    Property_value&            value,
    bool&                      immediate
) -> bool
{
    const Property_ui& ui = metadata.ui;
    const float speed = ui.step.value_or(0.01f);
    const float min   = ui.min.value_or(0.0f);
    const float max   = ui.max.value_or(0.0f);
    const bool  has_range = ui.min.has_value() && ui.max.has_value();
    const bool  slider    = has_range && (ui.presentation == Property_ui::Presentation::slider);
    const bool  degrees   = (ui.presentation == Property_ui::Presentation::angle_degrees);
    const bool  color     = (ui.presentation == Property_ui::Presentation::color);
    immediate = false;

    switch (property.get_type()) {
        case Property_type::boolean: {
            bool v = std::get<bool>(value);
            if (ImGui::Checkbox("##", &v)) {
                value = v;
                return true;
            }
            return false;
        }
        case Property_type::integer: {
            int v = std::get<int>(value);
            const bool changed = slider
                ? ImGui::SliderInt("##", &v, static_cast<int>(min), static_cast<int>(max))
                : ImGui::DragInt("##", &v, std::max(speed, 1.0f), has_range ? static_cast<int>(min) : 0, has_range ? static_cast<int>(max) : 0);
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::floating: {
            float v = std::get<float>(value);
            bool changed = false;
            if (degrees) {
                float d = glm::degrees(v);
                changed = ImGui::DragFloat("##", &d, ui.step.value_or(0.5f), has_range ? glm::degrees(min) : 0.0f, has_range ? glm::degrees(max) : 0.0f, "%.2f°");
                v = glm::radians(d);
            } else if (slider) {
                changed = ImGui::SliderFloat("##", &v, min, max);
            } else {
                changed = ImGui::DragFloat("##", &v, speed, has_range ? min : 0.0f, has_range ? max : 0.0f);
            }
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::vec2: {
            glm::vec2 v = std::get<glm::vec2>(value);
            const bool changed = ImGui::DragFloat2("##", &v.x, speed, has_range ? min : 0.0f, has_range ? max : 0.0f);
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::vec3: {
            glm::vec3 v = std::get<glm::vec3>(value);
            bool changed = false;
            if (color) {
                changed = ImGui::ColorEdit3("##", &v.x, ImGuiColorEditFlags_Float);
            } else if (degrees) {
                glm::vec3 d = glm::degrees(v);
                changed = ImGui::DragFloat3("##", &d.x, ui.step.value_or(0.5f), 0.0f, 0.0f, "%.2f°");
                v = glm::radians(d);
            } else {
                changed = ImGui::DragFloat3("##", &v.x, speed, has_range ? min : 0.0f, has_range ? max : 0.0f);
            }
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::vec4: {
            glm::vec4 v = std::get<glm::vec4>(value);
            const bool changed = color
                ? ImGui::ColorEdit4("##", &v.x, ImGuiColorEditFlags_Float)
                : ImGui::DragFloat4("##", &v.x, speed, has_range ? min : 0.0f, has_range ? max : 0.0f);
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::quat: {
            // Edited as Euler degrees; the session keeps its own Euler
            // vector so a drag does not re-derive (and jitter) from the
            // quaternion every frame.
            const glm::quat q = std::get<glm::quat>(value);
            if (m_edit_property != &property) {
                m_edit_euler_degrees = glm::degrees(glm::eulerAngles(q));
            }
            glm::vec3 d = m_edit_euler_degrees;
            const bool changed = ImGui::DragFloat3("##", &d.x, ui.step.value_or(0.5f), 0.0f, 0.0f, "%.2f°");
            if (changed) {
                m_edit_euler_degrees = d;
                value = glm::normalize(glm::quat{glm::radians(d)});
            }
            return changed;
        }
        case Property_type::string: {
            m_text_scratch = std::get<std::string>(value);
            const bool changed = ImGui::InputText("##", &m_text_scratch);
            if (changed) {
                value = m_text_scratch;
            }
            return changed;
        }
        case Property_type::enumeration: {
            immediate = true;
            const erhe::property::Enum_info* info = property.get_enum_info();
            const int32_t current = std::get<erhe::property::Enum_value>(value).value;
            const std::string_view current_label = (info != nullptr) ? info->label_for(current) : std::string_view{};
            bool changed = false;
            if (ImGui::BeginCombo("##", std::string{current_label}.c_str())) {
                if (info != nullptr) {
                    for (const erhe::property::Enum_entry& entry : info->get_entries()) {
                        const bool selected = (entry.value == current);
                        if (ImGui::Selectable(std::string{entry.label}.c_str(), selected) && !selected) {
                            value   = erhe::property::Enum_value{entry.value};
                            changed = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    }
    return false;
}

void Dependency_property_rows::begin_edit(const Dependency_property& property)
{
    m_edit_property = &property;
    m_edit_before.clear();
    for (const std::shared_ptr<erhe::Item_base>& item : m_items) {
        m_edit_before.push_back(item->read_local_value(property));
    }
}

void Dependency_property_rows::end_edit(const Dependency_property& property)
{
    if (m_edit_property != &property) {
        return;
    }
    // Live edits already wrote the local value; the operation records the
    // session's before / after and re-applies after (idempotent) so the
    // consequence hook runs once per completed edit.
    if (m_items.size() == 1) {
        queue_set(property, m_items.front()->read_local_value(property));
    } else {
        Compound_operation::Parameters parameters;
        for (std::size_t i = 0; i < m_items.size(); ++i) {
            parameters.operations.push_back(
                std::make_shared<Property_set_operation>(m_items[i], property, m_edit_before[i], m_items[i]->read_local_value(property))
            );
        }
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
    }
    m_edit_property = nullptr;
}

// Queues after = `after` for every item, with the before values captured by
// begin_edit().
void Dependency_property_rows::queue_set(const Dependency_property& property, const std::optional<Property_value>& after)
{
    if ((m_context.operation_stack == nullptr) || (m_edit_before.size() != m_items.size())) {
        return;
    }
    if (m_items.size() == 1) {
        m_context.operation_stack->queue(std::make_shared<Property_set_operation>(m_items.front(), property, m_edit_before.front(), after));
        return;
    }
    Compound_operation::Parameters parameters;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        parameters.operations.push_back(std::make_shared<Property_set_operation>(m_items[i], property, m_edit_before[i], after));
    }
    m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
}

void Dependency_property_rows::context_menu(const Dependency_property& property, const Property_metadata& metadata)
{
    static_cast<void>(metadata);
    if (!ImGui::BeginPopupContextItem("property_row_context")) {
        return;
    }
    bool any_local = false;
    for (const std::shared_ptr<erhe::Item_base>& item : m_items) {
        any_local = any_local || item->has_local_value(property);
    }
    if (ImGui::MenuItem("Reset to default", nullptr, false, any_local && !property.is_read_only())) {
        reset_to_default(property);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy Properties", nullptr, false, (m_items.size() == 1) && (m_context.clipboard != nullptr))) {
        copy_properties();
    }
    const bool can_paste = (m_context.clipboard != nullptr) && m_context.clipboard->has_property_contents();
    if (ImGui::MenuItem("Paste Properties", nullptr, false, can_paste)) {
        paste_properties();
    }
    ImGui::EndPopup();
}

void Dependency_property_rows::reset_to_default(const Dependency_property& property)
{
    begin_edit(property);
    queue_set(property, std::nullopt);
    m_edit_property = nullptr;
}

void Dependency_property_rows::copy_properties()
{
    if ((m_context.clipboard == nullptr) || (m_items.size() != 1)) {
        return;
    }
    m_context.clipboard->set_property_contents(erhe::property::Property_set::read_local_values(*m_items.front()));
}

void Dependency_property_rows::paste_properties()
{
    if ((m_context.clipboard == nullptr) || (m_context.operation_stack == nullptr)) {
        return;
    }
    const erhe::property::Property_set& source = m_context.clipboard->get_property_contents();
    const erhe::property::Property_registry& registry = erhe::property::Property_registry::get();
    Compound_operation::Parameters parameters;
    for (const std::shared_ptr<erhe::Item_base>& item : m_items) {
        // Only the properties this item's type has (by identity, not name).
        erhe::property::Property_set filtered;
        for (const erhe::property::Property_set::Entry& entry : source.entries()) {
            if (entry.property->is_read_only()) {
                continue;
            }
            if (registry.find_for_type(item->get_type(), entry.property->get_name()) == entry.property) {
                filtered.set(*entry.property, entry.value);
            }
        }
        if (!filtered.empty()) {
            parameters.operations.push_back(
                std::make_shared<Property_set_apply_operation>(std::vector<std::shared_ptr<erhe::Item_base>>{item}, std::move(filtered))
            );
        }
    }
    if (parameters.operations.empty()) {
        return;
    }
    if (parameters.operations.size() == 1) {
        m_context.operation_stack->queue(parameters.operations.front());
    } else {
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
    }
}

}
