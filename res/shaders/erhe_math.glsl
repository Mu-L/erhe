#ifndef ERHE_MATH_GLSL
#define ERHE_MATH_GLSL

const float m_pi    = 3.1415926535897932384626434;
const float m_i_pi  = 0.3183098861837906715377675;

float clamped_dot(vec3 x, vec3 y) {
    return clamp(dot(x, y), 0.001, 1.0);
}

float heaviside(float v) {
    if (v > 0.0) {
        return 1.0;
    } else {
        return 0.0;
    }
}

// Largest value a 16-bit float render target can represent. Lighting can
// legitimately evaluate to far more than this - the GGX NDF alone reaches
// ~1e8 at the centre of a low-roughness highlight - and an fp16 colour
// attachment stores anything larger as +Inf, which leaves every later
// consumer (blending, multisample resolve, the tonemap in compose.frag)
// operating on a non-finite value. Shaders that write lit colour should
// emit something the attachment can actually hold.
const float m_half_max = 65504.0;

bool is_nan(float v) {
    return (v < 0.0 || 0.0 < v || v == 0.0) ? false : true;
}

// NaN-safe clamp into the non-negative range [0, max_value]. min() and max()
// are not required to handle NaN consistently across implementations, so NaN is
// tested for explicitly instead of being relied on to fall out of them.
float to_render_target_range(float v, float max_value) {
    return is_nan(v) ? 0.0 : min(max(v, 0.0), max_value);
}

vec3 to_render_target_range(vec3 v, float max_value) {
    return vec3(
        to_render_target_range(v.r, max_value),
        to_render_target_range(v.g, max_value),
        to_render_target_range(v.b, max_value)
    );
}

#endif // ERHE_MATH_GLSL
