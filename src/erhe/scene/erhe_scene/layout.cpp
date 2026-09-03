#include "erhe_scene/layout.hpp"
#include "erhe_scene/layout_item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_primitive/primitive.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace erhe::scene {

auto axis_index(const Axis_direction direction) -> int
{
    switch (direction) {
        case Axis_direction::pos_x:
        case Axis_direction::neg_x: return 0;
        case Axis_direction::pos_y:
        case Axis_direction::neg_y: return 1;
        case Axis_direction::pos_z:
        case Axis_direction::neg_z: return 2;
        default:                    return 0;
    }
}

auto axis_sign(const Axis_direction direction) -> float
{
    switch (direction) {
        case Axis_direction::pos_x:
        case Axis_direction::pos_y:
        case Axis_direction::pos_z: return  1.0f;
        case Axis_direction::neg_x:
        case Axis_direction::neg_y:
        case Axis_direction::neg_z: return -1.0f;
        default:                    return  1.0f;
    }
}

auto axis_vector(const Axis_direction direction) -> glm::vec3
{
    glm::vec3 result{0.0f, 0.0f, 0.0f};
    result[axis_index(direction)] = axis_sign(direction);
    return result;
}

namespace {

// An Aabb is empty (carries no extent) when its minimum exceeds its maximum on
// any axis. The default-constructed Aabb (min = FLT_MAX, max = -FLT_MAX) is empty.
// Note: erhe::math::Aabb::is_valid() uses '||' and is unreliable here, so this
// feature uses its own explicit emptiness test.
[[nodiscard]] auto is_empty(const erhe::math::Aabb& aabb) -> bool
{
    return (aabb.min.x > aabb.max.x) ||
           (aabb.min.y > aabb.max.y) ||
           (aabb.min.z > aabb.max.z);
}

[[nodiscard]] auto node_own_local_aabb(const Node& node) -> erhe::math::Aabb
{
    erhe::math::Aabb aabb{};
    const std::shared_ptr<Mesh> mesh = get_attachment<Mesh>(&node);
    if (mesh) {
        for (const Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::math::Aabb local = mesh_primitive.primitive->get_bounding_box();
            if (!is_empty(local)) {
                aabb.include(local);
            }
        }
    }
    return aabb;
}

// Map a child's content box (in child-local space) into a target cell (in
// layout-local space) honoring per-axis alignment and margins. The layout owns
// translation and scale only; rotation is set to identity so the result is a
// clean TRS (no shear from non-uniform stretch combined with rotation).
[[nodiscard]] auto compute_child_placement(
    const erhe::math::Aabb&                cell,
    const erhe::math::Aabb&                content,
    const std::array<Layout_alignment, 3>& alignment,
    const glm::vec3                         margin_min,
    const glm::vec3                         margin_max
) -> Trs_transform
{
    erhe::math::Aabb c = content;
    if (is_empty(c)) {
        c.min = glm::vec3{0.0f, 0.0f, 0.0f};
        c.max = glm::vec3{0.0f, 0.0f, 0.0f};
    }

    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale      {1.0f, 1.0f, 1.0f};

    for (int axis = 0; axis < 3; ++axis) {
        const float cell_lo = cell.min[axis];
        const float cell_hi = cell.max[axis];
        float       avail_lo = cell_lo + margin_min[axis];
        float       avail_hi = cell_hi - margin_max[axis];
        if (avail_hi < avail_lo) {
            const float mid = 0.5f * (cell_lo + cell_hi);
            avail_lo = mid;
            avail_hi = mid;
        }
        const float avail  = avail_hi - avail_lo;
        const float c_lo   = c.min[axis];
        const float c_hi   = c.max[axis];
        const float c_size = c_hi - c_lo;

        switch (alignment[axis]) {
            case Layout_alignment::stretch: {
                scale[axis]       = (c_size > 1.0e-6f) ? (avail / c_size) : 1.0f;
                translation[axis] = avail_lo - (scale[axis] * c_lo);
                break;
            }
            case Layout_alignment::positive: {
                scale[axis]       = 1.0f;
                translation[axis] = avail_hi - c_hi;
                break;
            }
            case Layout_alignment::negative:
            default: {
                scale[axis]       = 1.0f;
                translation[axis] = avail_lo - c_lo;
                break;
            }
        }
    }

    return Trs_transform{translation, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, scale};
}

// Measure a direct child's content box in the child's own local space. A child
// that is itself a layout contributes its declared volume rather than its
// geometry (matches intent and breaks the recursion cycle).
[[nodiscard]] auto measure_child_content(const Node& child) -> erhe::math::Aabb
{
    const std::shared_ptr<Layout> child_layout = get_attachment<Layout>(&child);
    if (child_layout) {
        return child_layout->get_volume();
    }
    return compute_content_local_aabb(child);
}

// Build the n+1 track boundary positions along one axis, from low to high.
// When 'extents' has exactly 'count' entries they are honored as absolute track
// sizes accumulated from 'lo'; otherwise the range [lo, hi] is divided into
// 'count' equal tracks. The track count is clamped to at least 1.
[[nodiscard]] auto build_track_edges(
    const float               lo,
    const float               hi,
    const int                 count,
    const std::vector<float>& extents
) -> std::vector<float>
{
    const int n = (count > 1) ? count : 1;
    std::vector<float> edges;
    edges.reserve(static_cast<std::size_t>(n) + 1);
    edges.push_back(lo);
    if (extents.size() == static_cast<std::size_t>(n)) {
        float edge = lo;
        for (int k = 0; k < n; ++k) {
            edge += std::max(0.0f, extents[static_cast<std::size_t>(k)]); // a track cannot have negative size
            edges.push_back(edge);
        }
    } else {
        const float step = (hi - lo) / static_cast<float>(n);
        for (int k = 1; k <= n; ++k) {
            edges.push_back(lo + (step * static_cast<float>(k)));
        }
    }
    return edges;
}

// Per-child layout parameters resolved from an optional Layout_item attachment;
// a child without one uses these defaults. Shared by all layout algorithms.
class Resolved_item
{
public:
    std::array<Layout_alignment, 3> alignment{
        Layout_alignment::negative,
        Layout_alignment::negative,
        Layout_alignment::negative
    };
    glm::vec3  margin_min    {0.0f, 0.0f, 0.0f};
    glm::vec3  margin_max    {0.0f, 0.0f, 0.0f};
    bool       grid_cell_auto{true};
    glm::ivec3 grid_cell     {0, 0, 0};
    glm::ivec3 grid_span     {1, 1, 1};
};

[[nodiscard]] auto resolve_item(const Node& child) -> Resolved_item
{
    Resolved_item result;
    const std::shared_ptr<Layout_item> item = get_attachment<Layout_item>(&child);
    if (item) {
        result.alignment      = item->alignment;
        result.margin_min     = item->margin_min;
        result.margin_max     = item->margin_max;
        result.grid_cell_auto = item->grid_cell_auto;
        result.grid_cell      = item->grid_cell;
        result.grid_span      = item->grid_span;
    }
    return result;
}

// Advance a signed cursor by 'size', returning the [lo, hi] interval consumed
// and moving the cursor to the far edge (in the axis-sign direction).
[[nodiscard]] auto advance(float& cursor, const float sign, const float size) -> std::pair<float, float>
{
    float lo = 0.0f;
    float hi = 0.0f;
    if (sign > 0.0f) {
        lo = cursor;
        hi = cursor + size;
        cursor = hi;
    } else {
        hi = cursor;
        lo = cursor - size;
        cursor = lo;
    }
    return std::pair<float, float>{lo, hi};
}

// A flow "line": children packed along the primary axis. cross_s / cross_t are
// the maximum child sizes along the secondary / tertiary axes in this line.
class Flow_line
{
public:
    std::vector<std::size_t> members;
    float used_p {0.0f};
    float cross_s{0.0f};
    float cross_t{0.0f};
};

// A flow "sheet": lines stacked along the secondary axis. cross_t is the maximum
// line tertiary size in this sheet.
class Flow_sheet
{
public:
    std::vector<std::size_t> line_indices;
    float used_s {0.0f};
    float cross_t{0.0f};
};

} // anonymous namespace

