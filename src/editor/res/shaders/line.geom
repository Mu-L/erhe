// uniform vec4 _viewport;
// uniform vec2 _line_width;

#define SHOW_DEBUG_LINES        0
#define PASSTHROUGH_BASIC_LINES 0
#define STRIP                   1

layout(lines) in;

#if SHOW_DEBUG_LINES || PASSTHROUGH_BASIC_LINES
layout(line_strip, max_vertices = 10) out;
#else
layout(triangle_strip, max_vertices = 6) out;
#endif

in vec4  vs_color[];
in float vs_line_width[];

out vec2  v_start;
out vec2  v_line;
out vec4  v_color;
out float v_l2;
out float v_line_width;

void main(void)
{
    //  a - - - - - - - - - - - - - - - - b
    //  |      |                   |      |
    //  |      |                   |      |
    //  |      |                   |      |
    //  | - - -start - - - - - - end- - - |
    //  |      |                   |      |
    //  |      |                   |      |
    //  |      |                   |      |
    //  d - - - - - - - - - - - - - - - - c

    vec4 start = gl_in[0].gl_Position;
    vec4 end   = gl_in[1].gl_Position;

#if PASSTHROUGH_BASIC_LINES
    gl_Position = start; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = end;   v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    EndPrimitive();
    return;
#endif

    // It is necessary to manually clip the line before homogenization.
    // Compute line start and end distances to nearplane in clipspace
    // Distances are t0 = dot(start, plane) and t1 = dot(end, plane)
    // If signs are not equal then clip
    float t0 = start.z + start.w;
    float t1 = end.z   + end.w;
    if (t0 < 0.0)
    {
        if (t1 < 0.0)
        {
            return;
        }
        start = mix(start, end, (0 - t0) / (t1 - t0));
    }
    if (t1 < 0.0)
    {
        end = mix(start, end, (0 - t0) / (t1 - t0));
    }


    vec2 vpSize         = view.viewport.zw;

    // Compute line axis and side vector in screen space
    vec2 startInNDC     = start.xy / start.w;       //  clip to NDC: homogenize and drop z
    vec2 endInNDC       = end.xy   / end.w;
    vec2 lineInNDC      = endInNDC - startInNDC;
    vec2 startInScreen  = (0.5 * startInNDC + vec2(0.5)) * vpSize + view.viewport.xy;
    vec2 endInScreen    = (0.5 * endInNDC   + vec2(0.5)) * vpSize + view.viewport.xy;
    vec2 lineInScreen   = lineInNDC * vpSize;       //  NDC to window (direction vector)
    vec2 axisInScreen   = normalize(lineInScreen);
    vec2 sideInScreen   = vec2(-axisInScreen.y, axisInScreen.x);    // rotate
    vec2 axisInNDC      = axisInScreen / vpSize;                    // screen to NDC
    vec2 sideInNDC      = sideInScreen / vpSize;
    vec4 axis_start     = vec4(axisInNDC, 0.0, 0.0) * vs_line_width[0];  // NDC to clip (delta vector)
    vec4 axis_end       = vec4(axisInNDC, 0.0, 0.0) * vs_line_width[1];  // NDC to clip (delta vector)
    vec4 side_start     = vec4(sideInNDC, 0.0, 0.0) * vs_line_width[0];
    vec4 side_end       = vec4(sideInNDC, 0.0, 0.0) * vs_line_width[1];

    vec4 a = (start + (side_start - axis_start) * start.w);
    vec4 b = (end   + (side_end   + axis_end  ) * end.w);
    vec4 c = (end   - (side_end   - axis_end  ) * end.w);
    vec4 d = (start - (side_start + axis_start) * start.w);

    v_start = startInScreen;
    v_line  = endInScreen - startInScreen;
    v_l2    = dot(v_line, v_line);

#if SHOW_DEBUG_LINES
    gl_Position = gl_in[0].gl_Position; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = gl_in[1].gl_Position; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    EndPrimitive();

    gl_Position = a; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = b; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    EndPrimitive();

    gl_Position = b; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    gl_Position = c; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    EndPrimitive();

    gl_Position = c; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    gl_Position = d; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    EndPrimitive();

    gl_Position = d; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = a; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    EndPrimitive();

#else

#if STRIP
#if 0
    gl_Position = d; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = a; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = c; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    gl_Position = b; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
#else
    gl_Position = a; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = d; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = b; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    gl_Position = c; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
#endif
    EndPrimitive();
#else
    gl_Position = d; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = a; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = c; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    EndPrimitive();
    gl_Position = c; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    gl_Position = a; v_color = vs_color[0]; v_line_width = vs_line_width[0]; EmitVertex();
    gl_Position = b; v_color = vs_color[1]; v_line_width = vs_line_width[1]; EmitVertex();
    EndPrimitive();
#endif

#endif
}
