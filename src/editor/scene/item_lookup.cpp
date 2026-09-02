#include "scene/item_lookup.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/scene.hpp"

namespace editor {

namespace {

template <typename Predicate>
auto find_item_in_scene(Scene_root& scene_root, Predicate&& matches) -> std::shared_ptr<erhe::Item_base>
{
    std::shared_ptr<erhe::Item_base> result;
    const erhe::scene::Scene& scene = scene_root.get_scene();

    const std::shared_ptr<erhe::scene::Scene> scene_item = scene_root.get_scene_item();
    if (scene_item && matches(*scene_item)) {
        return scene_item;
    }

    // The root node is not registered in the transform-update buckets that
    // for_each_node visits.
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    if (root_node && matches(*root_node)) {
        return root_node;
    }

    scene.for_each_node(
        [&](const std::shared_ptr<erhe::scene::Node>& node) {
            if (matches(*node)) {
                result = node;
                return false;
            }
            for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
                if (attachment && matches(*attachment)) {
                    result = attachment;
                    return false;
                }
            }
            return true;
        }
    );
    if (result) {
        return result;
    }

    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (library && library->materials) {
        for (const std::shared_ptr<erhe::primitive::Material>& material : library->materials->get_all<erhe::primitive::Material>()) {
            if (material && matches(*material)) {
                return material;
            }
        }
    }
    return {};
}

} // anonymous namespace

auto find_item_in_scene_by_id(Scene_root& scene_root, const std::size_t id) -> std::shared_ptr<erhe::Item_base>
{
    return find_item_in_scene(scene_root, [id](const erhe::Item_base& item) { return item.get_id() == id; });
}

auto find_item_in_scene_by_name(Scene_root& scene_root, const std::string_view name) -> std::shared_ptr<erhe::Item_base>
{
    return find_item_in_scene(scene_root, [name](const erhe::Item_base& item) { return item.get_name() == name; });
}

}
