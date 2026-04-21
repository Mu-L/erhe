#ifndef ERHE_TEXTURE_GLSL
#define ERHE_TEXTURE_GLSL

const uint max_u32 = 4294967295u;

vec4 sample_texture(uvec2 texture_handle, vec2 texcoord) {
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
#if defined(ERHE_TEXTURE_HEAP_OPENGL_BINDLESS)
    sampler2D s_texture = sampler2D(texture_handle);
    vec4 v = texture(s_texture, texcoord);
#elif defined(ERHE_TEXTURE_HEAP_VULKAN_DESCRIPTOR_INDEXING)
    vec4 v = texture(erhe_texture_heap[texture_handle.x], texcoord);
#elif defined(ERHE_TEXTURE_HEAP_METAL_ARGUMENT_BUFFER)
    vec4 v = texture(s_texture[texture_handle.x], texcoord);
#elif defined(ERHE_TEXTURE_HEAP_OPENGL_SAMPLER_ARRAY)
    vec4 v = texture(s_texture[texture_handle.x], texcoord);
#else
    vec4 v = vec4(1.0, 0.0, 1.0, 1.0);
#endif
    return v;
}

vec4 sample_texture(uvec2 texture_handle, vec2 texcoord, vec4 rotation_scale, vec2 offset) {
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
    vec2 transformed_texcoord = mat2(rotation_scale.xy, rotation_scale.zw) * texcoord + offset;
#if defined(ERHE_TEXTURE_HEAP_OPENGL_BINDLESS)
    sampler2D s_texture = sampler2D(texture_handle);
    vec4 v = texture(s_texture, transformed_texcoord);
#elif defined(ERHE_TEXTURE_HEAP_VULKAN_DESCRIPTOR_INDEXING)
    vec4 v = texture(erhe_texture_heap[texture_handle.x], transformed_texcoord);
#elif defined(ERHE_TEXTURE_HEAP_METAL_ARGUMENT_BUFFER)
    vec4 v = texture(s_texture[texture_handle.x], transformed_texcoord);
#elif defined(ERHE_TEXTURE_HEAP_OPENGL_SAMPLER_ARRAY)
    vec4 v = texture(s_texture[texture_handle.x], transformed_texcoord);
#else
    vec4 v = vec4(1.0, 0.0, 1.0, 1.0);
#endif
    return v;
}

vec4 sample_texture_lod_bias(uvec2 texture_handle, vec2 texcoord, float lod_bias) {
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
#if defined(ERHE_TEXTURE_HEAP_OPENGL_BINDLESS)
    sampler2D s_texture = sampler2D(texture_handle);
    vec4 v = texture(s_texture, texcoord, lod_bias);
#elif defined(ERHE_TEXTURE_HEAP_VULKAN_DESCRIPTOR_INDEXING)
    vec4 v = texture(erhe_texture_heap[texture_handle.x], texcoord, lod_bias);
#elif defined(ERHE_TEXTURE_HEAP_METAL_ARGUMENT_BUFFER)
    vec4 v = texture(s_texture[texture_handle.x], texcoord, lod_bias);
#elif defined(ERHE_TEXTURE_HEAP_OPENGL_SAMPLER_ARRAY)
    vec4 v = texture(s_texture[texture_handle.x], texcoord, lod_bias);
#else
    vec4 v = vec4(1.0, 0.0, 1.0, 1.0);
#endif
    return v;
}

vec2 get_texture_size(uvec2 texture_handle) {
    if (texture_handle.x == max_u32) {
        return vec2(1.0, 1.0);
    }
#if defined(ERHE_TEXTURE_HEAP_OPENGL_BINDLESS)
    sampler2D s_texture = sampler2D(texture_handle);
    vec2 v = textureSize(s_texture, 0);
#elif defined(ERHE_TEXTURE_HEAP_VULKAN_DESCRIPTOR_INDEXING)
    vec2 v = textureSize(erhe_texture_heap[texture_handle.x], 0);
#elif defined(ERHE_TEXTURE_HEAP_METAL_ARGUMENT_BUFFER)
    vec2 v = textureSize(s_texture[texture_handle.x], 0);
#elif defined(ERHE_TEXTURE_HEAP_OPENGL_SAMPLER_ARRAY)
    vec2 v = textureSize(s_texture[texture_handle.x], 0);
#else
    vec2 v = vec2(0.0, 0.0);
#endif
    return v;
}

#endif // ERHE_TEXTURE_GLSL
