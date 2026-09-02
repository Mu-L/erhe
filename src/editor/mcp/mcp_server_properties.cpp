// Mcp_server item property tools (get_item_properties, set_item_property):
// the generic erhe::property view of any item (doc/property-system.md
// D13). Values travel as strings through erhe_property/property_string.hpp,
// so enumerations travel as their labels.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "operations/style_set_operation.hpp"
#include "scene/item_lookup.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_property/property_style.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace editor {

using namespace mcp_server_detail;

namespace {

// Resolves args.item_id (any scene) or args.item_name (args.scene_name, or
// the first scene when absent).
auto resolve_item(App_context& context, const json& args, std::string& out_error) -> std::shared_ptr<erhe::Item_base>
{
    if (context.app_scenes == nullptr) {
        out_error = "No scenes";
        return {};
    }
    const std::size_t item_id   = args.value("item_id", std::size_t{0});
    const std::string item_name = args.value("item_name", "");
    const std::string scene_name = args.value("scene_name", "");

    if (item_id != 0) {
        for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
            if (!scene_root) {
                continue;
            }
            std::shared_ptr<erhe::Item_base> item = find_item_in_scene_by_id(*scene_root, item_id);
            if (item) {
                return item;
            }
        }
        out_error = "Item not found with id: " + std::to_string(item_id);
        return {};
    }
    if (item_name.empty()) {
        out_error = "item_id or item_name is required";
        return {};
    }
    Scene_root* scene_root = nullptr;
    if (!scene_name.empty()) {
        for (const std::shared_ptr<Scene_root>& candidate : context.app_scenes->get_scene_roots()) {
            if (candidate && (candidate->get_name() == scene_name)) {
                scene_root = candidate.get();
                break;
            }
        }
        if (scene_root == nullptr) {
            out_error = "Scene not found: " + scene_name;
            return {};
        }
    } else if (!context.app_scenes->get_scene_roots().empty()) {
        scene_root = context.app_scenes->get_scene_roots().front().get();
    }
    if (scene_root == nullptr) {
        out_error = "No scene";
        return {};
    }
    std::shared_ptr<erhe::Item_base> item = find_item_in_scene_by_name(*scene_root, item_name);
    if (!item) {
        out_error = "Item not found with name: " + item_name;
    }
    return item;
}

auto value_json(const erhe::property::Dependency_property& property, const erhe::property::Property_value& value) -> json
{
    return json(erhe::property::to_string(property, value));
}

} // anonymous namespace

auto Mcp_server::query_item_properties(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<erhe::Item_base> item = resolve_item(m_context, args, error);
    if (!item) {
        return make_error_content(error);
    }

    json result;
    result["item"] = {
        {"id",     item->get_id()},
        {"name",   item->get_name()},
        {"type",   std::string{item->get_type_name()}},
        {"sealed", item->is_sealed()}, // lock_edit (D24): writes are refused
        {"style",  item->get_style() ? json(std::string{item->get_style()->get_name()}) : json(nullptr)} // D25
    };
    json properties = json::array();
    const uint64_t owner_type = item->get_property_owner_type();
    erhe::property::Property_registry::get().for_each_property_of_type(
        item->get_type(),
        [&](const erhe::property::Dependency_property& property) {
            const erhe::property::Property_metadata& metadata = property.get_metadata(owner_type);
            const std::optional<erhe::property::Property_value> local      = item->read_local_value(property);
            const std::optional<std::string_view>               expression = item->get_expression(property);
            json entry = {
                {"name",       std::string{property.get_name()}},
                {"type",       erhe::property::c_str(property.get_type())},
                {"value",      value_json(property, item->get_value(property))},
                {"source",     erhe::property::c_str(item->get_value_source(property))},
                {"local",      local.has_value() ? value_json(property, local.value()) : json(nullptr)},
                {"expression", expression.has_value() ? json(std::string{expression.value()}) : json(nullptr)},
                {"default",    value_json(property, metadata.default_value.value())},
                {"read_only",  property.is_read_only()},
                {"inherits",   metadata.inherits},
                {"coerced",    item->is_coerced(property)},
                {"flags",      metadata.flags}
            };
            if (const std::string_view error = item->get_expression_error(property); !error.empty()) {
                entry["expression_error"] = std::string{error};
            }
            if (const erhe::property::Enum_info* info = property.get_enum_info(); info != nullptr) {
                json labels = json::array();
                for (const erhe::property::Enum_entry& e : info->get_entries()) {
                    labels.push_back(std::string{e.label});
                }
                entry["enum_labels"] = labels;
            }
            properties.push_back(entry);
        }
    );
    // Attached properties with a local value on this item
    json attached = json::array();
    item->for_each_local_value(
        [&](const erhe::property::Dependency_property& property, const erhe::property::Property_value& value) {
            if (property.is_attached()) {
                attached.push_back({
                    {"name",  std::string{property.get_name()}},
                    {"type",  erhe::property::c_str(property.get_type())},
                    {"value", value_json(property, value)}
                });
            }
        }
    );
    result["properties"] = properties;
    result["attached"]   = attached;
    return make_json_content(result).dump();
}

