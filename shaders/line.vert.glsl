#version 450

layout(push_constant) uniform constants
{
    mat4 mvp;
    vec4 color;
} push_const;

layout(location = 0) in vec3 in_position;
layout(location = 1) in uvec3 in_color; // intrinsic vertex color no longer used, but not hurting anything
layout(location = 0) out vec4 out_color;

void main()
{
    gl_Position = push_const.mvp * vec4(in_position, 1.0);
    out_color = push_const.color;
}
