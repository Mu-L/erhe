#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/scene.hpp"

namespace erhe::scene {

Scene_host::~Scene_host() noexcept = default;

auto Scene_host::find_hosted_item(const std::string_view name) -> erhe::Item_base*
{
    Scene* const scene = get_hosted_scene();
    if (scene == nullptr) {
        return nullptr;
    }
    // The root node is not in the buckets for_each_node visits.
    const std::shared_ptr<Node> root_node = scene->get_root_node();
    if (root_node && (root_node->get_name() == name)) {
        return root_node.get();
    }
    erhe::Item_base* result = nullptr;
    scene->for_each_node(
        [&](const std::shared_ptr<Node>& node) {
            if (node->get_name() == name) {
                result = node.get();
                return false;
            }
            for (const std::shared_ptr<Node_attachment>& attachment : node->get_attachments()) {
                if (attachment && (attachment->get_name() == name)) {
                    result = attachment.get();
                    return false;
                }
            }
            return true;
        }
    );
    return result;
}

} // namespace erhe::scene
