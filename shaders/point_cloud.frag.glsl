#version 450

layout(location = 0) in vec3 in_color;
layout(location = 0) out vec4 out_color;

vec3 srgb_to_linear(vec3 srgb)
{
    return mix(srgb / 12.92, pow((srgb + 0.055) / 1.055, vec3(2.4)), step(0.04045, srgb));
}

void main()
{
    out_color = vec4(srgb_to_linear(in_color), 1.0);
}
