#!/bin/bash

# Source Vulkan SDK environment if available
for sdk_dir in ~/VulkanSDK/*/; do
    if [ -f "${sdk_dir}setup-env.sh" ]; then
        source "${sdk_dir}setup-env.sh"
        break
    fi
done

cmake \
    -G "Xcode" \
    -B build_xcode_vulkan_asan \
    -S . \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
    -Wno-dev \
    -DERHE_USE_PRECOMPILED_HEADERS=ON \
    -DERHE_FONT_RASTERIZATION_LIBRARY=none \
    -DERHE_GLTF_LIBRARY=fastgltf \
    -DERHE_GUI_LIBRARY=imgui \
    -DERHE_GRAPHICS_LIBRARY=vulkan \
    -DERHE_PHYSICS_LIBRARY=none \
    -DERHE_PROFILE_LIBRARY=none \
    -DERHE_RAYTRACE_LIBRARY=none \
    -DERHE_SVG_LIBRARY=none \
    -DERHE_TEXT_LAYOUT_LIBRARY=none \
    -DERHE_WINDOW_LIBRARY=sdl \
    -DERHE_XR_LIBRARY=none \
    -DERHE_USE_ASAN:BOOL=ON \
    -DERHE_BUILD_TESTS=ON
