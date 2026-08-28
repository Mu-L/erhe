#ifndef ERHE_VERTEX_POSITION_GLSL
#define ERHE_VERTEX_POSITION_GLSL

// Vertex position decode.
//
// The storage format of a_position is a property of the vertex format, so the
// value of ERHE_VERTEX_POSITION_ENCODING is emitted by
// Shader_stages_create_info::attributes_source() from that format - no compile
// site passes it explicitly, and Shader_key deliberately omits it from
// get_defines() so it is never defined twice.
//
// Keep the enumerators in sync with erhe::dataformat::Vertex_position_encoding.

#define ERHE_VERTEX_POSITION_ENCODING_PASSTHROUGH    0
#define ERHE_VERTEX_POSITION_ENCODING_SNORM16X3_AABB 1

#ifndef ERHE_VERTEX_POSITION_ENCODING
#   error "ERHE_VERTEX_POSITION_ENCODING is not defined - this shader was compiled without a vertex format"
#endif

// scale / offset are the primitive's dequantization affine (its object space
// AABB half extent and center). They are taken as parameters rather than read
// from primitive.primitives[] because not every shader that decodes a position
// has that block in scope - the lightmap baker carries the same two vectors in
// its own per-draw block.
//
// Note both arguments are evaluated in either variant, since they sit outside
// the #if: a shader converted to call this must already have the two fields in
// whatever block it reads them from. In the passthrough variant the loads are
// dead and the compiler drops them.
vec3 erhe_decode_vertex_position(vec3 scale, vec3 offset)
{
#if ERHE_VERTEX_POSITION_ENCODING == ERHE_VERTEX_POSITION_ENCODING_SNORM16X3_AABB
    return a_position * scale + offset;
#else
    return a_position;
#endif
}

#endif // ERHE_VERTEX_POSITION_GLSL
