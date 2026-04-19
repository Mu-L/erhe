# kk: vulkan macOS stencil test getting ignored 

On macOS running the Vulkan backend through KosmicKrisp, my rendering test
produces a visibly wrong result for a specific test sequence. The test renders
correctly on macOS Metal backend, on OpenGL backend on non-macOS systems, and
on Vulkan backend (without validation layer issues) on vulkan systems other than
macaOS. So issue has only reproduced on macOS/Vulkan.

## Reproduction

```sh
git clone https://github.com/tksuoran/erhe.git
cd erhe
scripts/configure_xcode_vulkan_minimal.sh
cmake --build build_xcode_vulkan_minimal --target rendering_test --config Debug
build_xcode_vulkan_minimal/src/rendering_test/Debug/rendering_test \
    config/rendering_test_wel_then_stencil_wl.json
```

## Test setup

The test renders two scenes, to left and right halves of the window.
The test involves compute shader producing content consumed by later draw calls and stencil
test.
Rendering steps:
1. A compute shader is run for left half and then for right half, producing edge lines for a cube
   for left half, and edge lines for a sphere for the right half.
2. The left half renders cube, basic rendering pipeline.
3. The left half renders wide "edge" lines for the cube, consuming output of the compute shader
4. The right half renders cube, rendering pipeline sets stencil value unconditional to 1.
5. The right half render wide "edge" lines for a sphere, consuming output of the compute shader,
   using render pipeline with stencil test set to not equal one

## Expected output (OpenGL, metal, vulkan on platforms other than macOS)

- Left half: cube, shaded with normal direction as color, with dark blue outline edge lines
- Right half: gray cube on front, yellow wide edge lines for sphere, not visible on top of the
  gray cube due to stencil test.

## Unexpected actual current output (vulkan on macOS/KK)

- Left half: cube, shaded with normal direction as color, with dark blue outline edge lines (expected)
- Right half: gray cube intersecting yellow wide edge lines for sphere, intersection is determined by depth
  test only, as if stencil test was ignored.
