in vec2 v_texcoord;

float fix(float value)
{
    return isnan(value) || isinf(value)
        ? 0.0
        : value;
}

vec3 fix(vec3 value)
{
    return vec3(
        fix(value.r),
        fix(value.g),
        fix(value.b)
    );
}

vec3 brightness(vec3 c)
{
    return vec3(max(c.r, max(c.g, c.b)));
}

// 3-tap median filter
vec3 median(vec3 a, vec3 b, vec3 c)
{
    return a + b + c - min(min(a, b), c) - max(max(a, b), c);
}

void main()
{
    sampler2D s_source = sampler2D(post_processing.source_texture[0]);

    float offset_1 = 1.43750000 * post_processing.texel_scale;
    float offset_2 = 3.31250000 * post_processing.texel_scale;
    float offset_3 = 5.18750000 * post_processing.texel_scale;
    float offset_4 = 7.06250000 * post_processing.texel_scale;
    float weight_0 = 0.19638062;
    float weight_1 = 0.34912109;
    float weight_2 = 0.13330078;
    float weight_3 = 0.01708984;
    float weight_4 = 0.00048828;

    vec3 prev_4 = weight_4 * texture(s_source, v_texcoord - vec2(offset_4, 0.0)).rgb;
    vec3 prev_3 = weight_3 * texture(s_source, v_texcoord - vec2(offset_3, 0.0)).rgb;
    vec3 prev_2 = weight_2 * texture(s_source, v_texcoord - vec2(offset_2, 0.0)).rgb;
    vec3 prev_1 = weight_1 * texture(s_source, v_texcoord - vec2(offset_1, 0.0)).rgb;
    vec3 center = weight_0 * texture(s_source, v_texcoord).rgb;
    vec3 next_1 = weight_1 * texture(s_source, v_texcoord + vec2(offset_1, 0.0)).rgb;
    vec3 next_2 = weight_2 * texture(s_source, v_texcoord + vec2(offset_2, 0.0)).rgb;
    vec3 next_3 = weight_3 * texture(s_source, v_texcoord + vec2(offset_3, 0.0)).rgb;
    vec3 next_4 = weight_3 * texture(s_source, v_texcoord + vec2(offset_4, 0.0)).rgb;
    prev_4 = fix(prev_4);
    prev_3 = fix(prev_3);
    prev_2 = fix(prev_2);
    prev_1 = fix(prev_1);
    center = fix(center);
    next_1 = fix(next_1);
    next_2 = fix(next_2);
    next_3 = fix(next_3);
    next_4 = fix(next_4);
    out_color.rgb  = fix(prev_4 + prev_3 + prev_2 + prev_1 + center + next_1 + next_2 + next_3 + next_4);
    //out_color.rgb  = prev_3 + prev_2 + prev_1 + center + next_1 + next_2 + next_3;
    //out_color.rgb  = prev_2 + prev_1 + center + next_1 + next_2;
    //out_color.rgb  = prev_1 + center + next_1;
    //out_color.rgb  = center;
    out_color.a    = 1.0;
}

    //float offset_1 = 1.46153846 * post_processing.texel_scale;
    //float offset_2 = 3.30769231 * post_processing.texel_scale;
    //float offset_3 = 5.15384615 * post_processing.texel_scale;
    //float weight_0 = 0.19345383 * 0.5;
    //float weight_1 = 0.41914998 * 0.5;
    //float weight_2 = 0.17464582 * 0.5;
    //float weight_3 = 0.01905227 * 0.5;
