from erhe_codegen import *

struct("Graphics_config",
    version=2,
    short_desc="Graphics Settings",
    long_desc="",
    developer=False,
    fields=[
        field(
            "initial_clear",
            Bool,
            added_in=1,
            default="true",
            short_desc="Initial Clear",
            long_desc="Submit few empty frames during Startup",
            visible=True,
            developer=False
        ),
        field(
            "renderdoc_capture_support",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable RenderDoc Capture Support",
            long_desc="Enables RenderDoc Capture Support. Disables use of OpenGL Bindless Textures. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "shader_monitor_enabled",
            Bool,
            added_in=1,
            default="true",
            short_desc="Enable Shader Monitor",
            long_desc="Enables Shader Monitor, allowing Shader Hot-reloading. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "force_disable_vsync",
            Bool,
            added_in=2,
            default="false",
            short_desc="Force Disable VSync",
            long_desc="Forces immediate presentation (no vsync) when supported. On Vulkan this maps to VK_PRESENT_MODE_IMMEDIATE_KHR. Intended for accurate performance measurements.",
            visible=True,
            developer=False
        ),
        field("opengl", StructRef("Opengl_config"), added_in=2),
        field("vulkan", StructRef("Vulkan_config"), added_in=2),
    ],
)
