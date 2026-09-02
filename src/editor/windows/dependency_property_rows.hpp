#pragma once

#include "erhe_property/expression.hpp"
#include "erhe_property/property_value.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe           { class Item_base; }
namespace erhe::property { class Dependency_property; class Property_metadata; }

namespace editor {

class App_context;
class Property_editor;

// Generic Properties-window rows for the registered properties of one or
// more items (doc/property-system-plan.md D12): one widget per
// Property_type shaped by the property's Property_ui metadata, a value
// source indicator, "Reset to default", Copy / Paste Properties, mixed-value
// display for multi-selection, undo through Property_set_operation /
// Property_set_apply_operation (one operation per completed drag), and the
// formula text in place of the widget for a property driven by an
// expression (D22), with "Edit as expression" / "Remove expression" in the
// context menu.
class Dependency_property_rows
{
public:
    explicit Dependency_property_rows(App_context& context);

    // Adds rows to `editor` for the properties every item's type has, in
    // registration order, grouped by Property_ui::group. Call between the
    // caller's push_group() / pop_group(); the rows draw when the caller
    // calls show_entries().
    void add_rows(Property_editor& editor, const std::vector<std::shared_ptr<erhe::Item_base>>& items);

private:
    void row(Property_editor& editor, const erhe::property::Dependency_property& property);

    // Returns true when the widget produced a new value this frame.
    // `immediate` is set for widgets that commit on selection (combo)
    // instead of on deactivation.
    auto draw_widget(
        const erhe::property::Dependency_property& property,
        const erhe::property::Property_metadata&   metadata,
        erhe::property::Property_value&            value,
        bool&                                      immediate
    ) -> bool;

    // The formula row of a driven property; commits on Enter or deactivation.
    void draw_expression(const erhe::property::Dependency_property& property, std::string_view text);

    void begin_edit (const erhe::property::Dependency_property& property);
    void end_edit   (const erhe::property::Dependency_property& property);
    void queue_set  (const erhe::property::Dependency_property& property, const std::optional<erhe::property::Local_state>& after);
    void context_menu(const erhe::property::Dependency_property& property, const erhe::property::Property_metadata& metadata);
    void reset_to_default  (const erhe::property::Dependency_property& property);
    void edit_as_expression(const erhe::property::Dependency_property& property);
    void remove_expression (const erhe::property::Dependency_property& property);
    void copy_properties ();
    void paste_properties();

    App_context& m_context;

    // Items of the current add_rows() call; row lambdas run in the same
    // frame from show_entries().
    std::vector<std::shared_ptr<erhe::Item_base>> m_items;

    // Drag / type session: `before` captured at widget activation, one per
    // item, committed as an operation at deactivation.
    const erhe::property::Dependency_property*               m_edit_property{nullptr};
    std::vector<std::optional<erhe::property::Local_state>>  m_edit_before;
    glm::vec3                                                m_edit_euler_degrees{0.0f}; // quaternion rows edit in Euler space
    std::string                                              m_text_scratch;
    std::string                                              m_expression_scratch; // the formula being typed in the active expression row
};

}
