#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <string_view>

namespace erhe::graphics {
    class Blend_state_component;
    class Color_blend_state;
    class Depth_stencil_state;
    class Pipeline;
    class Rasterization_state;
    class Stencil_op_state;
}

namespace erhe::imgui {

class Imgui_windows;

class Pipelines : public Imgui_window
{
public:
    Pipelines(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows);

    // Implements Imgui_window
    void imgui() override;

    static void rasterization(erhe::graphics::Rasterization_state& rasterization);
    static void stencil_op(const char* label, erhe::graphics::Stencil_op_state& stencil_op);
    static void depth_stencil(erhe::graphics::Depth_stencil_state& depth_stencil);
    static void blend_state_component(const char* label, erhe::graphics::Blend_state_component& component);
    static void color_blend(erhe::graphics::Color_blend_state& color_blend);
};

void pipeline_imgui(erhe::graphics::Pipeline& pipeline);

} // namespace erhe::imgui
