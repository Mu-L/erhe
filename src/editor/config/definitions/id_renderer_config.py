from erhe_codegen import *

struct("Id_renderer_config",
    reflect=True,
    version=2,
    short_desc="ID Renderer",
    long_desc="",
    developer=True,
    fields=[
        field(
            "enabled",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable ID Renderer",
            long_desc="",
            visible=True,
            developer=False
        ),
        # The CPU readback + dedup fallback was removed in v2: compute shaders
        # are a hard device requirement, box / paint select always uses the
        # GPU compute scan, and the A/B toggle is retired.
        field("box_select_use_compute", Bool, added_in=1, removed_in=2, default="true", short_desc="Box/Paint Select Uses GPU Compute", long_desc="Removed in v2."),
    ],
)
