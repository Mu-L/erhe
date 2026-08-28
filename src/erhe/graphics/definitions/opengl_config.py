from erhe_codegen import *

struct("Opengl_config",
    reflect=True,
    version=2,
    short_desc="OpenGL-specific Graphics Settings",
    long_desc="Debug overrides for the OpenGL backend.",
    developer=False,
    fields=[
        field(
            "force_bindless_textures_off",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable OpenGL Bindless Textures",
            long_desc="Prevent any use of OpenGL Bindless Textures. Always used when RenderDoc capture is enabled.",
            visible=True,
            developer=False
        ),
        field(
            "force_no_persistent_buffers",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable OpenGL Persistent Buffers",
            long_desc="Prevent any use of OpenGL presistent buffer. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "force_emulate_multi_draw_indirect",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable OpenGL Multi Draw Indirect",
            long_desc="Prevent any use of OpenGL MDI (Multi Draw Indirect). Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        # The pre-GL-4.5 emulation layer was removed in v2: OpenGL 4.5 (DSA,
        # clip control, compute shaders, SSBOs) is now a hard device-creation
        # requirement, so the switches that requested the emulated paths are
        # retired.
        field("force_no_direct_state_access", Bool, added_in=1, removed_in=2, default="false", short_desc="Force Disable OpenGL Direct State Access", long_desc="Removed in v2."),
        field("force_no_clip_control",        Bool, added_in=1, removed_in=2, default="false", short_desc="Force Disable OpenGL Clip Control",        long_desc="Removed in v2."),
        field("force_no_compute_shader",      Bool, added_in=1, removed_in=2, default="false", short_desc="Force Disable Compute Shaders",            long_desc="Removed in v2."),
        field("force_gl_version",             Int,  added_in=1, removed_in=2, default="0",     short_desc="Force OpenGL Version",                     long_desc="Removed in v2."),
        field("force_glsl_version",           Int,  added_in=1, removed_in=2, default="0",     short_desc="Force OpenGL GLSL Version",                long_desc="Removed in v2."),
    ],
)
