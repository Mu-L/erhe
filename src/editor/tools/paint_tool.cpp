// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "tools/paint_tool.hpp"

#include "editor_context.hpp"
#include "editor_message_bus.hpp"
#include "editor_settings.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

#include <fmt/core.h>
#include <fmt/format.h>

#include <string>

namespace editor {

#pragma region Commands
Paint_vertex_command::Paint_vertex_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Paint_tool.paint_vertex"}
    , m_context{context}
{
}

void Paint_vertex_command::try_ready()
{
    if (!m_context.paint_tool->is_enabled()) {
        return;
    }

    if (m_context.paint_tool->try_ready()) {
        set_ready();
    }
}

auto Paint_vertex_command::try_call() -> bool
{
    if (!m_context.paint_tool->is_enabled()) {
        return false;
    }

    if (get_command_state() == erhe::commands::State::Ready) {
        set_active();
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }
    if (m_context.paint_tool->get_hover_scene_view() == nullptr) {
        set_inactive();
        return false;
    }

    m_context.paint_tool->paint();
    return true;
}

#pragma endregion Commands

namespace {

auto vertex_id_from_corner_id(
    erhe::scene::Mesh&              mesh,
    const erhe::geometry::Geometry& geometry,
    const erhe::geometry::Corner_id corner_id
) -> std::optional<uint32_t>
{
    for (const auto& primitive : mesh.get_primitives()) {
        if (primitive.render_shape) {
            const std::shared_ptr<erhe::geometry::Geometry>& geometry_in_primitive = primitive.render_shape->get_geometry_const();
            if (geometry_in_primitive.get() == &geometry) {
                return primitive.render_shape->get_vertex_id_from_corner_id(corner_id);
            }
        }
    }
    return std::nullopt;
}

}

Paint_tool::Paint_tool(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus,
    Headset_view&                headset_view,
    Icon_set&                    icon_set,
    Tools&                       tools
)
    : erhe::imgui::Imgui_window     {imgui_renderer, imgui_windows, "Paint Tool", "paint_tool"}
    , Tool                          {editor_context}
    , m_paint_vertex_command        {commands, editor_context}
    , m_drag_redirect_update_command{commands, m_paint_vertex_command}
    , m_drag_enable_command         {commands, m_drag_redirect_update_command}
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(c_priority);
    set_description  ("Paint Tool");
    set_flags        (Tool_flags::toolbox);
    set_icon         (icon_set.icons.brush_small);
    tools.register_tool(this);

    m_paint_vertex_command        .set_host(this);
    m_drag_redirect_update_command.set_host(this);
    m_drag_enable_command         .set_host(this);

    commands.register_command(&m_paint_vertex_command);
    commands.register_command(&m_drag_redirect_update_command);
    commands.register_command(&m_drag_enable_command);
    commands.bind_command_to_mouse_drag(&m_paint_vertex_command, erhe::window::Mouse_button_left, true);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = headset_view.get_headset();
    if (headset != nullptr) {
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.a_click,       erhe::commands::Button_trigger::Any);
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.trigger_click, erhe::commands::Button_trigger::Any);
        commands.bind_command_to_update           (&m_drag_redirect_update_command);
    }
#else
    static_cast<void>(headset_view);
