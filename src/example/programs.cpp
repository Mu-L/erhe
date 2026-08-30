#include "programs.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

namespace example {

Programs::Programs(
    erhe::graphics::Device&                     graphics_device,
    erhe::scene_renderer::Shader_variant_cache& shader_variant_cache
)
    : shader_variant_cache{shader_variant_cache}
{
    static_cast<void>(graphics_device);
}

}
