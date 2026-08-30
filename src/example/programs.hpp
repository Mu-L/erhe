#pragma once

#include <filesystem>

namespace erhe::graphics{
    class Device;
}

namespace erhe::scene_renderer{
    class Program_interface;
    class Shader_variant_cache;
}

namespace example {

class Programs
{
public:
    Programs(
        erhe::graphics::Device&                     graphics_device,
        erhe::scene_renderer::Shader_variant_cache& shader_variant_cache
    );

    erhe::scene_renderer::Shader_variant_cache& shader_variant_cache;
};

}
