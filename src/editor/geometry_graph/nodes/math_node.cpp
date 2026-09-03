#include "geometry_graph/nodes/math_node.hpp"

#include "graph_editor/graph_editor_widgets.hpp"
#include "graph_editor/graph_node_property_bridge.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <cmath>

namespace editor {

namespace {
constexpr erhe::property::Enum_entry c_math_operation_entries[] = {
    {"Add",      static_cast<int32_t>(Math_node::Math_operation::add)},
    {"Subtract", static_cast<int32_t>(Math_node::Math_operation::subtract)},
    {"Multiply", static_cast<int32_t>(Math_node::Math_operation::multiply)},
    {"Divide",   static_cast<int32_t>(Math_node::Math_operation::divide)},
    {"Power",    static_cast<int32_t>(Math_node::Math_operation::power)},
    {"Min",      static_cast<int32_t>(Math_node::Math_operation::minimum)},
    {"Max",      static_cast<int32_t>(Math_node::Math_operation::maximum)},
    {"Abs",      static_cast<int32_t>(Math_node::Math_operation::absolute)},
    {"Sqrt",     static_cast<int32_t>(Math_node::Math_operation::square_root)},
    {"Sin",      static_cast<int32_t>(Math_node::Math_operation::sine)},
    {"Cos",      static_cast<int32_t>(Math_node::Math_operation::cosine)},
};
const erhe::property::Enum_info c_math_operation_enum_info{"Math_operation", c_math_operation_entries};
} // anonymous namespace

auto Math_node::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(Graph_editor_node::property_owner_type(), "Math_node");
    return s_id;
}

auto Math_node::get_property_owner_type() const -> erhe::property::Owner_type
{
    return property_owner_type();
}

const erhe::property::Property<Math_node::Math_operation> Math_node::operation_property =
    erhe::property::Property<Math_node::Math_operation>::register_member(
        "operation", Math_node::property_owner_type(),
        c_math_operation_enum_info, &Math_node::m_operation,
        erhe::property::Property_metadata{
            .flags  = erhe::property::Property_flags::none, // the graph JSON is the serializer
            .ui     = erhe::property::Property_ui{.group = "Parameters", .label = "Operation"}
        },
        mark_node_dirty
    );

const erhe::property::Property<float> Math_node::a_property =
    erhe::property::Property<float>::register_member(
        "a", Math_node::property_owner_type(), &Math_node::m_a,
        erhe::property::Property_metadata{
            .flags  = erhe::property::Property_flags::none,
            .ui     = erhe::property::Property_ui{.step = 0.01f, .group = "Parameters", .label = "A"}
        },
        mark_node_dirty
    );

const erhe::property::Property<float> Math_node::b_property =
    erhe::property::Property<float>::register_member(
        "b", Math_node::property_owner_type(), &Math_node::m_b,
        erhe::property::Property_metadata{
            .flags  = erhe::property::Property_flags::none,
            .ui     = erhe::property::Property_ui{.step = 0.01f, .group = "Parameters", .label = "B"}
        },
        mark_node_dirty
    );

Math_node::Math_node()
    : Geometry_graph_node{"Math"}
{
    make_input_pin(Geometry_pin_key::float_value, "A");
    make_input_pin(Geometry_pin_key::float_value, "B");
    make_output_pin(Geometry_pin_key::float_value, "out");
}

void Math_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float a = get_input(0).get_float(m_a);
    const float b = get_input(1).get_float(m_b);

    float result = 0.0f;
    switch (m_operation) {
        case Math_operation::add:         result = a + b; break;
        case Math_operation::subtract:    result = a - b; break;
        case Math_operation::multiply:    result = a * b; break;
        case Math_operation::divide:      result = (b != 0.0f) ? (a / b) : 0.0f; break;
        case Math_operation::power:       result = std::pow(a, b); break;
        case Math_operation::minimum:     result = std::min(a, b); break;
        case Math_operation::maximum:     result = std::max(a, b); break;
        case Math_operation::absolute:    result = std::abs(a); break;
        case Math_operation::square_root: result = (a >= 0.0f) ? std::sqrt(a) : 0.0f; break;
        case Math_operation::sine:        result = std::sin(a); break;
        case Math_operation::cosine:      result = std::cos(a); break;
    }
    set_output(0, Geometry_payload{.value = result});
}

void Math_node::imgui()
{
    const char* operation_names[] = { "Add", "Subtract", "Multiply", "Divide", "Power", "Min", "Max", "Abs", "Sqrt", "Sin", "Cos" };
    int operation = static_cast<int>(m_operation);
    if (imgui_enum_combo("operation", operation, operation_names, IM_ARRAYSIZE(operation_names), content_scale())) {
        m_operation = static_cast<Math_operation>(operation);
        mark_dirty();
    }
    ImGui::TextUnformatted("A");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##a", &m_a, 0.01f)) { mark_dirty(); }
    ImGui::TextUnformatted("B");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##b", &m_b, 0.01f)) { mark_dirty(); }
    ImGui::Text("= %f", static_cast<double>(get_output(0).get_float()));
}

void Math_node::write_parameters(nlohmann::json& out) const
{
    out["operation"] = static_cast<int>(m_operation);
    out["a"]         = m_a;
    out["b"]         = m_b;
}

void Math_node::read_parameters(const nlohmann::json& in)
{
    m_operation = static_cast<Math_operation>(in.value("operation", static_cast<int>(m_operation)));
    m_a         = in.value("a", m_a);
    m_b         = in.value("b", m_b);
    mark_dirty();
}

}