#endif
    //// tools.register_tool(this);

    m_palette.emplace_back(240.0f / 255.0f, 163.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_palette.emplace_back(  0.0f / 255.0f, 117.0f / 255.0f, 220.0f / 255.0f, 1.0f);
    m_palette.emplace_back(153.0f / 255.0f,  63.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 76.0f / 255.0f,   0.0f / 255.0f,  92.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 25.0f / 255.0f,  25.0f / 255.0f,  25.0f / 255.0f, 1.0f);
    m_palette.emplace_back(  0.0f / 255.0f,  92.0f / 255.0f,  49.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 43.0f / 255.0f, 206.0f / 255.0f,  72.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 204.0f / 255.0f, 153.0f / 255.0f, 1.0f);
    m_palette.emplace_back(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_palette.emplace_back(148.0f / 255.0f, 255.0f / 255.0f, 181.0f / 255.0f, 1.0f);
    m_palette.emplace_back(143.0f / 255.0f, 124.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(157.0f / 255.0f, 204.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(194.0f / 255.0f,   0.0f / 255.0f, 136.0f / 255.0f, 1.0f);
    m_palette.emplace_back(  0.0f / 255.0f,  51.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 164.0f / 255.0f,   5.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 168.0f / 255.0f, 187.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 66.0f / 255.0f, 102.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f,   0.0f / 255.0f,  16.0f / 255.0f, 1.0f);
    m_palette.emplace_back( 94.0f / 255.0f, 241.0f / 255.0f, 242.0f / 255.0f, 1.0f);
    m_palette.emplace_back(  0.0f / 255.0f, 153.0f / 255.0f, 143.0f / 255.0f, 1.0f);
    m_palette.emplace_back(224.0f / 255.0f, 255.0f / 255.0f, 102.0f / 255.0f, 1.0f);
    m_palette.emplace_back(116.0f / 255.0f,  10.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_palette.emplace_back(153.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 255.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f, 255.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_palette.emplace_back(255.0f / 255.0f,  80.0f / 255.0f,   5.0f / 255.0f, 1.0f);

    m_ngon_colors.emplace_back(230.0f / 255.0f,  25.0f / 255.0f,  75.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back( 60.0f / 255.0f, 180.0f / 255.0f,  75.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 225.0f / 255.0f,  25.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f, 130.0f / 255.0f, 200.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(245.0f / 255.0f, 130.0f / 255.0f,  48.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(145.0f / 255.0f,  30.0f / 255.0f, 180.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back( 70.0f / 255.0f, 240.0f / 255.0f, 240.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(240.0f / 255.0f,  50.0f / 255.0f, 230.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(210.0f / 255.0f, 245.0f / 255.0f,  60.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(250.0f / 255.0f, 190.0f / 255.0f, 212.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(220.0f / 255.0f, 190.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(170.0f / 255.0f, 110.0f / 255.0f,  40.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 250.0f / 255.0f, 200.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(128.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(170.0f / 255.0f, 255.0f / 255.0f, 195.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(128.0f / 255.0f, 128.0f / 255.0f,   0.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 215.0f / 255.0f, 180.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f,   0.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    m_ngon_colors.emplace_back(  0.0f / 255.0f,   0.0f / 255.0f,   0.0f / 255.0f, 1.0f);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            Tool::on_message(message);
        }
    );
}

void Paint_tool::tool_render(const Render_context& context)
{
    if (!is_enabled()) {
        return;
    }
    erhe::renderer::Scoped_line_renderer line_renderer = context.get_line_renderer(2, true, true);
    line_renderer.set_thickness(5.0f);

    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    if (
        !content.valid                ||
        !content.position.has_value() ||
        !content.normal.has_value()   ||
        !content.geometry
    ) {
        return;
    }

    ERHE_VERIFY(content.mesh != nullptr);
    ERHE_VERIFY(content.primitive_index != std::numeric_limits<std::size_t>::max());

    erhe::geometry::Geometry& geometry = *content.geometry.get();
    auto* const point_locations = geometry.point_attributes().find<glm::vec3>(erhe::geometry::c_point_locations);
    if (point_locations == nullptr) {
        return;
    }

    const erhe::geometry::Polygon_id polygon_id = static_cast<erhe::geometry::Polygon_id>(content.polygon_id);
    const erhe::geometry::Polygon&   polygon    = geometry.polygons.at(polygon_id);
    if (polygon.corner_count == 0) {
        return;
    }

    const glm::vec3 hover_position_in_world = content.position.value();

    const auto* node = content.mesh->get_node();
    if (node == nullptr) {
        return;
    }

    const glm::vec3 hover_position_in_mesh = node->transform_point_from_world_to_local(hover_position_in_world);

    float                     max_distance_squared = std::numeric_limits<float>::max();
    erhe::geometry::Point_id  nearest_point_id     = 0;
    erhe::geometry::Corner_id nearest_corner_id    = 0;
    std::vector<erhe::geometry::Point_id> corner_points;
    polygon.for_each_corner_const(
        geometry,
        [&](const erhe::geometry::Polygon::Polygon_corner_context_const& i) {
            const erhe::geometry::Point_id point_id = i.corner.point_id;
            const glm::vec3 p_in_mesh = point_locations->get(point_id);
            corner_points.push_back(point_id);
            const float d2 = glm::distance2(hover_position_in_mesh, p_in_mesh);
            if (d2 < max_distance_squared) {
                max_distance_squared = d2;
                nearest_point_id = point_id;
                nearest_corner_id = i.corner_id;
            }
        }
    );

    glm::vec3 n = glm::normalize(content.normal.value());

    for (const erhe::geometry::Point_id point_id : corner_points) {
        const glm::vec3 p_in_mesh = point_locations->get(point_id);
        const glm::vec3 p = node->transform_point_from_local_to_world(p_in_mesh);
        line_renderer.add_lines(
            (point_id == nearest_point_id) 
                ? glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}
                : glm::vec4{1.0f, 0.0f, 1.0f, 1.0f},
            {{ p, p + 0.2f * n }}
        );
    }
}

void Paint_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    if (new_priority < old_priority) {
        disable();
    }
    if (new_priority > old_priority) {
        enable();
    }
}

auto Paint_tool::try_ready() -> bool
{
    if (!Command_host::is_enabled()) {
        return false;
    }

    auto* scene_view = get_hover_scene_view();

    if (scene_view == nullptr) {
        return false;
    }

    const Hover_entry* hover = scene_view->get_nearest_hover(Hover_entry::all_bits);
    return (hover != nullptr) && (hover->mask == Hover_entry::content_bit) && hover->valid;
}

void Paint_tool::paint_corner(
    erhe::scene::Mesh&              mesh,
    const erhe::geometry::Geometry& geometry,
    erhe::geometry::Corner_id       corner_id,
    const glm::vec4                 color
)
{
    const auto vertex_id_opt = vertex_id_from_corner_id(mesh, geometry, corner_id);
    if (!vertex_id_opt.has_value()) {
        return;
    }
    paint_vertex(mesh, geometry, vertex_id_opt.value(), color);
}

void Paint_tool::paint_vertex(
    erhe::scene::Mesh&              mesh,
    const erhe::geometry::Geometry& geometry,
    const uint32_t                  vertex_id,
    const glm::vec4                 color
)
{
    using Vertex_attribute = erhe::graphics::Vertex_attribute;
    auto& mesh_memory = *m_context.mesh_memory;
    const erhe::graphics::Vertex_format&    vertex_format = mesh_memory.buffer_info.vertex_format;
    const erhe::graphics::Vertex_attribute* attribute     = vertex_format.find_attribute(erhe::graphics::Vertex_attribute::Usage_type::color, 0);
    if (attribute == nullptr) {
        return;
    }

    const std::size_t vertex_offset = vertex_id * vertex_format.stride() + attribute->offset;

    std::vector<std::uint8_t> buffer;

    for (erhe::primitive::Primitive& primitive : mesh.get_mutable_primitives()) {
        if (!primitive.render_shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry>& geometry_in_mesh = primitive.render_shape->get_geometry();
        if (geometry_in_mesh.get() != &geometry) {
            continue;
        }
        const erhe::primitive::Buffer_mesh& buffer_mesh = primitive.render_shape->get_renderable_mesh();
        const std::size_t range_byte_offset = buffer_mesh.vertex_buffer_range.byte_offset;
        if (attribute->data_type == erhe::dataformat::Format::format_32_vec4_float) {
            buffer.resize(sizeof(float) * 4);
            auto* const ptr = reinterpret_cast<float*>(buffer.data());
            ptr[0] = color.x;
            ptr[1] = color.y;
            ptr[2] = color.z;
            ptr[3] = color.w;
            mesh_memory.gl_buffer_transfer_queue.enqueue(
                mesh_memory.gl_vertex_buffer,
                range_byte_offset + vertex_offset,
                std::move(buffer)
            );
        } else if (attribute->data_type == erhe::dataformat::Format::format_8_vec4_unorm) {
            buffer.resize(sizeof(uint8_t) * 4);
            auto* const ptr = reinterpret_cast<uint8_t*>(buffer.data());
            ptr[0] = erhe::dataformat::float_to_unorm8(color.x);
            ptr[1] = erhe::dataformat::float_to_unorm8(color.y);
            ptr[2] = erhe::dataformat::float_to_unorm8(color.z);
            ptr[3] = erhe::dataformat::float_to_unorm8(color.w);
            mesh_memory.gl_buffer_transfer_queue.enqueue(
                mesh_memory.gl_vertex_buffer,
                range_byte_offset + vertex_offset,
                std::move(buffer)
            );
        }

        break;
    }
}

void Paint_tool::paint()
{
    m_point_id.reset();
    m_corner_id.reset();

    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    if (!content.valid || !content.position.has_value() || !content.geometry) {
        return;
    }

    ERHE_VERIFY(content.mesh != nullptr);
    ERHE_VERIFY(content.primitive_index != std::numeric_limits<std::size_t>::max());

    erhe::geometry::Geometry& geometry = *content.geometry.get();
    auto* const point_locations = geometry.point_attributes().find<glm::vec3>(
        erhe::geometry::c_point_locations
    );
    if (point_locations == nullptr) {
        return;
    }

    const erhe::geometry::Polygon_id polygon_id = static_cast<erhe::geometry::Polygon_id>(content.polygon_id);
    const erhe::geometry::Polygon&   polygon    = geometry.polygons.at(polygon_id);
    if (polygon.corner_count == 0) {
        return;
    }

    const glm::vec3 hover_position_in_world = content.position.value();

    const auto* node = content.mesh->get_node();
    if (node == nullptr) {
        return;
    }

    const glm::vec3 hover_position_in_mesh = node->transform_point_from_world_to_local(hover_position_in_world);

    float                     max_distance_squared = std::numeric_limits<float>::max();
    erhe::geometry::Point_id  nearest_point_id     = 0;
    erhe::geometry::Corner_id nearest_corner_id    = 0;
    polygon.for_each_corner_const(
        geometry,
        [&](const erhe::geometry::Polygon::Polygon_corner_context_const& i) {
            const erhe::geometry::Point_id point_id = i.corner.point_id;
            const glm::vec3 p = point_locations->get(point_id);
            const float d2 = glm::distance2(hover_position_in_mesh, p);
            if (d2 < max_distance_squared) {
                max_distance_squared = d2;
                nearest_point_id = point_id;
                nearest_corner_id = i.corner_id;
            }
        }
    );

    m_point_id  = nearest_point_id;
    m_corner_id = nearest_corner_id;

    glm::vec4 color = m_palette.at(m_selected_palette_slot);
    switch (m_paint_mode) {
        case Paint_mode::Corner: {
            paint_corner(*content.mesh, geometry, nearest_corner_id, color);
            break;
        }
        case Paint_mode::Point: {
            const erhe::geometry::Corner& corner = geometry.corners.at(nearest_corner_id);
            const erhe::geometry::Point&  point  = geometry.points.at(corner.point_id);
            //const uint32_t vertex_id = content.p
            point.for_each_corner_const(
                geometry,
                [&](const auto& i) {
                    paint_corner(*content.mesh, geometry, i.corner_id, color);
                }
            );
            break;
        }
        case Paint_mode::Polygon: {
            polygon.for_each_corner_const(
                geometry,
                [&](const erhe::geometry::Polygon::Polygon_corner_context_const& i) {
                    paint_corner(*content.mesh, geometry, i.corner_id, color);
                }
            );
            break;
        }
    }
}

void Paint_tool::tool_properties(erhe::imgui::Imgui_window&)
{
    erhe::imgui::make_combo("Mode", m_paint_mode, c_paint_mode_strings, IM_ARRAYSIZE(c_paint_mode_strings));

    ImGui::ColorEdit4("Color", &m_palette.at(m_selected_palette_slot).x, ImGuiColorEditFlags_Float);

    int slot = 0;
    int row_length = 1;
    for (glm::vec4& entry : m_palette) {
        ImGui::PushID(slot);
        if (slot > 0) {
            if (row_length < 10) {
                ImGui::SameLine();
                row_length++;
            } else {
                row_length = 1;
            }
        }
        ImVec4 color{entry.x, entry.y, entry.z, entry.w};
        ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoTooltip;

        if (ImGui::ColorButton("##", entry, flags)) {
            m_selected_palette_slot = slot;
        }
        ImGui::PopID();
        ++slot;
    }
}

void Paint_tool::imgui()
{
    const float ui_scale = m_context.editor_settings->get_ui_scale();
    const ImVec2 button_size{110.0f * ui_scale, 0.0f};

    ImGui::SetNextItemWidth(200);
    erhe::imgui::make_combo("Paint mode", m_paint_mode, c_paint_mode_strings, IM_ARRAYSIZE(c_paint_mode_strings));

    if (m_point_id.has_value()) {
        ImGui::Text("Point: %u", m_point_id.value());
    }
    if (m_corner_id.has_value()) {
        ImGui::Text("Corner: %u", m_corner_id.value());
    }

    int corner_count = 3;
    for (auto& ngon_color : m_ngon_colors) {
        std::string label = fmt::format("{}-gon", corner_count);
        ImGui::ColorEdit4(label.c_str(), &ngon_color.x, ImGuiColorEditFlags_Float);
        ++corner_count;
    }
    std::string label = fmt::format("{}-gon", corner_count);
    if (ImGui::Button(label.c_str(), button_size)) {
        m_ngon_colors.push_back(glm::vec4{1.0f, 1.0f, 1.0f, 1.0f});
    }
    if (ImGui::Button("Color Selection") && !m_ngon_colors.empty()) {
        const auto& selection = m_context.selection->get_selection();
        for (const auto& item : selection) {
            auto* node = std::dynamic_pointer_cast<erhe::scene::Node>(item).get();
            auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);
            if (!mesh) {
                if (node) {
                    mesh = erhe::scene::get_mesh(node);
                }
                if (!mesh) {
                    continue;
                }
            }
            for (erhe::primitive::Primitive& primitive : mesh->get_mutable_primitives()) {
                if (!primitive.render_shape) {
                    continue;
                }
                const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive.render_shape->get_geometry();
                if (!geometry) {
                    continue;
                }
                geometry->for_each_polygon_const(
                    [&](const auto& i) {
                        const std::size_t color_index = std::min(
                            static_cast<std::size_t>(i.polygon.corner_count),
                            m_ngon_colors.size() - 1
                        );

                        const glm::vec4 color = m_ngon_colors.at(color_index);
                        i.polygon.for_each_corner_const(
                            *geometry.get(),
                            [&](const erhe::geometry::Polygon::Polygon_corner_context_const& i) {
                                paint_corner(*mesh, *geometry.get(), i.corner_id, color);
                            }
                        );
                    }
                );
            }
        }
    }
}

} // namespace editor
