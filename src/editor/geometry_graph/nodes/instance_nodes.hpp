#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_property/dependency_property.hpp"

namespace editor {

// Scatters points on the surface of the input geometry: facets are fan
// triangulated, triangles are picked with probability proportional to
// their area, and points are sampled uniformly within the triangle.
// The sampling is deterministic for a given (geometry, count, seed).
// Each point carries the normal of its source facet.
class Distribute_points_node : public Geometry_graph_node
{
public:
    Distribute_points_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Parameter properties bridged over the members below (D18 / D27);
    // Mesh_torus_node is the recipe.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    static const erhe::property::Property<int> count_property;
    static const erhe::property::Property<int> seed_property;

private:
    int m_count{100};
    int m_seed {0};
};

// Places one instance of the input geometry at every input point,
// producing an instance set (geometry reference + per-point transforms).
// Optionally aligns the instance +Y axis with the point normal.
class Instance_on_points_node : public Geometry_graph_node
{
public:
    Instance_on_points_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    static const erhe::property::Property<float> scale_property;
    static const erhe::property::Property<bool>  align_to_normal_property;

private:
    float m_scale          {1.0f};
    bool  m_align_to_normal{true};
};

// Flattens an instance set into real geometry by merging every
// referenced geometry once per instance transform.
class Realize_instances_node : public Geometry_graph_node
{
public:
    Realize_instances_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
};

}