auto compute_content_local_aabb(const Node& node) -> erhe::math::Aabb
{
    erhe::math::Aabb aabb = node_own_local_aabb(node);

    for (const std::shared_ptr<erhe::Hierarchy>& child_item : node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }
        const erhe::math::Aabb child_box = measure_child_content(*child);
        if (!is_empty(child_box)) {
            const glm::mat4 child_from_node = child->parent_from_node(); // child-local -> node-local
            aabb.include(child_box.transformed_by(child_from_node));
        }
    }
    return aabb;
}

namespace {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

constexpr erhe::property::Enum_entry c_layout_type_entries[] = {
    {"Stack", static_cast<int32_t>(Layout_type::stack)},
    {"Grid",  static_cast<int32_t>(Layout_type::grid)},
    {"Flow",  static_cast<int32_t>(Layout_type::flow)},
};

constexpr erhe::property::Enum_entry c_axis_direction_entries[] = {
    {"+X", static_cast<int32_t>(Axis_direction::pos_x)},
    {"-X", static_cast<int32_t>(Axis_direction::neg_x)},
    {"+Y", static_cast<int32_t>(Axis_direction::pos_y)},
    {"-Y", static_cast<int32_t>(Axis_direction::neg_y)},
    {"+Z", static_cast<int32_t>(Axis_direction::pos_z)},
    {"-Z", static_cast<int32_t>(Axis_direction::neg_z)},
};

constexpr std::string_view c_group = "Layout";

auto is_grid(const erhe::property::Dependency_object& object) -> bool
{
    return static_cast<const Layout&>(object).get_layout_type() == Layout_type::grid;
}

auto positive_tracks(const Property_value& value) -> bool
{
    const glm::ivec3 v = std::get<glm::ivec3>(value);
    return (v.x >= 1) && (v.y >= 1) && (v.z >= 1);
}

} // anonymous namespace

