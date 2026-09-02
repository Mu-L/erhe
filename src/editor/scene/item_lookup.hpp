#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

namespace erhe { class Item_base; }

namespace editor {

class Scene_root;

// Any item of a scene by unique id or by name: nodes, their attachments
// (meshes, lights, cameras, ...) and the content library's materials. Name
// lookup returns the first match in that order.
[[nodiscard]] auto find_item_in_scene_by_id  (Scene_root& scene_root, std::size_t id)        -> std::shared_ptr<erhe::Item_base>;
[[nodiscard]] auto find_item_in_scene_by_name(Scene_root& scene_root, std::string_view name) -> std::shared_ptr<erhe::Item_base>;

}
