in vec2      v_texcoord;
in vec4      v_position;
in vec4      v_color;
in vec2      v_aniso_control;
in mat3      v_TBN;
in flat uint v_material_index;

const uint  max_u32 = 4294967295u;

vec4 sample_texture(uvec2 texture_handle, vec2 texcoord)
{
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return texture(s_texture, v_texcoord);
#else
    return texture(s_texture[texture_handle.x], v_texcoord);
#endif
}

vec4 sample_texture_lod_bias(uvec2 texture_handle, vec2 texcoord, float lod_bias)
{
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return texture(s_texture, texcoord, lod_bias);
#else
    return texture(s_texture[texture_handle.x], texcoord, lod_bias);
#endif
}

vec2 get_texture_size(uvec2 texture_handle)
{
    if (texture_handle.x == max_u32) {
        return vec2(1.0, 1.0);
    }
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return textureSize(s_texture, 0);
#else
    return textureSize(s_texture[texture_handle.x], 0);
#endif
}

const float m_pi   = 3.1415926535897932384626434;
const float m_i_pi = 0.3183098861837906715377675;

float ggx_isotropic_ndf(float N_dot_H, float alpha)
{
    float a = N_dot_H * alpha;
    float k = alpha / (1.0 - N_dot_H * N_dot_H + a * a);
    return k * k * m_i_pi;
}

float ggx_anisotropic_ndf(
    float alpha_t,
    float alpha_b,
    float T_dot_H,
    float B_dot_H,
    float N_dot_H
)
{
    vec3 v = vec3(
        alpha_b * T_dot_H,
        alpha_t * B_dot_H,
        alpha_t * alpha_b * N_dot_H
    );
    float v2 = dot(v, v);
    float a2 = alpha_t * alpha_b;
    float w2 = a2 / v2;
    return a2 * w2 * w2 * m_i_pi;
}

float ggx_isotropic_visibility(float N_dot_V, float N_dot_L, float alpha)
{
    float a2 = alpha * alpha;
    float GV = N_dot_L * sqrt(N_dot_V * N_dot_V * (1.0 - a2) + a2);
    float GL = N_dot_V * sqrt(N_dot_L * N_dot_L * (1.0 - a2) + a2);
    return 0.5 / (GV + GL);
}

float ggx_anisotropic_visibility(
    float alpha_t,
    float alpha_b,
	float T_dot_V,
    float B_dot_V,
    float N_dot_V,
    float T_dot_L,
    float B_dot_L,
    float N_dot_L
)
{
    float lambda_V = N_dot_L * length(vec3(alpha_t * T_dot_V, alpha_b * B_dot_V, N_dot_V));
    float lambda_L = N_dot_V * length(vec3(alpha_t * T_dot_L, alpha_b * B_dot_L, N_dot_L));
    float v = 0.5 / (lambda_V + lambda_L);
    return clamp(v, 0.0, 1.0);
}

