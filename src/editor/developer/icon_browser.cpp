#include "developer/icon_browser.hpp"

#include "app_context.hpp"

#include "windows/IconsMaterialDesignIcons.h"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

namespace editor {

Icon_browser::Icon_browser(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&              context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Icon Browser", "icon_browser"}
    , m_context                {context}
{
}

void Icon_browser::imgui()
{
    ImFont* icon_font = m_context.imgui_renderer->icon_font();
    if (icon_font == nullptr) {
        return;
    }

    const float font_size = m_imgui_renderer.get_imgui_settings().font_size; // TODO mono font size
    ImGui::PushFont(icon_font, font_size);
    ImGui::TextUnformatted(ICON_MDI_FILTER);
    ImGui::PopFont();
    ImGui::SameLine();
    m_name_filter.Draw("##icon_filter", -FLT_MIN);

    ImVec2 button_size{36.0f, 36.0f};
    int x = 0;
    for (int i = 0; (icon_MDI_codes[i] != 0) && (icon_MDI_names[i] != nullptr); ++i) {
        ImGui::PushID(i);
        ERHE_DEFER( ImGui::PopID(); );
        const bool show_by_name = m_name_filter.PassFilter(icon_MDI_names[i]);
        if (!show_by_name) {
            continue;
        }

        if ((icon_MDI_unicodes[i] < ICON_MIN_MDI) || (icon_MDI_unicodes[i] > ICON_MAX_MDI)) {
            continue;
        }
        ImGui::PushFont(icon_font, font_size);
        ImGui::Button(icon_MDI_codes[i], button_size);
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s - %x", icon_MDI_names[i], icon_MDI_unicodes[i]);
            {
                ImGui::PushFont(icon_font, font_size);
                ImGui::TextUnformatted(icon_MDI_codes[i]);
                ImGui::PopFont();
            }
            ImGui::EndTooltip();
        }
        ++x;
        if (x < 20) {
            ImGui::SameLine();
        } else {
            x = 0;
        }
    }
    ImGui::NewLine();
}

}
