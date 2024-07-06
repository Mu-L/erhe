#include "windows/viewport_config_window.hpp"

#include "editor_context.hpp"
#include "editor_rendering.hpp"
#include "tools/hotbar.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor {

Viewport_config_window::Viewport_config_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Viewport config", "viewport_config"}
    , m_context                {editor_context}
{
    config.render_style_not_selected.line_color = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    config.render_style_not_selected.edge_lines = false;

    config.render_style_selected.edge_lines = false;

    auto ini = erhe::configuration::get_ini("erhe.ini", "viewport");
    ini->get("gizmo_scale",               gizmo_scale);
    ini->get("polygon_fill",              polygon_fill);
    ini->get("edge_lines",                edge_lines);
    ini->get("edge_color",                edge_color);
    ini->get("selection_polygon_fill",    selection_polygon_fill);
    ini->get("selection_edge_lines",      selection_edge_lines);
    ini->get("corner_points",             corner_points);
    ini->get("polygon_centroids",         polygon_centroids);
    ini->get("selection_bounding_box",    selection_bounding_box);
    ini->get("selection_bounding_sphere", selection_bounding_sphere);
    ini->get("selection_edge_color",      selection_edge_color);
    ini->get("clear_color",               clear_color);

    config.render_style_not_selected.polygon_fill      = polygon_fill;
    config.render_style_not_selected.edge_lines        = edge_lines;
    config.render_style_not_selected.corner_points     = corner_points;
    config.render_style_not_selected.polygon_centroids = polygon_centroids;
    config.render_style_not_selected.line_color        = edge_color;
    config.render_style_not_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    config.render_style_not_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};

    config.render_style_selected.polygon_fill      = selection_polygon_fill;
    config.render_style_selected.edge_lines        = selection_edge_lines;
    config.render_style_selected.corner_points     = corner_points;
    config.render_style_selected.polygon_centroids = polygon_centroids;
    config.render_style_selected.line_color        = selection_edge_color;
    config.render_style_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    config.render_style_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};

    config.selection_highlight_low        = glm::vec4{  0.0f,  0.0f, 0.0f, 0.0f};
    config.selection_highlight_high       = glm::vec4{200.0f, 10.0f, 1.0f, 0.5f};
    config.selection_highlight_width_low  =  0.0f;
    config.selection_highlight_width_high = -3.0f;
    config.selection_highlight_frequency  = 1.0f;

    config.gizmo_scale               = gizmo_scale;
    config.selection_bounding_box    = selection_bounding_box;
    config.selection_bounding_sphere = selection_bounding_sphere;
    config.clear_color               = clear_color;
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Viewport_config_window::render_style_ui(Render_style_data& render_style)
{
    ERHE_PROFILE_FUNCTION();

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    if (ImGui::TreeNodeEx("Polygon Fill", flags)) {
        ImGui::Checkbox("Visible", &render_style.polygon_fill);
        //if (render_style.polygon_fill) {
        //    ImGui::Text       ("Polygon Offset");
        //    ImGui::Checkbox   ("Enable", &render_style.polygon_offset_enable);
        //    ImGui::SliderFloat("Factor", &render_style.polygon_offset_factor, -2.0f, 2.0f);
        //    ImGui::SliderFloat("Units",  &render_style.polygon_offset_units,  -2.0f, 2.0f);
        //    ImGui::SliderFloat("clamp",  &render_style.polygon_offset_clamp,  -0.01f, 0.01f);
        //}
        ImGui::TreePop();
    }

    using Primitive_interface_settings = erhe::scene_renderer::Primitive_interface_settings;
    if (ImGui::TreeNodeEx("Edge Lines", flags)) {
        ImGui::Checkbox("Visible", &render_style.edge_lines);
        if (render_style.edge_lines) {
            ImGui::SliderFloat("Width",          &render_style.line_width, 0.0f, 20.0f);
            ImGui::ColorEdit4 ("Constant Color", &render_style.line_color.x,     ImGuiColorEditFlags_Float);
            erhe::imgui::make_combo(
                "Color Source",
                render_style.edge_lines_color_source,
                Primitive_interface_settings::c_primitive_color_source_strings_data.data(),
                static_cast<int>(Primitive_interface_settings::c_primitive_color_source_strings_data.size())
            );
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Polygon Centroids", flags)) {
        ImGui::Checkbox("Visible", &render_style.polygon_centroids);
        if (render_style.polygon_centroids) {
            ImGui::ColorEdit4("Constant Color", &render_style.centroid_color.x, ImGuiColorEditFlags_Float);
            erhe::imgui::make_combo(
                "Color Source",
                render_style.polygon_centroids_color_source,
                Primitive_interface_settings::c_primitive_color_source_strings_data.data(),
                static_cast<int>(Primitive_interface_settings::c_primitive_color_source_strings_data.size())
            );
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Corner Points", flags)) {
        ImGui::Checkbox  ("Visible",        &render_style.corner_points);
        ImGui::ColorEdit4("Constant Color", &render_style.corner_color.x,   ImGuiColorEditFlags_Float);
        erhe::imgui::make_combo(
            "Color Source",
            render_style.corner_points_color_source,
            Primitive_interface_settings::c_primitive_color_source_strings_data.data(),
            static_cast<int>(Primitive_interface_settings::c_primitive_color_source_strings_data.size())
        );
        ImGui::TreePop();
    }

    if (render_style.polygon_centroids || render_style.corner_points) {
        ImGui::SliderFloat("Point Size", &render_style.point_size, 0.0f, 20.0f);
    }
}
#endif

void Viewport_config_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_Framed            |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth     |
        ImGuiTreeNodeFlags_DefaultOpen
    };

    if (ImGui::Button("Make RenderDoc Capture")) {
        m_context.editor_rendering->request_renderdoc_capture();
    }

    if (edit_data != nullptr) {
        ImGui::SliderFloat("Gizmo Scale", &edit_data->gizmo_scale, 1.0f, 8.0f, "%.2f");
        ImGui::ColorEdit4("Clear Color", &edit_data->clear_color.x, ImGuiColorEditFlags_Float);

        if (ImGui::TreeNodeEx("Default Style", flags)) {
            render_style_ui(edit_data->render_style_not_selected);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Selection", flags)) {
            render_style_ui(edit_data->render_style_selected);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Selection Outline", flags)) {
            ImGui::ColorEdit4 ("Color Low",  &edit_data->selection_highlight_low.x,  ImGuiColorEditFlags_Float);
            ImGui::ColorEdit4 ("Color High", &edit_data->selection_highlight_high.x, ImGuiColorEditFlags_Float);
            ImGui::SliderFloat("Width Low",  &edit_data->selection_highlight_width_low,  -20.0f, 0.0f, "%.2f");
            ImGui::SliderFloat("Width High", &edit_data->selection_highlight_width_high, -20.0f, 0.0f, "%.2f");
            ImGui::SliderFloat("Frequency",  &edit_data->selection_highlight_frequency,    0.0f, 1.0f, "%.2f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Debug Visualizations", flags)) {
            erhe::imgui::make_combo("Light",  edit_data->debug_visualizations.light,  c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
            erhe::imgui::make_combo("Camera", edit_data->debug_visualizations.camera, c_visualization_mode_strings, IM_ARRAYSIZE(c_visualization_mode_strings));
            ImGui::TreePop();
        }
    }

    ImGui::Separator();

    ImGui::SliderFloat("LoD Bias", &rendertarget_mesh_lod_bias, -8.0f, 8.0f);

    m_context.editor_rendering->imgui();

    if (ImGui::TreeNodeEx("Hotbar", flags)) {
        auto& hotbar = *m_context.hotbar;
        auto& color_inactive = hotbar.get_color(0);
        auto& color_hover    = hotbar.get_color(1);
        auto& color_active   = hotbar.get_color(2);
        ImGui::ColorEdit4("Inactive", &color_inactive.x, ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4("Hover",    &color_hover.x,    ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4("Active",   &color_active.x,   ImGuiColorEditFlags_Float);

        auto position = hotbar.get_position();
        if (ImGui::DragFloat3("Position", &position.x, 0.1f)) {
            hotbar.set_position(position);
        }
        ImGui::TreePop();

        bool locked = hotbar.get_locked();
        if (ImGui::Checkbox("Locked", &locked)) {
            hotbar.set_locked(locked);
        }
    }
#endif
}

} // namespace editor
