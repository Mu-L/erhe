#ifndef ERHE_VERTEX_TBN_GLSL
#define ERHE_VERTEX_TBN_GLSL

// Tangent frame decode.
//
// Same contract as erhe_vertex_position.glsl and erhe_vertex_texcoord.glsl: the
// storage is a property of the vertex format, so ERHE_VERTEX_TBN_ENCODING is
// emitted by Shader_stages_create_info::attributes_source() from that format,
// and Shader_key deliberately omits it from get_defines() so it is never
// defined twice.
//
// VERTEX STAGES ONLY - it reads vertex attributes. A fragment shader gets the
// ERHE_VERTEX_TBN_ENCODING define like every other stage but must not include
// this file.
//
// Keep in sync with erhe::dataformat::Vertex_tbn_encoding and with
// erhe::primitive::encode_tbn_quaternion(), which writes the bits this reads.

#define ERHE_VERTEX_TBN_ENCODING_PASSTHROUGH  0
#define ERHE_VERTEX_TBN_ENCODING_QUATERNION16 1

#ifndef ERHE_VERTEX_TBN_ENCODING
#   error "ERHE_VERTEX_TBN_ENCODING is not defined - this shader was compiled without a vertex format"
#endif

// Whether a tangent frame is available at all, for the call sites that used to
// test ERHE_ATTRIBUTE_a_normal / ERHE_ATTRIBUTE_a_tangent directly. Under the
// quaternion encoding there IS no a_normal attribute, so those tests would
// answer "no normal" for a format that carries a complete frame. (a_tangent
// does still exist there - it is the slot the quaternion occupies - so the
// fragment side's ERHE_ATTRIBUTE_a_tangent gates stay correct as they are.)
#if ERHE_VERTEX_TBN_ENCODING == ERHE_VERTEX_TBN_ENCODING_QUATERNION16
#   define ERHE_HAS_VERTEX_NORMAL  1
#   define ERHE_HAS_VERTEX_TANGENT 1
#else
#   ifdef ERHE_ATTRIBUTE_a_normal
#       define ERHE_HAS_VERTEX_NORMAL  1
#   endif
#   ifdef ERHE_ATTRIBUTE_a_tangent
#       define ERHE_HAS_VERTEX_TANGENT 1
#   endif
#endif

// Object space tangent frame. handedness is the bitangent sign, i.e. what the
// float4 tangent's w component carries in the passthrough case:
//   bitangent = cross(normal, tangent) * handedness
void erhe_decode_vertex_tbn(out vec3 normal, out vec3 tangent, out float handedness)
{
#if ERHE_VERTEX_TBN_ENCODING == ERHE_VERTEX_TBN_ENCODING_QUATERNION16
    // a_tangent is ivec4: three snorm16 quaternion components scaled by
    // sqrt(2), cyclically swizzled away from the omitted largest one, plus a
    // control lane holding that component's index and the handedness bit.
    // See erhe::primitive::encode_tbn_quaternion() for the exact layout.
    const float inv_scale = 1.0 / (32767.0 * 1.4142135623730951);

    int control = a_tangent.w;
    int largest = control & 3;
    handedness  = ((control & 4) != 0) ? -1.0 : 1.0;

    float qa = float(a_tangent.x) * inv_scale;
    float qb = float(a_tangent.y) * inv_scale;
    float qc = float(a_tangent.z) * inv_scale;
    // The omitted component was the largest, so it was non-negative after the
    // encoder's canonicalization and the square root recovers it. The max()
    // guards the rounding case where the three stored components sum past 1.
    float qd = sqrt(max(0.0, 1.0 - qa * qa - qb * qb - qc * qc));

    vec4 q;
    q[(largest + 1) & 3] = qa;
    q[(largest + 2) & 3] = qb;
    q[(largest + 3) & 3] = qc;
    q[largest]           = qd;

    // Columns 0 and 2 of the rotation matrix of q: the encoder built it from
    // (tangent, bitangent, normal) in that column order.
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    tangent = vec3(1.0 - 2.0 * (yy + zz),       2.0 * (xy + wz),       2.0 * (xz - wy));
    normal  = vec3(      2.0 * (xz + wy),       2.0 * (yz - wx), 1.0 - 2.0 * (xx + yy));
#else
#   ifdef ERHE_ATTRIBUTE_a_normal
    normal = a_normal;
#   else
    normal = vec3(0.0, 0.0, 1.0);
#   endif
#   ifdef ERHE_ATTRIBUTE_a_tangent
    tangent    = a_tangent.xyz;
    handedness = a_tangent.w;
#   else
    tangent    = vec3(1.0, 0.0, 0.0);
    handedness = 1.0;
#   endif
#endif
}

#endif // ERHE_VERTEX_TBN_GLSL
