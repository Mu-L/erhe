#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace erhe::graphics { class Device; }
namespace erhe::renderer { class Text_renderer; }
namespace erhe::window   { class Context_window; }

namespace editor {

class Init_status_display
{
public:
    Init_status_display(
        erhe::graphics::Device&        graphics_device,
        erhe::window::Context_window&  window,
        erhe::renderer::Text_renderer& text_renderer,
        bool                           enabled
    );
    ~Init_status_display() noexcept;

    Init_status_display (const Init_status_display&) = delete;
    void operator=      (const Init_status_display&) = delete;
    Init_status_display (Init_status_display&&)      = delete;
    void operator=      (Init_status_display&&)      = delete;

    void set_line(std::size_t line_index, std::string_view text);
    void render_present();

private:
    static constexpr std::chrono::milliseconds kMinPresentInterval{16};


    erhe::graphics::Device&        m_graphics_device;
    erhe::window::Context_window&  m_window;
    erhe::renderer::Text_renderer& m_text_renderer;
    bool                           m_enabled{false};
    std::vector<std::string>       m_lines;
};

} // namespace editor
