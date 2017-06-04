#version 440

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    v_texcoord = texcoord;
    gl_Position = pc.mvp * position;
}
