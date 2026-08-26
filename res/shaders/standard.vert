#include "erhe_camera_view.glsl"
#include "erhe_skinning.glsl"
#include "erhe_standard_variant.glsl"
#include "erhe_vertex_position.glsl"

#define a_valency_edge_count a_custom_2

#if defined(ERHE_VARIANT_ID_RENDER)
// Locations 0 and 1 are reused under the ID-render variant for the two
// flat outputs the fragment packs into an RGB id. The lit varyings below
// are skipped via ERHE_VARIANT_POSITION_PASS, so there is no link
// conflict.
layout(location = 0) flat out int v_draw_id;
layout(location = 1) flat out int v_primitive_id;
#endif

#if defined(ERHE_VARIANT_POINTS)
// The points variant (corner points / polygon centroids) draws each vertex
// as a GL point. Redeclaring gl_PerVertex is required to write gl_PointSize;
// each variant compiles separately and multiview does not redeclare the
// block, so there is no conflict. Location 0 carries the flat point color --
// free here because v_position is gated out under ERHE_VARIANT_POSITION_PASS
// and the ID-render variant (the only other user of location 0) is mutually
// exclusive with points.
out gl_PerVertex {
    vec4  gl_Position;
    float gl_PointSize;
};
layout(location = 0) out vec4 v_point_color;
#endif

#if defined(ERHE_USE_VARYING_POSITION)
layout(location = 0) out vec4      v_position;
#endif

// TODO In the future we might have alpha test which would need texcoord
//      to be passed to fragment shader
#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD0) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 1) out vec2      v_texcoord_0;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD1) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 12) out vec2     v_texcoord_1;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD2) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 21) out vec2      v_texcoord_2;             // lightmap UVs
layout(location = 22) flat out vec4 v_lightmap_scale_offset;  // atlas region; xy 0 = no lightmap
#endif

#if defined(ERHE_USE_VERTEX_VARYING_COLOR) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 2) out vec4      v_color;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_ANISO_CONTROL) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 3) out vec2      v_aniso_control;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TANGENT) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 4) out vec3      v_T;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 5) out vec3      v_B;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_NORMAL) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 6) out vec3      v_N;
#endif

// TODO In the future we might have alpha test which would need material_index
//      to be passed to fragment shader
#if !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 7) flat out uint v_material_index;
#endif

// Debug-visualization varyings, only emitted when the viewport has
// selected a Shader_debug != none. Each is further gated on
// attribute presence so meshes lacking the underlying attribute
// still link; the fragment shader treats absent attributes as
// neutral defaults.
#if (ERHE_SHADER_DEBUG != 0) && !defined(ERHE_VARIANT_POSITION_PASS)
#  ifdef ERHE_ATTRIBUTE_a_tangent
layout(location =  8) out float      v_tangent_scale;
#  endif
layout(location =  9) out float      v_line_width;
#  ifdef ERHE_USE_SKINNING
layout(location = 10) out vec4       v_bone_color;
#  endif
#  ifdef ERHE_ATTRIBUTE_a_custom_2
layout(location = 11) flat out uvec2 v_valency_edge_count;
#  endif
#endif

// Single-joint weight for the joint_weight_ramp debug mode. Sign-encoded
// Blender-style: >= 0 = the target joint's weight at this vertex, -1 =
// no influence from the target joint with the zero-black option on
// (joint.debug_joint_indices.y != 0), -2 = no target joint selected.
// Only skinned variants emit it; the fragment side falls back to a const
// and leaves non-skinned meshes with their normal shading.
#if (ERHE_SHADER_DEBUG == 34) && defined(ERHE_USE_SKINNING) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 23) out float v_weight;
#endif

// Untransformed node-space position for the node_* texgen modes
// (ERHE_SELECT_TEXCOORD in standard.frag); only emitted when a material
// texture slot or the circular-brushed-metal block sources one.
#if defined(ERHE_USE_VERTEX_VARYING_NODE_POSITION) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 24) out vec3 v_node_position;
#endif

// Solid-wireframe varyings: a per-vertex barycentric basis (reconstructed from
// the packed corner index in a_custom_4) plus the per-triangle real-edge mask
// and the wireframe color / width. Only emitted for the expanded fill mesh
// (which carries a_custom_4) under the solid-wireframe variant.
#if defined(ERHE_SOLID_WIREFRAME) && defined(ERHE_ATTRIBUTE_a_custom_4) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 13)      out vec3  v_bary;
layout(location = 14) flat out uint  v_edge_mask;
layout(location = 15) flat out vec4  v_wire_color;
layout(location = 16) flat out float v_wire_width;
#endif