const erhe::property::Enum_info c_layout_type_enum_info   {"Layout_type",    c_layout_type_entries};
const erhe::property::Enum_info c_axis_direction_enum_info{"Axis_direction", c_axis_direction_entries};

const Property<Layout_type> Layout::type_property = Property<Layout_type>::register_member(
    "type", Layout::property_owner_type(), c_layout_type_enum_info, &Layout::m_type,
    Property_metadata{.default_value = erhe::property::make_value(Layout_type::stack), .ui = Property_ui{.group = c_group, .tooltip = "Stack along the primary axis, an explicit cell grid, or lines wrapped into sheets", .label = "Type"}}
);
const Property<glm::vec3> Layout::volume_min_property = Property<glm::vec3>::register_member<Layout, glm::vec3>(
    "volume_min", Layout::property_owner_type(), [](auto& layout) -> auto& { return layout.m_volume.min; },
    Property_metadata{.default_value = glm::vec3{-0.5f, -0.5f, -0.5f}, .ui = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "Local-space box the children are arranged in", .label = "Volume Min"}}
);
const Property<glm::vec3> Layout::volume_max_property = Property<glm::vec3>::register_member<Layout, glm::vec3>(
    "volume_max", Layout::property_owner_type(), [](auto& layout) -> auto& { return layout.m_volume.max; },
    Property_metadata{.default_value = glm::vec3{0.5f, 0.5f, 0.5f}, .ui = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "Local-space box the children are arranged in", .label = "Volume Max"}}
);
const Property<Axis_direction> Layout::primary_property = Property<Axis_direction>::register_member(
    "primary", Layout::property_owner_type(), c_axis_direction_enum_info, &Layout::m_primary,
    Property_metadata{.default_value = erhe::property::make_value(Axis_direction::pos_x), .ui = Property_ui{.group = c_group, .tooltip = "Signed axis the children advance along", .label = "Primary"}}
);
const Property<Axis_direction> Layout::secondary_property = Property<Axis_direction>::register_member(
    "secondary", Layout::property_owner_type(), c_axis_direction_enum_info, &Layout::m_secondary,
    Property_metadata{.default_value = erhe::property::make_value(Axis_direction::pos_y), .ui = Property_ui{.group = c_group, .tooltip = "Signed axis lines wrap into (grid and flow)", .label = "Secondary"}}
);
const Property<Axis_direction> Layout::tertiary_property = Property<Axis_direction>::register_member(
    "tertiary", Layout::property_owner_type(), c_axis_direction_enum_info, &Layout::m_tertiary,
    Property_metadata{.default_value = erhe::property::make_value(Axis_direction::pos_z), .ui = Property_ui{.group = c_group, .tooltip = "Signed axis sheets stack along (grid and flow)", .label = "Tertiary"}}
);
const Property<glm::vec3> Layout::gap_property = Property<glm::vec3>::register_member(
    "gap", Layout::property_owner_type(), &Layout::m_gap,
    Property_metadata{.default_value = glm::vec3{0.0f, 0.0f, 0.0f}, .ui = Property_ui{.min = 0.0f, .max = 10000.0f, .step = 0.01f, .group = c_group, .tooltip = "Spacing per level: primary, secondary, tertiary", .label = "Gap"}}
);
const Property<glm::ivec3> Layout::grid_track_count_property = Property<glm::ivec3>::register_member(
    "grid_track_count", Layout::property_owner_type(), &Layout::m_grid_track_count,
    Property_metadata{.default_value = glm::ivec3{1, 1, 1}, .ui = Property_ui{.min = 1.0f, .max = 1000.0f, .step = 0.1f, .group = c_group, .tooltip = "Grid: cells per axis", .label = "Grid Tracks", .visible_when = is_grid}},
    {}, positive_tracks
);

