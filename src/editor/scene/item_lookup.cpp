#include "scene/item_lookup.hpp"
#include "app_context.hpp"
#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "content_library/content_library.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "scene/scene_root.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph_node.hpp"

#include "erhe_graphics/texture.hpp"
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
    // Textures: the targets of a material's texture slot properties (D28).
    if (library && library->textures) {
        for (const std::shared_ptr<erhe::graphics::Texture>& texture : library->textures->get_all<erhe::graphics::Texture>()) {
            if (texture && matches(*texture)) {
                return texture;
            }
        }
    }
    // Graph assets and their nodes: the nodes share the asset's host
    // (Graph_asset::set_item_host), so a D22 expression or an MCP property
    // call reaches a graph node the way it reaches a scene item.
    if (library && library->graph_meshes) {
        for (const std::shared_ptr<Graph_mesh>& graph_mesh : library->graph_meshes->get_all<Graph_mesh>()) {
            if (!graph_mesh) {
                continue;
            }
            if (matches(*graph_mesh)) {
                return graph_mesh;
            }
            for (const std::shared_ptr<Geometry_graph_node>& node : graph_mesh->nodes()) {
                if (node && matches(*node)) {
                    return node;
                }
            }
        }
    }
    if (library && library->graph_textures) {
        for (const std::shared_ptr<Graph_texture>& graph_texture : library->graph_textures->get_all<Graph_texture>()) {
            if (!graph_texture) {
                continue;
            }
            if (matches(*graph_texture)) {
                return graph_texture;
            }
            for (const std::shared_ptr<Texture_graph_node>& node : graph_texture->nodes()) {
                if (node && matches(*node)) {
                    return node;
                }
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

auto resolve_reference_by_name(App_context& context, const erhe::Item_base& from, const std::string_view name) -> std::shared_ptr<erhe::Item_base>
{
    Scene_root* const scene_root = find_scene_root_for_item(context, from);
    if (scene_root == nullptr) {
        return {};
    }
    return find_item_in_scene_by_name(*scene_root, name);
}

auto find_scene_root_for_item(App_context& context, const erhe::Item_base& item) -> Scene_root*
{
    if (context.app_scenes == nullptr) {
        return nullptr;
    }
    const erhe::Item_host* const host = item.get_item_host();
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        if (!scene_root) {
            continue;
        }
        const erhe::Item_host* const candidate = scene_root.get();
        if (host != nullptr) {
            if (host == candidate) {
                return scene_root.get();
            }
            continue;
        }
        // An unhosted item (an asset-typed one): the scene whose content
        // library lists it, else the scene the manager records as defining it.
        const std::shared_ptr<Content_library>& library = scene_root->get_content_library();
        if (library && library->root && library->root->has_item(item)) {
            return scene_root.get();
        }
        if ((context.asset_manager != nullptr) && context.asset_manager->is_defined_by(item, candidate)) {
            return scene_root.get();
        }
    }
    return nullptr;
}

void collect_reference_candidates(
    App_context&                                   context,
    const erhe::Item_base&                         target,
    const uint64_t                                 item_types,
    std::vector<std::shared_ptr<erhe::Item_base>>& out
)
{
    out.clear();
    Scene_root* const scene_root = find_scene_root_for_item(context, target);
    if (scene_root == nullptr) {
        return;
    }
    const std::shared_ptr<Content_library>& library = scene_root->get_content_library();
    if (!library || !library->root) {
        return;
    }
    const bool developer_mode = context.developer_mode;
    library->root->for_each_const<Content_library_node>(
        [&out, item_types, developer_mode](const Content_library_node& node) -> bool {
            const std::shared_ptr<erhe::Item_base>& item = node.item;
            if (!item || ((item->get_type() & item_types) == 0)) {
                return true; // continue
            }
            const bool shown =
                item->is_shown_in_ui() ||
                (developer_mode && ((item->get_flag_bits() & erhe::Item_flags::show_in_developer_ui) != 0));
            if (shown) {
                out.push_back(item);
            }
            return true;
        }
    );
}

}
