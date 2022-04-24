#pragma once

#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"

namespace erhe::application {

class Configuration
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Configuration"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Configuration(int argc, char** argv);

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }

    // Public API
    [[nodiscard]] auto depth_clear_value_pointer() const -> const float *; // reverse_depth ? 0.0f : 1.0f;
    [[nodiscard]] auto depth_function           (const gl::Depth_function depth_function) const -> gl::Depth_function;

    bool viewports_hosted_in_imgui_windows;
    bool openxr;
    bool show_window;
    bool parallel_initialization;
    bool reverse_depth;
    bool fullscreen;
    int  window_width;
    int  window_height;
    int  window_msaa_sample_count;
};

} // namespace erhe::application
