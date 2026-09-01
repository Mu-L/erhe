#include "erhe_scene_renderer/material_set.hpp"

#include "erhe_primitive/material.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene_renderer {

Material_set::Material_set() = default;

Material_set::~Material_set() noexcept = default;

auto Material_set::allocate_slot(const std::shared_ptr<erhe::primitive::Material>& material) -> uint32_t
{
    ERHE_VERIFY(material);
    const auto i = m_index_by_material.find(material.get());
    if (i != m_index_by_material.end()) {
        return i->second;
    }

    uint32_t index = 0;
    if (!m_free_slots.empty()) {
        index = m_free_slots.back();
        m_free_slots.pop_back();
    } else {
        index = static_cast<uint32_t>(m_materials.size());
        m_materials.emplace_back();
    }

    Material_slot& slot = m_materials[index];
    // The generation was bumped when the slot was freed, so a Material_slot_id
    // issued for the previous occupant no longer validates.
    slot.material   = material;
    slot.in_library = false;
    slot.use_count  = 0;
    slot.alive      = true;
    m_index_by_material.emplace(material.get(), index);
    m_membership_dirty = true;
    return index;
}

void Material_set::release_slot_if_unreferenced(const uint32_t index)
{
    ERHE_VERIFY(index < m_materials.size());
    Material_slot& slot = m_materials[index];
    if (!slot.alive || slot.in_library || (slot.use_count > 0)) {
        return;
    }
    m_index_by_material.erase(slot.material.get());
    slot.material.reset();
    slot.alive = false;
    ++slot.generation;
    m_free_slots.push_back(index);
    m_membership_dirty = true;
}

void Material_set::sync_library(const std::span<const std::shared_ptr<erhe::primitive::Material>> materials)
{
    // Reconciled by difference, not by rebuild: the slot a material already
    // holds must survive a library update that did not mention it, or every
    // cached record naming that slot would have to be rewritten.
    for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
        if (!material) {
            continue;
        }
        const uint32_t index = allocate_slot(material);
        m_materials[index].in_library = true;
    }

    for (std::size_t i = 0, end = m_materials.size(); i < end; ++i) {
        Material_slot& slot = m_materials[i];
        if (!slot.alive || !slot.in_library) {
            continue;
        }
        const erhe::primitive::Material* material = slot.material.get();
        const bool still_in_library = std::any_of(
            materials.begin(),
            materials.end(),
            [material](const std::shared_ptr<erhe::primitive::Material>& entry) { return entry.get() == material; }
        );
        if (!still_in_library) {
            slot.in_library = false;
            release_slot_if_unreferenced(static_cast<uint32_t>(i));
        }
    }
}

auto Material_set::add_ref(const std::shared_ptr<erhe::primitive::Material>& material) -> Material_slot_id
{
    if (!material) {
        return Material_slot_id{};
    }
    const uint32_t index = allocate_slot(material);
    Material_slot& slot = m_materials[index];
    ++slot.use_count;
    return Material_slot_id{.index = index, .generation = slot.generation};
}

void Material_set::release(const erhe::primitive::Material* material)
{
    if (material == nullptr) {
        return;
    }
    const auto i = m_index_by_material.find(material);
    if (i == m_index_by_material.end()) {
        return;
    }
    const uint32_t index = i->second;
    Material_slot& slot = m_materials[index];
    ERHE_VERIFY(slot.use_count > 0);
    --slot.use_count;
    release_slot_if_unreferenced(index);
}

