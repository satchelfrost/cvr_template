#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec3 in_color;

layout(push_constant) uniform constants
{
    vec4 color;
    vec4 pos_width;
} push_const;

layout(location = 0) out vec4 out_color;

void main()
{
    vec2 norm = in_pos*0.5f + 0.5f;

    float width  = push_const.pos_width.z;
    float height = push_const.pos_width.w;
    float norm_width  = width/400.0f;
    float norm_height = height/400.0f;
    norm.x *= norm_width;
    norm.y *= norm_height;

    vec2 pix_pos = norm*vec2(400.0f, 400.0f) + vec2(push_const.pos_width.x, push_const.pos_width.y);
    pix_pos.x /= 400.0f;
    pix_pos.y /= 400.0f;
    pix_pos.y = pix_pos.y*2.0f - 1.0f;
    pix_pos.x = pix_pos.x*2.0f - 1.0f;
    gl_Position = vec4(pix_pos, 0.0, 1.0);
    out_color = push_const.color;
}
