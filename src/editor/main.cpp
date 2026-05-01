#include "editor.hpp"

#include <SDL3/SDL_main.h>

#if defined(ERHE_OS_ANDROID)
#   include <SDL3/SDL_system.h>
#   include <unistd.h>
#endif

auto main(int, char**) -> int
{
#if defined(ERHE_OS_ANDROID)
    // Android starts the process at cwd "/", which is read-only and outside
    // the app sandbox. Move to internal storage so relative writes
    // (spirv_cache/, generated configs) land under /data/data/<pkg>/files/.
    // Asset reads still go through SDL_IOFromFile -> AAssetManager.
    if (const char* base = SDL_GetAndroidInternalStoragePath()) {
        ::chdir(base);
    }
#endif
    editor::run_editor();
    return 0;
}
