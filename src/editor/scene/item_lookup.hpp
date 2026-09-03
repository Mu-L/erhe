#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe { class Item_base; }

namespace editor {

class App_context;
class Scene_root;

// Any item of a scene by unique id or by name: nodes, their attachments
// (meshes, lights, cameras, ...) and the content library's materials. Name
// lookup returns the first match in that order.
[[nodiscard]] auto find_item_in_scene_by_id  (Scene_root& scene_root, std::size_t id)        -> std::shared_ptr<erhe::Item_base>;
[[nodiscard]] auto find_item_in_scene_by_name(Scene_root& scene_root, std::string_view name) -> std::shared_ptr<erhe::Item_base>;

// The scene an item belongs to: the registered scene that is its Item_host,
// or, for an asset-typed item (materials never claim a host), the scene
// whose container defines it. nullptr when neither applies (a preview or
// tool scene item, an unregistered asset).
[[nodiscard]] auto find_scene_root_for_item(App_context& context, const erhe::Item_base& item) -> Scene_root*;

// Object reference candidates (doc/property-system.md D28): the content
// library items of the target's scene whose type bit is in item_types and
// that are shown in the UI (developer-only items in developer mode). Clears
// `out` first; the caller clears it again after the draw so the strong
// references do not outlive the frame.
void collect_reference_candidates(
    App_context&                                   context,
    const erhe::Item_base&                         target,
    uint64_t                                       item_types,
    std::vector<std::shared_ptr<erhe::Item_base>>& out
);

}