void Layout::set_layout_type     (const Type value)           { set_value(type_property, value); }
void Layout::set_volume_min      (const glm::vec3& value)     { set_value(volume_min_property, value); }
void Layout::set_volume_max      (const glm::vec3& value)     { set_value(volume_max_property, value); }
void Layout::set_primary         (const Axis_direction value) { set_value(primary_property, value); }
void Layout::set_secondary       (const Axis_direction value) { set_value(secondary_property, value); }
void Layout::set_tertiary        (const Axis_direction value) { set_value(tertiary_property, value); }
void Layout::set_gap             (const glm::vec3& value)     { set_value(gap_property, value); }
void Layout::set_grid_track_count(const glm::ivec3& value)    { set_value(grid_track_count_property, value); }

Layout::Layout(const Layout&) = default;
Layout::~Layout() noexcept    = default;

Layout::Layout(const std::string_view name)
    : Item{name}
{
}

Layout::Layout(const Layout& src, erhe::for_clone)
    : Item             {src, erhe::for_clone{}}
    , m_type             {src.m_type             }
    , m_volume           {src.m_volume           }
    , m_primary          {src.m_primary          }
    , m_secondary        {src.m_secondary        }
    , m_tertiary         {src.m_tertiary         }
    , m_gap              {src.m_gap              }
    , m_grid_track_count {src.m_grid_track_count }
    , m_grid_track_extent{src.m_grid_track_extent}
{
}

void Layout::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    const std::shared_ptr<Layout> shared_this = std::static_pointer_cast<Layout>(shared_from_this()); // keep alive

    Scene_host* const old_scene_host = static_cast<Scene_host*>(old_item_host);
    Scene_host* const new_scene_host = static_cast<Scene_host*>(new_item_host);

    if (old_scene_host != nullptr) {
        old_scene_host->unregister_layout(shared_this);
    }
    if (new_scene_host != nullptr) {
        new_scene_host->register_layout(shared_this);
    }
}

void Layout::update()
{
    Node* const layout_node = get_node();
    if (layout_node == nullptr) {
        return;
    }

    switch (m_type) {
        case Type::stack: {
            layout_stack(*layout_node);
            break;
        }
        case Type::grid: {
            layout_grid(*layout_node);
            break;
        }
        case Type::flow: {
            layout_flow(*layout_node);
            break;
        }
        default: {
            break;
        }
    }
}

