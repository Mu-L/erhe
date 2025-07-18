#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <memory>

namespace erhe::imgui { class Imgui_windows; }

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

class App_context;

class Rendergraph_window : public erhe::imgui::Imgui_window
{
public:
    Rendergraph_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context);
    ~Rendergraph_window() noexcept override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

private:
    App_context& m_context;
    float           m_image_size{100.0f};
    float           m_curve_strength{10.0f};
    std::unique_ptr<ax::NodeEditor::EditorContext> m_node_editor;
};

}