void main()
{
    mat4 world_from_node;
    mat4 world_from_node_normal;

#ifdef ERHE_USE_SKINNING
    if (primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5) {
        world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
        world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
    } else {
        erhe_skin_matrices(
            primitive.primitives[ERHE_DRAW_ID].base_joint_index,
            a_joint_indices_0,
            a_joint_weights_0,
            world_from_node,
            world_from_node_normal
        );
    }
#else
    world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
#endif

    mat4 clip_from_world = camera.cameras[c_view_index].clip_from_world;

    // Object space position: identical to a_position unless the vertex format
    // stores it quantized into the primitive AABB, in which case this is the
    // decode. ERHE_DRAW_ID is the same primitive record the transforms above
    // came from, so caster and lit pass always decode with the same affine.
    vec3 node_position   = erhe_decode_vertex_position(
        primitive.primitives[ERHE_DRAW_ID].position_scale.xyz,
        primitive.primitives[ERHE_DRAW_ID].position_offset.xyz
    );

    vec4 position        = world_from_node * vec4(node_position, 1.0);
    gl_Position          = clip_from_world * position;

#if defined(ERHE_VARIANT_SHADOW_CUBE)
    // Point-light shadow cube caster: the fragment shader needs the world
    // position to compute radial distance to the light. This is a position pass,
    // so the lit-varying block below is skipped; assign v_position here. The
    // per-face clip-space y-flip that makes the stored face match the
    // samplerCubeArray (s,t) convention is NOT done here: it is applied on the
    // C++ side via the cube caster's coordinate conventions (clip_space_y_flip
    // derived from framebuffer_origin), baked into clip_from_world. See the
    // Shadow_renderer point-cube pass.
    v_position = position;
#endif

#if defined(ERHE_VARIANT_ID_RENDER)
    v_draw_id      = ERHE_DRAW_ID;
    // Emit this facet's GEO index directly, decoded from the per-vertex facet-id
    // attribute (a_custom_0 = build_polygon_id()'s vec4_from_uint(facet)). Every
    // corner of a facet carries the same value, so this flat output is the facet
    // id regardless of provoking vertex or how the facet was triangulated -- the
    // value Id_renderer's readback packs as (id_offset + facet_id) and the picking
    // resolve consumes as a GEO facet index (the same index space mesh-component
    // selection and the facet debug viz use). Deriving it arithmetically from
    // gl_VertexID/3 over the shared fan-triangulated pool was wrong for any facet
    // with more than 3 corners; this attribute is robust by construction.
#   if defined(ERHE_ATTRIBUTE_a_custom_0)
    // a_custom_0: r = (facet>>24), g = (facet>>16), b = (facet>>8), a = (facet>>0);
    // small facet indices live entirely in .a, so all four components recombine.
    v_primitive_id = int(
        (uint(a_custom_0.r * 255.0 + 0.5) << 24) |
        (uint(a_custom_0.g * 255.0 + 0.5) << 16) |
        (uint(a_custom_0.b * 255.0 + 0.5) <<  8) |
         uint(a_custom_0.a * 255.0 + 0.5));
#   else
    // Identity-only meshes (rendertarget / tools) whose vertex format lacks the
    // attribute: mesh identity comes from the id_offset range, not this value.
    v_primitive_id = 0;
#   endif
#endif

#if defined(ERHE_VARIANT_POINTS)
    // Point size and color are per-draw-primitive values from the primitive
    // buffer. gl_PointSize uses 1/distance attenuation (floored at 2 px). The
    // small depth bias pulls the point toward the camera so it passes the
    // surface depth test (Compare_operation::less) instead of z-fighting the
    // mesh corner / facet centroid it sits on.
    vec3  view_position_in_world = camera.cameras[c_view_index].world_from_node[3].xyz;
    float point_distance         = distance(view_position_in_world, position.xyz);
    float clip_depth_direction   = camera.cameras[c_view_index].clip_depth_direction;
#   ifdef ERHE_ATTRIBUTE_a_normal
    vec3  point_normal           = normalize(vec3(world_from_node_normal * vec4(a_normal, 0.0)));
    vec3  point_view_vector      = normalize(view_position_in_world - position.xyz);
    float point_NdotV            = dot(point_normal, point_view_vector);
    gl_Position.z               -= clip_depth_direction * 0.0005 * abs(point_NdotV);
#   else
    gl_Position.z               -= clip_depth_direction * 0.0005;
#   endif
    gl_PointSize  = max(primitive.primitives[ERHE_DRAW_ID].size / point_distance, 2.0);
    v_point_color = primitive.primitives[ERHE_DRAW_ID].color;
#endif

#if !defined(ERHE_VARIANT_POSITION_PASS)

#   if defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal, 0.0)));
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TANGENT)
    vec3 tangent         = vec3(world_from_node * vec4(a_tangent.xyz, 0.0));
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    vec3 bitangent       = cross(normal, tangent) * a_tangent.w;
#   endif