void Layout::layout_stack(Node& layout_node)
{
    const int   primary_axis = axis_index(m_primary);
    const float primary_sign = axis_sign(m_primary);
    const float primary_gap  = m_gap[primary_axis];

    float cursor = (primary_sign > 0.0f) ? m_volume.min[primary_axis] : m_volume.max[primary_axis];

    for (const std::shared_ptr<erhe::Hierarchy>& child_item : layout_node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }

        erhe::math::Aabb content = measure_child_content(*child);
        if (is_empty(content)) {
            content.min = glm::vec3{0.0f, 0.0f, 0.0f};
            content.max = glm::vec3{0.0f, 0.0f, 0.0f};
        }
        const float extent = content.max[primary_axis] - content.min[primary_axis];

        // Cell keeps the full volume extent on the two cross axes; the primary
        // axis is the slice this child consumes (sized to the child's own extent).
        // A Layout_item margin on the primary axis therefore shifts the child
        // within its slice but does not reserve extra space between neighbours;
        // inter-child spacing on the primary axis is controlled by 'gap'.
        erhe::math::Aabb cell = m_volume;
        if (primary_sign > 0.0f) {
            cell.min[primary_axis] = cursor;
            cell.max[primary_axis] = cursor + extent;
            cursor = cell.max[primary_axis] + primary_gap;
        } else {
            cell.max[primary_axis] = cursor;
            cell.min[primary_axis] = cursor - extent;
            cursor = cell.min[primary_axis] - primary_gap;
        }

        const Resolved_item params = resolve_item(*child);
        const Trs_transform placement = compute_child_placement(cell, content, params.alignment, params.margin_min, params.margin_max);
        child->set_parent_from_node(placement);
    }
}

void Layout::layout_grid(Node& layout_node)
{
    const std::vector<float> edges_x = build_track_edges(m_volume.min.x, m_volume.max.x, m_grid_track_count.x, m_grid_track_extent[0]);
    const std::vector<float> edges_y = build_track_edges(m_volume.min.y, m_volume.max.y, m_grid_track_count.y, m_grid_track_extent[1]);
    const std::vector<float> edges_z = build_track_edges(m_volume.min.z, m_volume.max.z, m_grid_track_count.z, m_grid_track_extent[2]);
    const int track_count_x = static_cast<int>(edges_x.size()) - 1;
    const int track_count_y = static_cast<int>(edges_y.size()) - 1;
    const int track_count_z = static_cast<int>(edges_z.size()) - 1;
    const glm::ivec3 track_count{track_count_x, track_count_y, track_count_z};

    // Auto placement: children without an explicit cell flow into successive
    // cells in document order - primary axis fastest, wrapping into the
    // secondary axis, then into the tertiary axis (like the flow layout but on
    // cell indices). A negative axis direction walks that axis from the last
    // cell towards the first. Each auto child consumes its span along the
    // primary axis; a wrap advances by the largest secondary/tertiary span
    // seen in the completed row/sheet. Explicitly placed children do not move
    // the cursor and are not tracked for occupancy, so mixing explicit and
    // auto children can overlap.
    const int p_axis  = axis_index(m_primary);
    const int s_axis  = axis_index(m_secondary);
    const int t_axis  = axis_index(m_tertiary);
    glm::ivec3 logical_cursor{0, 0, 0}; // (primary, secondary, tertiary) positions, sign-independent
    int row_advance_s  = 1; // secondary advance when the current row wraps
    int sheet_advance_t = 1; // tertiary advance when the current sheet wraps

    for (const std::shared_ptr<erhe::Hierarchy>& child_item : layout_node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }

        erhe::math::Aabb content = measure_child_content(*child);
        if (is_empty(content)) {
            content.min = glm::vec3{0.0f, 0.0f, 0.0f};
            content.max = glm::vec3{0.0f, 0.0f, 0.0f};
        }

        const Resolved_item params = resolve_item(*child);

        glm::ivec3 child_cell = params.grid_cell;
        if (params.grid_cell_auto) {
            const int span_p = std::clamp(params.grid_span[p_axis], 1, track_count[p_axis]);
            const int span_s = std::clamp(params.grid_span[s_axis], 1, track_count[s_axis]);
            const int span_t = std::clamp(params.grid_span[t_axis], 1, track_count[t_axis]);
            if ((logical_cursor[0] + span_p) > track_count[p_axis]) {
                logical_cursor[0]  = 0;
                logical_cursor[1] += row_advance_s;
                row_advance_s      = 1;
            }
            if ((logical_cursor[1] + span_s) > track_count[s_axis]) {
                logical_cursor[1]  = 0;
                logical_cursor[2] += sheet_advance_t;
                sheet_advance_t    = 1;
            }
            if ((logical_cursor[2] + span_t) > track_count[t_axis]) {
                logical_cursor[2] = 0; // grid full: wrap around (overlaps from the start)
            }
            // Convert sign-independent logical positions into cell indices:
            // a positive direction counts up from the first cell, a negative
            // direction counts down from the last cell (span occupies the
            // cells below the converted start index).
            const auto to_cell = [](const Axis_direction direction, const int logical, const int span, const int count) -> int {
                return (axis_sign(direction) > 0.0f) ? logical : (count - logical - span);
            };
            child_cell[p_axis] = to_cell(m_primary,   logical_cursor[0], span_p, track_count[p_axis]);
            child_cell[s_axis] = to_cell(m_secondary, logical_cursor[1], span_s, track_count[s_axis]);
            child_cell[t_axis] = to_cell(m_tertiary,  logical_cursor[2], span_t, track_count[t_axis]);
            row_advance_s      = std::max(row_advance_s,   span_s);
            sheet_advance_t    = std::max(sheet_advance_t, span_t);
            logical_cursor[0] += span_p;
        }

        // Clamp the cell index and span into the available track range.
        const int cx = std::clamp(child_cell.x, 0, track_count_x - 1);
        const int cy = std::clamp(child_cell.y, 0, track_count_y - 1);
        const int cz = std::clamp(child_cell.z, 0, track_count_z - 1);
        const int sx = std::clamp(params.grid_span.x, 1, track_count_x - cx);
        const int sy = std::clamp(params.grid_span.y, 1, track_count_y - cy);
        const int sz = std::clamp(params.grid_span.z, 1, track_count_z - cz);

        erhe::math::Aabb cell;
        cell.min = glm::vec3{
            edges_x[static_cast<std::size_t>(cx)],
            edges_y[static_cast<std::size_t>(cy)],
            edges_z[static_cast<std::size_t>(cz)]
        };
        cell.max = glm::vec3{
            edges_x[static_cast<std::size_t>(cx + sx)],
            edges_y[static_cast<std::size_t>(cy + sy)],
            edges_z[static_cast<std::size_t>(cz + sz)]
        };

        const Trs_transform placement = compute_child_placement(cell, content, params.alignment, params.margin_min, params.margin_max);
        child->set_parent_from_node(placement);
    }
}

