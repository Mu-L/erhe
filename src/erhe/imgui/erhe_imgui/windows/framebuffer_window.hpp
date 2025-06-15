#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/pipeline.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_math/viewport.hpp"

#include <memory>

namespace erhe::graphics {
    class Device;
    class Texture;
}

namespace erhe::imgui {

class Imgui_windows;

class Framebuffer_window : public Imgui_window
{
public:
    Framebuffer_window(
        erhe::graphics::Device& graphics_device,
        Imgui_renderer&         imgui_renderer,
        Imgui_windows&          imgui_windows,
        const std::string_view  title,
        const char*             ini_label
    );

    // Implements Imgui_window
    void imgui() override;

    // Implements Framebuffer window
    virtual auto get_size(glm::vec2 available_size) const -> glm::vec2;

    [[nodiscard]] auto to_content(const glm::vec2 position_in_root) const -> glm::vec2;

    // Public API
    virtual void update_framebuffer();

    void bind_framebuffer();

protected:
    erhe::graphics::Device&                      m_graphics_device;
    std::string                                  m_debug_label;
    erhe::math::Viewport                         m_viewport           {0, 0, 0, 0, true};
    float                                        m_content_rect_x     {0.0f};
    float                                        m_content_rect_y     {0.0f};
    float                                        m_content_rect_width {0.0f};
    float                                        m_content_rect_height{0.0f};
    erhe::dataformat::Vertex_format              m_empty_vertex_format;
    erhe::graphics::Vertex_input_state           m_vertex_input;
    std::shared_ptr<erhe::graphics::Texture>     m_texture;
    std::unique_ptr<erhe::graphics::Framebuffer> m_framebuffer;
};

} // namespace erhe::imgui