void Material_set::sync_object_materials(
    const uint64_t                                                    object_key,
    const std::span<const std::shared_ptr<erhe::primitive::Material>> materials
)
{
    // Apply the DIFFERENCE, so a material shared between the old and the new
    // list keeps a positive count throughout and never loses its slot in
    // between - records naming it stay valid across the reassignment.
    std::vector<std::shared_ptr<erhe::primitive::Material>> next;
    for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
        if (!material) {
            continue;
        }
        const bool already_listed = std::any_of(
            next.begin(),
            next.end(),
            [&material](const std::shared_ptr<erhe::primitive::Material>& entry) { return entry == material; }
        );
        if (!already_listed) {
            next.push_back(material);
        }
    }

    std::vector<std::shared_ptr<erhe::primitive::Material>>& previous = m_object_materials[object_key];
    for (const std::shared_ptr<erhe::primitive::Material>& material : next) {
        const bool held = std::any_of(
            previous.begin(),
            previous.end(),
            [&material](const std::shared_ptr<erhe::primitive::Material>& entry) { return entry == material; }
        );
        if (!held) {
            static_cast<void>(add_ref(material));
        }
    }
    for (const std::shared_ptr<erhe::primitive::Material>& material : previous) {
        const bool kept = std::any_of(
            next.begin(),
            next.end(),
            [&material](const std::shared_ptr<erhe::primitive::Material>& entry) { return entry == material; }
        );
        if (!kept) {
            release(material.get());
        }
    }
    previous = std::move(next);
}

void Material_set::release_object_materials(const uint64_t object_key)
{
    const auto i = m_object_materials.find(object_key);
    if (i == m_object_materials.end()) {
        return;
    }
    // Move the list out first: release() can drop the last reference and
    // destroy the Material, and the map entry being iterated is what holds it.
    const std::vector<std::shared_ptr<erhe::primitive::Material>> materials = std::move(i->second);
    m_object_materials.erase(i);
    for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
        release(material.get());
    }
}

void Material_set::enqueue_object_materials(
    const uint64_t                                                    object_key,
    const std::span<const std::shared_ptr<erhe::primitive::Material>> materials
)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(
        Pending_op{
            .object_key = object_key,
            .materials  = std::vector<std::shared_ptr<erhe::primitive::Material>>{materials.begin(), materials.end()},
            .release    = false
        }
    );
}

void Material_set::enqueue_release_object(const uint64_t object_key)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.object_key = object_key, .materials = {}, .release = true});
}

void Material_set::flush_pending()
{
    std::vector<Pending_op> pending;
    {
        const std::lock_guard<std::mutex> lock{m_pending_mutex};
        pending.swap(m_pending);
    }
    // In enqueue order: a register / unregister pair for one object means
    // different things in either order.
    for (const Pending_op& op : pending) {
        if (op.release) {
            release_object_materials(op.object_key);
        } else {
            sync_object_materials(op.object_key, op.materials);
        }
    }
}

auto Material_set::get_pending_count() const -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    return m_pending.size();
}

auto Material_set::find(const erhe::primitive::Material* material) const -> Material_slot_id
{
    if (material == nullptr) {
        return Material_slot_id{};
    }
    const auto i = m_index_by_material.find(material);
    if (i == m_index_by_material.end()) {
        return Material_slot_id{};
    }
    return Material_slot_id{.index = i->second, .generation = m_materials[i->second].generation};
}

auto Material_set::get_slot(const erhe::primitive::Material* material) const -> std::optional<uint32_t>
{
    const Material_slot_id id = find(material);
    if (!id.is_valid()) {
        return {};
    }
    return id.index;
}

auto Material_set::is_valid(const Material_slot_id& id) const -> bool
{
    if (!id.is_valid() || (id.index >= m_materials.size())) {
        return false;
    }
    const Material_slot& slot = m_materials[id.index];
    return slot.alive && (slot.generation == id.generation);
}

auto Material_set::get_slot_count() const -> std::size_t
{
    for (std::size_t i = m_materials.size(); i > 0; --i) {
        if (m_materials[i - 1].alive) {
            return i;
        }
    }
    return 0;
}

auto Material_set::get_live_count() const -> std::size_t
{
    return m_index_by_material.size();
}

auto Material_set::get_materials() const -> std::span<const Material_slot>
{
    return std::span<const Material_slot>{m_materials};
}

auto Material_set::is_membership_dirty() const -> bool
{
    return m_membership_dirty;
}

void Material_set::clear_membership_dirty()
{
    m_membership_dirty = false;
}

} // namespace erhe::scene_renderer
