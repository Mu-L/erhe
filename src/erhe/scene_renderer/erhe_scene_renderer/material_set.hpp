#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace erhe::primitive {
    class Material;
}

namespace erhe::scene_renderer {

// Handle to a material registered in one Material_set. The index is stable
// (free-listed) and IS the GPU slot the shader indexes with; the generation
// detects use of a handle whose slot has since been freed and reused.
class Material_slot_id
{
public:
    uint32_t index     {invalid_index};
    uint32_t generation{0};

    static constexpr uint32_t invalid_index = 0xffffffffu;

    [[nodiscard]] auto is_valid() const -> bool { return index != invalid_index; }
    [[nodiscard]] auto operator==(const Material_slot_id&) const -> bool = default;
};

// One registered material. Owns the material for as long as it is registered,
// so a cached record naming this slot keeps meaning the same material until
// the record is rewritten or dropped.
class Material_slot
{
public:
    std::shared_ptr<erhe::primitive::Material> material{};

    // The two reference sources, kept apart because they reconcile
    // differently. The content library is regenerated whole on every update
    // and so reconciles by diff; object references arrive and depart with
    // object lifetimes and so are counted. Counting a regenerated source would
    // leak - every material ever previewed would hold a slot and a strong
    // reference for the session.
    bool     in_library {false};
    uint32_t use_count  {0};

    uint32_t generation {0};
    bool     alive      {false};
};

// Which materials are members of one slot space, and at which slots.
//
// Membership is by reference: a material belongs while anything references it
// IN THIS SET, rather than because a caller passed it in a list. A Material may
// be a member of any number of sets at once; its slot is a property of the set,
// not of the material, and two sets holding the same Material are wholly
// independent - which is what keeps one scene's or one render path's slot
// numbering out of another's.
//
// Every mutator is non-const and every lookup is const, so a record writer can
// be handed a `const Material_set*` and then cannot do anything but look slots
// up: slots are assigned ahead of every record write, never by one.
//
// Phase 3 of doc/draw_list_material_set_plan.md adds the GPU half to this class
// - the material buffer, the texture heap and update / bind / unbind - written
// from the slot table below. Nothing outside the tests uses this type yet.
class Material_set final
{
public:
    Material_set();
    ~Material_set() noexcept;
    Material_set   (const Material_set&) = delete;
    void operator= (const Material_set&) = delete;
    Material_set   (Material_set&&)      = delete;
    void operator= (Material_set&&)      = delete;

    // Library membership, once per update. Materials that left the library
    // lose their library reference; their slot survives while object
    // references remain. Main thread only.
    void sync_library(std::span<const std::shared_ptr<erhe::primitive::Material>> materials);

    // Object membership, refcounted. Main thread only.
    auto add_ref(const std::shared_ptr<erhe::primitive::Material>& material) -> Material_slot_id;
    void release(const erhe::primitive::Material* material);

    // Deferral for callers that can run on a worker thread (mesh registration
    // during async glTF load): enqueue off-thread, apply on the main thread.
    // The object key identifies the referencing object; a second
    // enqueue_object_materials() for the same key replaces its list, and the
    // difference against the previous one is what is applied, so a material
    // shared between the old and the new list keeps a positive count
    // throughout.
    void enqueue_object_materials(uint64_t object_key, std::span<const std::shared_ptr<erhe::primitive::Material>> materials);
    void enqueue_release_object  (uint64_t object_key);
    // Applies every enqueued operation, in the order they were enqueued. Main
    // thread only.
    void flush_pending();
    [[nodiscard]] auto get_pending_count() const -> std::size_t;

    // Object membership applied directly, for a caller that is already on the
    // main thread inside its own flush. Same diff rule as the enqueued form.
    void sync_object_materials   (uint64_t object_key, std::span<const std::shared_ptr<erhe::primitive::Material>> materials);
    void release_object_materials(uint64_t object_key);

    // Lookups. Const, and none of them assigns a slot.
    [[nodiscard]] auto find          (const erhe::primitive::Material* material) const -> Material_slot_id;
    [[nodiscard]] auto get_slot      (const erhe::primitive::Material* material) const -> std::optional<uint32_t>;
    [[nodiscard]] auto is_valid      (const Material_slot_id& id) const -> bool;
    // Highest live slot + 1, i.e. how many slots the GPU write covers. Holes
    // inside it are zero-filled.
    [[nodiscard]] auto get_slot_count() const -> std::size_t;
    // How many slots are actually live, holes excluded.
    [[nodiscard]] auto get_live_count() const -> std::size_t;
    // Slot-indexed; Material_slot::alive marks the live entries.
    [[nodiscard]] auto get_materials () const -> std::span<const Material_slot>;

    // True when a slot was assigned or freed since the dirty edge was last
    // cleared. Exact: a sync_library() that changes nothing, a repeated
    // add_ref() of an already-referenced material and a release() that leaves
    // the count above zero all leave it alone. The GPU update consumes this
    // edge (phase 3); it is public so a test can too.
    [[nodiscard]] auto is_membership_dirty   () const -> bool;
    void               clear_membership_dirty();

private:
    class Pending_op
    {
    public:
        uint64_t                                                object_key{0};
        std::vector<std::shared_ptr<erhe::primitive::Material>>  materials {};
        bool                                                     release   {false};
    };

    [[nodiscard]] auto allocate_slot(const std::shared_ptr<erhe::primitive::Material>& material) -> uint32_t;
    // Frees the slot when neither reference source holds it any more.
    void release_slot_if_unreferenced(uint32_t index);

    std::vector<Material_slot>                                     m_materials;   // holes: alive == false
    std::vector<uint32_t>                                          m_free_slots;
    std::unordered_map<const erhe::primitive::Material*, uint32_t> m_index_by_material;
    // The material list each registered object last contributed, keyed by the
    // object key. What a new list is diffed against.
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<erhe::primitive::Material>>> m_object_materials;

    mutable std::mutex                                             m_pending_mutex;
    std::vector<Pending_op>                                        m_pending;

    bool                                                           m_membership_dirty{true};
};

} // namespace erhe::scene_renderer