void Layout::layout_flow(Node& layout_node)
{
    // primary / secondary / tertiary should select three distinct axes. The cell
    // is seeded from the full volume so a misconfiguration (duplicate axis) leaves
    // the unset axis at full extent rather than producing an invalid cell.
    const int   primary_axis   = axis_index(m_primary);
    const int   secondary_axis = axis_index(m_secondary);
    const int   tertiary_axis  = axis_index(m_tertiary);
    const float primary_sign   = axis_sign(m_primary);
    const float secondary_sign = axis_sign(m_secondary);
    const float tertiary_sign  = axis_sign(m_tertiary);
    const float primary_gap    = m_gap[primary_axis];
    const float secondary_gap  = m_gap[secondary_axis];
    const float tertiary_gap   = m_gap[tertiary_axis];
    const float cap_primary    = m_volume.max[primary_axis]   - m_volume.min[primary_axis];
    const float cap_secondary  = m_volume.max[secondary_axis] - m_volume.min[secondary_axis];
    const float epsilon        = 1.0e-4f;

    // Collect direct Node children with their measured content boxes.
    std::vector<std::shared_ptr<Node>> children;
    std::vector<erhe::math::Aabb>      contents;
    for (const std::shared_ptr<erhe::Hierarchy>& child_item : layout_node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }
        erhe::math::Aabb content = measure_child_content(*child);
        if (is_empty(content)) {
            content.min = glm::vec3{0.0f, 0.0f, 0.0f};
            content.max = glm::vec3{0.0f, 0.0f, 0.0f};
        }
        children.push_back(child);
        contents.push_back(content);
    }

    // Pass 1a: group children into lines along the primary axis (wrap on cap_primary).
    std::vector<Flow_line> lines;
    Flow_line current_line;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const float primary_len   = contents[i].max[primary_axis]   - contents[i].min[primary_axis];
        const float secondary_len = contents[i].max[secondary_axis] - contents[i].min[secondary_axis];
        const float tertiary_len  = contents[i].max[tertiary_axis]  - contents[i].min[tertiary_axis];
        float add_primary = (current_line.members.empty() ? 0.0f : primary_gap) + primary_len;
        if (!current_line.members.empty() && ((current_line.used_p + add_primary) > (cap_primary + epsilon))) {
            lines.push_back(current_line);
            current_line = Flow_line{};
            add_primary = primary_len;
        }
        current_line.used_p  += add_primary;
        current_line.cross_s  = std::max(current_line.cross_s, secondary_len);
        current_line.cross_t  = std::max(current_line.cross_t, tertiary_len);
        current_line.members.push_back(i);
    }
    if (!current_line.members.empty()) {
        lines.push_back(current_line);
    }

    // Pass 1b: group lines into sheets along the secondary axis (wrap on cap_secondary).
    std::vector<Flow_sheet> sheets;
    Flow_sheet current_sheet;
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const float line_cross_s = lines[line_index].cross_s;
        float add_secondary = (current_sheet.line_indices.empty() ? 0.0f : secondary_gap) + line_cross_s;
        if (!current_sheet.line_indices.empty() && ((current_sheet.used_s + add_secondary) > (cap_secondary + epsilon))) {
            sheets.push_back(current_sheet);
            current_sheet = Flow_sheet{};
            add_secondary = line_cross_s;
        }
        current_sheet.used_s  += add_secondary;
        current_sheet.cross_t  = std::max(current_sheet.cross_t, lines[line_index].cross_t);
        current_sheet.line_indices.push_back(line_index);
    }
    if (!current_sheet.line_indices.empty()) {
        sheets.push_back(current_sheet);
    }

    // Pass 2: assign each child a cell and place it. Sheets stack along the
    // tertiary axis (no further wrapping; overflow past the volume is allowed).
    float tertiary_cursor = (tertiary_sign > 0.0f) ? m_volume.min[tertiary_axis] : m_volume.max[tertiary_axis];
    for (const Flow_sheet& sheet : sheets) {
        const std::pair<float, float> tertiary_interval = advance(tertiary_cursor, tertiary_sign, sheet.cross_t);
        float secondary_cursor = (secondary_sign > 0.0f) ? m_volume.min[secondary_axis] : m_volume.max[secondary_axis];
        for (const std::size_t line_index : sheet.line_indices) {
            const Flow_line& line = lines[line_index];
            const std::pair<float, float> secondary_interval = advance(secondary_cursor, secondary_sign, line.cross_s);
            float primary_cursor = (primary_sign > 0.0f) ? m_volume.min[primary_axis] : m_volume.max[primary_axis];
            for (const std::size_t i : line.members) {
                const float primary_len = contents[i].max[primary_axis] - contents[i].min[primary_axis];
                const std::pair<float, float> primary_interval = advance(primary_cursor, primary_sign, primary_len);

                erhe::math::Aabb cell = m_volume;
                cell.min[primary_axis]   = primary_interval.first;   cell.max[primary_axis]   = primary_interval.second;
                cell.min[secondary_axis] = secondary_interval.first; cell.max[secondary_axis] = secondary_interval.second;
                cell.min[tertiary_axis]  = tertiary_interval.first;  cell.max[tertiary_axis]  = tertiary_interval.second;

                const Resolved_item params = resolve_item(*children[i]);
                const Trs_transform placement = compute_child_placement(cell, contents[i], params.alignment, params.margin_min, params.margin_max);
                children[i]->set_parent_from_node(placement);

                primary_cursor += (primary_sign > 0.0f) ? primary_gap : -primary_gap;
            }
            secondary_cursor += (secondary_sign > 0.0f) ? secondary_gap : -secondary_gap;
        }
        tertiary_cursor += (tertiary_sign > 0.0f) ? tertiary_gap : -tertiary_gap;
    }
}

} // namespace erhe::scene
