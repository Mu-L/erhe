#pragma once

namespace erhe::window {

class Context_window;

void initialize_frame_capture();
[[nodiscard]] auto get_renderdoc_api() -> void*;

} // namespace erhe::window
