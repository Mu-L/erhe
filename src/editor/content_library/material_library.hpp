#pragma once

#include <memory>

namespace erhe::property { class Property_style; }

namespace editor {

class Content_library;

// The style the default metals share (doc/property-system-plan.md D25):
// roughness, metallic, BxDF model and the brushed metal flags.
[[nodiscard]] auto make_brushed_metal_style() -> std::shared_ptr<const erhe::property::Property_style>;

void add_default_materials        (Content_library& library);
void add_default_physics_materials(Content_library& library);

}
