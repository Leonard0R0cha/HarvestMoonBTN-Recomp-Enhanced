#version 450
/* The overlay: the vertex colour times the font texture */

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D Font;

void main()
{
    FragColor = vColor * texture(Font, vUV);
}