auto Mcp_server::action_set_item_property(const json& args) -> std::string
{
    if (m_context.operation_stack == nullptr) {
        return make_error_content("Operation stack not available");
    }
    std::string error;
    const std::shared_ptr<erhe::Item_base> item = resolve_item(m_context, args, error);
    if (!item) {
        return make_error_content(error);
    }
    const std::string property_name = args.value("property", "");
    if (property_name.empty()) {
        return make_error_content("property is required");
    }
    const erhe::property::Dependency_property* property = erhe::property::Property_registry::get().find_for_type(item->get_type(), property_name);
    if (property == nullptr) {
        return make_error_content("Item '" + item->get_name() + "' (" + std::string{item->get_type_name()} + ") has no property '" + property_name + "'");
    }
    if (property->is_read_only()) {
        return make_error_content("Property '" + property_name + "' is read-only");
    }
    if (item->is_sealed()) {
        return make_error_content("Item '" + item->get_name() + "' is sealed (lock_edit): unlock_items first, or edit the prefab's source scene");
    }

    // An expression (doc/property-system.md D22) instead of a value.
    const auto expression_it = args.find("expression");
    if ((expression_it != args.end()) && !expression_it->is_null()) {
        if (!expression_it->is_string()) {
            return make_error_content("expression must be a string");
        }
        const std::string text = expression_it->get<std::string>();
        if (!erhe::property::validate_expression_text(*property, text, error)) {
            return make_error_content("expression '" + text + "' rejected for property '" + property_name + "': " + error);
        }
        const std::optional<erhe::property::Local_state> before = item->read_local_state(*property);
        m_context.operation_stack->queue(
            std::make_shared<Property_set_operation>(item, *property, before, erhe::property::Local_state{erhe::property::Expression_text{text}})
        );
        json result = {
            {"item",       {{"id", item->get_id()}, {"name", item->get_name()}, {"type", std::string{item->get_type_name()}}}},
            {"property",   property_name},
            {"before",     describe_local_state(*property, before)},
            {"expression", text},
            {"queued",     true}
        };
        return make_json_content(result).dump();
    }

    std::optional<erhe::property::Property_value> after;
    const auto value_it = args.find("value");
    const bool clear = (value_it == args.end()) || value_it->is_null();
    if (!clear) {
        // Accept a string in property_string form, or a JSON number / bool /
        // array of numbers, which is rendered to that form first.
        std::string text;
        if (value_it->is_string()) {
            text = value_it->get<std::string>();
        } else if (value_it->is_boolean()) {
            text = value_it->get<bool>() ? "true" : "false";
        } else if (value_it->is_number()) {
            text = value_it->dump();
        } else if (value_it->is_array()) {
            for (const json& component : *value_it) {
                if (!component.is_number()) {
                    return make_error_content("value array entries must be numbers");
                }
                if (!text.empty()) {
                    text += " ";
                }
                text += component.dump();
            }
        } else {
            return make_error_content("value must be a string, number, bool, array of numbers, or null (reset to default)");
        }
        after = erhe::property::parse_value(*property, text);
        if (!after.has_value()) {
            return make_error_content("'" + text + "' is not a valid " + erhe::property::c_str(property->get_type()) + " for property '" + property_name + "'");
        }
        if (!property->validate(after.value())) {
            return make_error_content("'" + text + "' was rejected by property '" + property_name + "' validation");
        }
    }

    const std::optional<erhe::property::Local_state> before = item->read_local_state(*property);
    m_context.operation_stack->queue(std::make_shared<Property_set_operation>(item, *property, before, to_local_state(after)));

    json result = {
        {"item",     {{"id", item->get_id()}, {"name", item->get_name()}, {"type", std::string{item->get_type_name()}}}},
        {"property", property_name},
        {"before",   before.has_value() ? json(describe_local_state(*property, before)) : json(nullptr)},
        {"after",    after.has_value() ? value_json(*property, after.value()) : json(nullptr)},
        {"queued",   true}
    };
    return make_json_content(result).dump();
}

