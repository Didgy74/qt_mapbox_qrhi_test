#version 450

layout(std140, binding = 0) uniform buf {
    vec2 posOffset;
    vec4 color;
};

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = color;
}
