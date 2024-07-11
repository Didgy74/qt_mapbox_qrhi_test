#version 450

layout(column_major, std140, binding = 0) uniform buf {
    mat4 matrix;
    vec4 color;
};

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec2 normalizedPos;

void main() {
    if (normalizedPos.x < 0 || normalizedPos.x > 1 ||
        normalizedPos.y < 0 || normalizedPos.y > 1)
    {
        discard;
    }

    fragColor = color;
}