// Style layer (D25): the source item's local values become the target's
// style, named after the source. Local values of the target stay on top.
auto Mcp_server::action_set_item_style(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<erhe::Item_base> item = resolve_item(m_context, args, error);
    if (!item) {
        return make_error_content(error);
    }
    json source_args = json::object();
    if (args.contains("source_item_id")) {
        source_args["item_id"] = args["source_item_id"];
    }
    if (args.contains("source_item_name")) {
        source_args["item_name"] = args["source_item_name"];
    }
    if (args.contains("scene_name")) {
        source_args["scene_name"] = args["scene_name"];
    }
    const std::shared_ptr<erhe::Item_base> source = resolve_item(m_context, source_args, error);
    if (!source) {
        return make_error_content("source: " + error);
    }
    if (item->is_sealed()) {
        return make_error_content("Item '" + item->get_name() + "' is sealed (lock_edit): unlock_items first");
    }
    // Only the properties the target's type has (by identity), as paste.
    const erhe::property::Property_registry& registry = erhe::property::Property_registry::get();
    erhe::property::Property_set values;
    const erhe::property::Property_set source_values = erhe::property::Property_set::read_local_values(*source);
    for (const erhe::property::Property_set::Entry& entry : source_values.entries()) {
        if (entry.property->is_read_only()) {
            continue;
        }
        if (registry.find_for_type(item->get_type(), entry.property->get_name()) == entry.property) {
            values.set(*entry.property, entry.value);
        }
    }
    if (values.empty()) {
        return make_error_content("Item '" + source->get_name() + "' has no local values that '" + item->get_name() + "' (" + std::string{item->get_type_name()} + ") could use");
    }
    json names = json::array();
    for (const erhe::property::Property_set::Entry& entry : values.entries()) {
        names.push_back(std::string{entry.property->get_name()});
    }
    const std::shared_ptr<const erhe::property::Property_style> style = std::make_shared<const erhe::property::Property_style>(source->get_name(), std::move(values));
    m_context.operation_stack->queue(std::make_shared<Style_set_operation>(item, item->get_style(), style));
    return make_json_content(
        json{
            {"item",       {{"id", item->get_id()}, {"name", item->get_name()}, {"type", std::string{item->get_type_name()}}}},
            {"style",      std::string{style->get_name()}},
            {"properties", names},
            {"before",     item->get_style() ? json(std::string{item->get_style()->get_name()}) : json(nullptr)},
            {"queued",     true}
        }
    ).dump();
}

auto Mcp_server::action_clear_item_style(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<erhe::Item_base> item = resolve_item(m_context, args, error);
    if (!item) {
        return make_error_content(error);
    }
    if (item->is_sealed()) {
        return make_error_content("Item '" + item->get_name() + "' is sealed (lock_edit): unlock_items first");
    }
    if (!item->get_style()) {
        return make_error_content("Item '" + item->get_name() + "' has no style");
    }
    const std::string before{item->get_style()->get_name()};
    m_context.operation_stack->queue(std::make_shared<Style_set_operation>(item, item->get_style(), nullptr));
    return make_json_content(
        json{
            {"item",   {{"id", item->get_id()}, {"name", item->get_name()}, {"type", std::string{item->get_type_name()}}}},
            {"before", before},
            {"queued", true}
        }
    ).dump();
}

} // namespace editor
