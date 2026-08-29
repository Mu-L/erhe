#ifndef ERHE_VERTEX_JOINT_WEIGHTS_GLSL
#define ERHE_VERTEX_JOINT_WEIGHTS_GLSL

// Skinning weight decode.
//
// Same contract as the other erhe_vertex_*.glsl decoders: the storage is a
// property of the vertex format, so ERHE_VERTEX_JOINT_WEIGHTS_ENCODING is
// emitted by Shader_stages_create_info::attributes_source() from that format,
// and Shader_key omits it from get_defines() so it is never defined twice.
//
// VERTEX STAGES ONLY - it reads a vertex attribute.
//
// Keep in sync with erhe::dataformat::Vertex_joint_weights_encoding and with
// erhe::primitive::encode_implicit_sum_joint_weights().

#define ERHE_VERTEX_JOINT_WEIGHTS_ENCODING_PASSTHROUGH            0
#define ERHE_VERTEX_JOINT_WEIGHTS_ENCODING_UNORM16X3_IMPLICIT_SUM 1

#ifndef ERHE_VERTEX_JOINT_WEIGHTS_ENCODING
#   error "ERHE_VERTEX_JOINT_WEIGHTS_ENCODING is not defined - this shader was compiled without a vertex format"
#endif

// The four skinning weights, in the order a_joint_indices_0 names the joints.
// Defined for every format, including those with no joint attributes at all -
// the callers are gated on ERHE_USE_SKINNING, which needs both joint attributes
// present, but the function still has to compile into a non-skinned variant.
vec4 erhe_decode_vertex_joint_weights()
{
#if !defined(ERHE_ATTRIBUTE_a_joint_weights_0)
    return vec4(1.0, 0.0, 0.0, 0.0);
#elif ERHE_VERTEX_JOINT_WEIGHTS_ENCODING == ERHE_VERTEX_JOINT_WEIGHTS_ENCODING_UNORM16X3_IMPLICIT_SUM
    // The encoder sorted the influences so the SMALLEST weight is last and
    // permuted a_joint_indices_0 in lockstep, then stored only the first three.
    // The four sum to one, so the last is whatever is left; the max() guards
    // the rounding case, and the encoder additionally caps the stored three at
    // one unit so this cannot go far negative.
    vec3 stored = a_joint_weights_0;
    return vec4(stored, max(0.0, 1.0 - (stored.x + stored.y + stored.z)));
#else
    return a_joint_weights_0;
#endif
}

#endif // ERHE_VERTEX_JOINT_WEIGHTS_GLSL