vec3 fresnel_schlick(float cos_theta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

float clamped_dot(vec3 x, vec3 y)
{
    return clamp(dot(x, y), 0.001, 1.0);
}

vec3 brdf(
    vec3  base_color,
    float roughness_x,
    float roughness_y,
    float metallic,
    float reflectance,
    vec3  L,
    vec3  V,
    vec3  T,
    vec3  B,
    vec3  N
)
{
    float alpha_x = roughness_x * roughness_x;
    float alpha_y = roughness_y * roughness_y;
    vec3  F0      = 0.16 * reflectance * reflectance * (1.0 - metallic) + base_color * metallic;
    vec3  H       = normalize(L + V);
    float N_dot_H = clamped_dot(N, H);
    float N_dot_V = clamped_dot(N, V);
    float N_dot_L = clamped_dot(N, L);
    float T_dot_V = dot(T, V);
    float B_dot_V = dot(B, V);
    float T_dot_L = dot(T, L);
    float B_dot_L = dot(B, L);
    float T_dot_H = dot(T, H);
    float B_dot_H = dot(B, H);
    //float D       = ggx_isotropic_ndf       (N_dot_H, alpha_x);
    //float Vis     = ggx_isotropic_visibility(N_dot_V, N_dot_L, alpha_x);
    float D       = ggx_anisotropic_ndf       (alpha_x, alpha_y, T_dot_H, B_dot_H, N_dot_H);
    float Vis     = ggx_anisotropic_visibility(alpha_x, alpha_y, T_dot_V, B_dot_V, N_dot_V, T_dot_L, B_dot_L, N_dot_L);
    vec3  F                   = fresnel_schlick(max(dot(V, H), 0.0), F0);
    vec3  specular_microfacet = D * Vis * F;
    vec3  diffuse_lambert     = m_i_pi * (1.0 - metallic) * base_color;
    vec3  diffuse_factor      = vec3(1.0) - F;
    return N_dot_L * (diffuse_factor * diffuse_lambert + specular_microfacet);
}

float sample_light_visibility(vec4 position, uint light_index, float N_dot_L)
{
#if defined(ERHE_SHADOW_MAPS)
    if (light_block.shadow_texture.x == max_u32) {
        return 1.0;
    }

#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2DArray s_shadow = sampler2DArray(light_block.shadow_texture);
#endif

    Light light = light_block.lights[light_index];
    vec4  position_in_light_texture_homogeneous = light.texture_from_world * position;

    vec4  position_in_light_texture = position_in_light_texture_homogeneous / position_in_light_texture_homogeneous.w;
    float depth_in_light_texture    = position_in_light_texture.z;
    float sampled_depth = texture(
        s_shadow,
        vec3(
            position_in_light_texture.xy,
            float(light_index)
        )
    ).x;

    float bias = 0.005 * sqrt(1.0 - N_dot_L * N_dot_L) / N_dot_L; // tan(acos(N_dot_L))
    bias = clamp(bias, 0.0, 0.01);
    if (camera.cameras[0].clip_depth_direction < 0.0) {
        if (depth_in_light_texture + bias >= sampled_depth) { // reverse depth
            return 1.0;
        }
    } else {
        if (depth_in_light_texture - bias <= sampled_depth) { // forward depth
            return 1.0;
        }
    }
    return 0.0;
#else
    return 1.0;
#endif
}

float get_range_attenuation(float range, float distance)
{
    if (range <= 0.0) {
        return 1.0 / pow(distance, 2.0); // negative range means unlimited
    }
    return max(min(1.0 - pow(distance / range, 4.0), 1.0), 0.0) / pow(distance, 2.0);
}

float get_spot_attenuation(vec3 point_to_light, vec3 spot_direction, float outer_cone_cos, float inner_cone_cos)
{
    float actual_cos = dot(normalize(spot_direction), normalize(-point_to_light));
    if (actual_cos > outer_cone_cos) {
        if (actual_cos < inner_cone_cos) {
            return smoothstep(outer_cone_cos, inner_cone_cos, actual_cos);
        }
        return 1.0;
    }
    return 0.0;
}

float srgb_to_linear(float x)
{
    if (x <= 0.04045) {
        return x / 12.92;
    } else {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec3 srgb_to_linear(vec3 V)
{
    return vec3(
        srgb_to_linear(V.r),
        srgb_to_linear(V.g),
        srgb_to_linear(V.b)
    );
}

vec2 srgb_to_linear(vec2 V)
{
    return vec2(
        srgb_to_linear(V.r),
        srgb_to_linear(V.g)
    );
}

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  V  = normalize(view_position_in_world - v_position.xyz);
    vec3  T0 = normalize(v_TBN[0]); // Geometry tangent from vertex attribute
    vec3  B0 = normalize(v_TBN[1]); // Geometry bitangent from vertex attribute
    vec3  N  = normalize(v_TBN[2]);
    float N_dot_V = clamped_dot(N, V);

    Material material = material.materials[v_material_index];
    uvec2 base_color_texture         = material.base_color_texture;
    uvec2 metallic_roughness_texture = material.metallic_roughness_texture;
    vec3  base_color                 = v_color.rgb * material.base_color.rgb * sample_texture(base_color_texture, v_texcoord).rgb;

    // Generating circular anisotropy direction from texcoord
    vec2  T_circular                    = normalize(v_texcoord);
    float circular_anisotropy_magnitude = pow(length(v_texcoord) * 8.0, 0.25);
    // X is used to modulate anisotropy level:
    //   0.0 -- Anisotropic
    //   1.0 -- Isotropic when approaching texcoord (0, 0)
    // Y is used for tangent space selection/control:
    //   0.0 -- Use geometry T and B (from vertex attribute
    //   1.0 -- Use T and B derived from texcoord
    float anisotropy_strength = mix(
        1.0,
        min(1.0, circular_anisotropy_magnitude),
        v_aniso_control.y
    ) * v_aniso_control.x;
    // Mix tangent space geometric .. texcoord generated
    vec3  T                   = mix(T0, T_circular.x * T0 + T_circular.y * B0, v_aniso_control.y);
    vec3  B                   = mix(B0, T_circular.y * T0 - T_circular.x * B0, v_aniso_control.y);
    float isotropic_roughness = 0.5 * material.roughness.x + 0.5 * material.roughness.y;
    // Mix roughness based on anisotropy_strength
    float roughness_x         = mix(isotropic_roughness, material.roughness.x, anisotropy_strength);
    float roughness_y         = mix(isotropic_roughness, material.roughness.y, anisotropy_strength);

    uint directional_light_count  = light_block.directional_light_count;
    uint spot_light_count         = light_block.spot_light_count;
    uint point_light_count        = light_block.point_light_count;
    uint directional_light_offset = 0;
    uint spot_light_offset        = directional_light_count;
    uint point_light_offset       = spot_light_offset + spot_light_count;
    vec3 color = vec3(0);
    color += (0.5 + 0.5 * N.y) * light_block.ambient_light.rgb * base_color;
    color += material.emissive.rgb;
    for (uint i = 0; i < directional_light_count; ++i) {
        uint  light_index    = directional_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
        vec3  L              = normalize(point_to_light);   // Direction from surface point to light
        float N_dot_L        = clamped_dot(N, L);
        //color = vec3(N_dot_V);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            vec3 intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, N_dot_L);
            color += intensity * brdf(
                base_color,
                roughness_x,
                roughness_y,
                material.metallic,
                material.reflectance,
                L,
                V,
                T,
                B,
                N
            );
        }
    }

    for (uint i = 0; i < spot_light_count; ++i) {
        uint  light_index    = spot_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
        vec3  L              = normalize(point_to_light);
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
            float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
            float light_visibility  = sample_light_visibility(v_position, light_index, N_dot_L);
            vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb * light_visibility;
            color += intensity * brdf(
                base_color,
                roughness_x,
                roughness_y,
                material.metallic,
                material.reflectance,
                L,
                V,
                T,
                B,
                N
            );
        }
    }

    for (uint i = 0; i < point_light_count; ++i) {
        uint  light_index    = point_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
        vec3  L              = normalize(point_to_light);
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
            float light_visibility  = 1.0; // TODO sample_light_visibility(v_position, light_index, N_dot_L);
            vec3  intensity         = range_attenuation * light.radiance_and_range.rgb * light_visibility;
            color += intensity * brdf(
                base_color,
                roughness_x,
                roughness_y,
                material.metallic,
                material.reflectance,
                L,
                V,
                T,
                B,
                N
            );
        }
    }

    float exposure = camera.cameras[0].exposure;
    out_color.rgb = color * exposure * material.opacity;
    out_color.a = material.opacity;

    //out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * T0);

}
