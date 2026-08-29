#ifndef ERHE_VERTEX_TEXCOORD_GLSL
#define ERHE_VERTEX_TEXCOORD_GLSL

// Vertex texcoord decode, for channels 0 and 1.
//
// Same contract as erhe_vertex_position.glsl: the storage format is a property
// of the vertex format, so ERHE_VERTEX_TEXCOORD_ENCODING is emitted by
// Shader_stages_create_info::attributes_source() from that format - no compile
// site passes it explicitly, and Shader_key deliberately omits it from
// get_defines() so it is never defined twice.
//
// Channel 2 (lightmap UVs) is NOT covered here. It is in [0, 1] by
// construction, so the optimized variant stores it as plain unorm16 with no
// affine and the shader reads it undecoded - the lightmap atlas region
// (primitive.lightmap_scale_offset) composes with it exactly as before.
//
// Keep the enumerators in sync with erhe::dataformat::Vertex_texcoord_encoding.

#define ERHE_VERTEX_TEXCOORD_ENCODING_PASSTHROUGH      0
#define ERHE_VERTEX_TEXCOORD_ENCODING_UNORM16X2_AFFINE 1

#ifndef ERHE_VERTEX_TEXCOORD_ENCODING
#   error "ERHE_VERTEX_TEXCOORD_ENCODING is not defined - this shader was compiled without a vertex format"
#endif

// scale / offset are the primitive's per-channel UV dequantization affine (the
// extent and minimum of that channel's UV range over the primitive's vertices).
// Taken as parameters rather than read from primitive.primitives[] for the same
// reason the position decode does: not every shader that decodes a texcoord has
// that block in scope.
//
// Both arguments are evaluated in either variant, since they sit outside the
// #if. In the passthrough variant the loads are dead and the compiler drops
// them.
vec2 erhe_decode_vertex_texcoord(vec2 texcoord, vec2 scale, vec2 offset)
{
#if ERHE_VERTEX_TEXCOORD_ENCODING == ERHE_VERTEX_TEXCOORD_ENCODING_UNORM16X2_AFFINE
    return texcoord * scale + offset;
#else
    return texcoord;
#endif
}

#endif // ERHE_VERTEX_TEXCOORD_GLSL
