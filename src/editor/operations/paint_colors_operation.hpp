#pragma once

#include "operations/operation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/enums.hpp"

#include <geogram/basic/numeric.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class App_context;

// Undo-able edit of the vertex colors (corner_color_0 attribute) of a set of
// geometry corners of a single mesh primitive, produced by one Paint_tool
// stroke. Before this operation existed, paint lived only in the base
// variant's GPU buffer and any primitive rebuild silently discarded it.
//
// Follows Paint_weights_operation: the *same* Geometry object is mutated in
// place (so Mesh_component_selection entries keyed on the Geometry pointer
// survive), and the primitive is rebuilt and shared across every mesh that
// references the Geometry. The rebuild is also what brings the optimized
// variant back after the stroke's edit-start invalidation (requirement 11).
// No physics or normal work: painting colors changes neither positions nor
// topology.
//
// A corner that had no explicit color before the stroke gets its undo value
// from what the builder would have rendered (vertex color, else the build's
// constant color) - the attribute becomes explicitly set on undo, but the
// rendered result is identical.
class Paint_colors_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>        mesh;
        std::size_t                               primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry> geometry;
        std::vector<GEO::index_t>                 corners;       // touched geometry corners
        std::vector<glm::vec4>                    before_colors; // parallel to corners
        std::vector<glm::vec4>                    after_colors;  // parallel to corners
        erhe::primitive::Build_info               build_info;
        erhe::primitive::Normal_style             normal_style{erhe::primitive::Normal_style::corner_normals};
    };

    explicit Paint_colors_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(App_context& context, const std::vector<glm::vec4>& colors);

    Parameters m_parameters;
};

}
