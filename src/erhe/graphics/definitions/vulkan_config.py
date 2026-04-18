from erhe_codegen import *

struct("Vulkan_config",
    version=1,
    short_desc="Vulkan-specific Graphics Settings",
    long_desc="Debug overrides for the Vulkan backend.",
    developer=False,
    fields=[
        field(
            "vulkan_validation_layers",
            Bool,
            added_in=1,
            default="true",
            short_desc="Enable Vulkan Validation Layers",
            long_desc="Enables Vulkan validation layers (VK_LAYER_KHRONOS_validation). Only meaningful for Vulkan backend.",
            visible=True,
            developer=False
        ),
    ],
)