#   if (ERHE_SHADER_DEBUG != 0) && defined(ERHE_USE_SKINNING)
    if (primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5) {
        v_bone_color = vec4(0.3, 0.0, 0.3, 1.0);
    } else {
        v_bone_color =
            a_joint_weights_0.x * joint.debug_joint_colors[(int(a_joint_indices_0.x) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.y * joint.debug_joint_colors[(int(a_joint_indices_0.y) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.z * joint.debug_joint_colors[(int(a_joint_indices_0.z) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.w * joint.debug_joint_colors[(int(a_joint_indices_0.w) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count];
    }
#   endif

#   if (ERHE_SHADER_DEBUG == 34) && defined(ERHE_USE_SKINNING)
    // joint_weight_ramp: sum this vertex's influence from the target joint.
    // debug_joint_indices.x == 0xffffffffu = no target joint;
    // .y != 0 = show zero-weight vertices as black (-1).
    if ((primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5) || (joint.debug_joint_indices.x == 0xffffffffu)) {
        v_weight = -2.0;
    } else {
        uint base_joint = primitive.primitives[ERHE_DRAW_ID].base_joint_index;
        float w =
            ((joint.joints[int(a_joint_indices_0.x) + int(base_joint)].debug_flags.x != 0u) ? a_joint_weights_0.x : 0.0) +
            ((joint.joints[int(a_joint_indices_0.y) + int(base_joint)].debug_flags.x != 0u) ? a_joint_weights_0.y : 0.0) +
            ((joint.joints[int(a_joint_indices_0.z) + int(base_joint)].debug_flags.x != 0u) ? a_joint_weights_0.z : 0.0) +
            ((joint.joints[int(a_joint_indices_0.w) + int(base_joint)].debug_flags.x != 0u) ? a_joint_weights_0.w : 0.0);
        bool zero_black = joint.debug_joint_indices.y != 0u;
        v_weight = ((w <= 0.0) && zero_black) ? -1.0 : w;
    }
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TANGENT)
    v_T              = normalize(tangent  );
#   endif

    // Guard must match the bitangent declaration above (BITANGENT alone is
    // not enough - the cross product needs normal and tangent), so a key
    // that violates the BITANGENT => TANGENT && NORMAL invariant degrades
    // to an unwritten varying instead of a compile error.
#   if defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    v_B              = normalize(bitangent);
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    v_N              = normal;
#   endif
    v_position       = position;

    v_material_index = primitive.primitives[ERHE_DRAW_ID].material_index;

#   if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD0)
    v_texcoord_0     = a_texcoord_0;
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_NODE_POSITION)
    v_node_position  = node_position;
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD2)
    v_texcoord_2            = a_texcoord_2;
    v_lightmap_scale_offset = primitive.primitives[ERHE_DRAW_ID].lightmap_scale_offset;
#   endif
#   if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD1)
    v_texcoord_1     = a_texcoord_1;
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_COLOR)
    v_color          = a_color_0;
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_ANISO_CONTROL)
    v_aniso_control  = a_custom_1;
#   endif

#   if ERHE_SHADER_DEBUG != 0
#       ifdef ERHE_ATTRIBUTE_a_tangent
    v_tangent_scale       = a_tangent.w;
#       endif
    v_line_width          = primitive.primitives[ERHE_DRAW_ID].size;
#       if defined(ERHE_ATTRIBUTE_a_custom_2)
    v_valency_edge_count  = a_valency_edge_count;
#       endif
#   endif

#   if defined(ERHE_SOLID_WIREFRAME) && defined(ERHE_ATTRIBUTE_a_custom_4)
    // a_custom_4: bits 0..1 = triangle corner index (barycentric basis),
    // bits 2..4 = real-edge mask (bit b gates barycentric component b).
    uint wireframe_corner = a_custom_4 & 3u;
    v_bary       = vec3(
        (wireframe_corner == 0u) ? 1.0 : 0.0,
        (wireframe_corner == 1u) ? 1.0 : 0.0,
        (wireframe_corner == 2u) ? 1.0 : 0.0
    );
    v_edge_mask  = (a_custom_4 >> 2u) & 7u;
    v_wire_color = primitive.primitives[ERHE_DRAW_ID].color;
    v_wire_width = primitive.primitives[ERHE_DRAW_ID].size;
#   endif
#endif

}
