#pragma once

#include "erhe/components/component.hpp"
#include "erhe/components/components.hpp"
#include "erhe/toolkit/window.hpp"

#include <renderdoc_app.h>

namespace editor {

class Configuration;

class Window
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Window"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Window();

    // Implements Component
    auto get_type_hash() const -> uint32_t override { return hash; }

    auto create_gl_window() -> bool;

    auto get_context_window() const -> erhe::toolkit::Context_window*;

    void begin_renderdoc_capture();
    void end_renderdoc_capture  ();

private:

    std::unique_ptr<erhe::toolkit::Context_window> m_context_window;
    RENDERDOC_API_1_1_2*                           m_renderdoc_api;
};

}