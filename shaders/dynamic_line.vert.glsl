#version 450

layout(push_constant) uniform constants
{
    mat4 mvp;
    vec4 color;
    vec4 start;
    vec4 end;
} push_const;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 position = (gl_VertexIndex > 0) ? push_const.end : push_const.start;
    gl_Position = push_const.mvp * position;
    out_color = push_const.color;
}
