layout(location = 0) out vec2      v_texcoord;
layout(location = 1) out vec4      v_position;
layout(location = 2) out vec4      v_color;
layout(location = 3) out vec2      v_aniso_control;
layout(location = 4) out mat3      v_TBN;
layout(location = 7) out flat uint v_draw_id;

void main()
{
    mat4 world_from_node;
    mat4 world_from_node_normal;

    if (primitive.primitives[gl_DrawID].skinning_factor < 0.5) {
        world_from_node        = primitive.primitives[gl_DrawID].world_from_node;
        world_from_node_normal = primitive.primitives[gl_DrawID].world_from_node_normal;
    } else {
        world_from_node =
            a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x)].world_from_bind +
            a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y)].world_from_bind +
            a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z)].world_from_bind +
            a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w)].world_from_bind;
        world_from_node_normal =
            a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x)].world_from_bind_normal +
            a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y)].world_from_bind_normal +
            a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z)].world_from_bind_normal +
            a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w)].world_from_bind_normal;
    }

    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal,      0.0)));
    vec3 tangent         = normalize(vec3(world_from_node        * vec4(a_tangent.xyz, 0.0)));
    vec3 bitangent       = normalize(cross(normal, tangent)) * a_tangent.w;
    vec4 position        = world_from_node * vec4(a_position, 1.0);

    v_TBN            = mat3(tangent, bitangent, normal);
    v_position       = position;
    gl_Position      = clip_from_world * position;
    v_draw_id        = gl_DrawID;
    v_texcoord       = a_texcoord_0;
    v_color          = a_color_0;
    v_aniso_control  = a_custom_1; //aniso_control;
}
