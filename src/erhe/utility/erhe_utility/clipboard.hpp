#pragma once

#include <string_view>

namespace erhe::utility {

// Copy diagnostic text to the system clipboard for inspection by the
// developer. On desktop platforms this calls SDL_SetClipboardText. On
// Android the message is emitted to logcat under tag "erhe.clipboard"
// instead, because app processes have no general-purpose system
// clipboard SDL can write to and the diagnostic dumps callers pass here
// can exceed the binder parcel size limit.
void copy_to_clipboard(std::string_view text);

} // namespace erhe::utility
