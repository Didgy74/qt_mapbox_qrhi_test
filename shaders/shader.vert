#version 450

// Input data is roughly in range [0, 4096]
layout(location = 0) in vec2 positionIn;

layout(column_major, std140, binding = 0) uniform UniformBuff {
    mat4 matrix;
    vec4 color;
};

layout(location = 0) out vec2 normalizedPos;

void main() {
    vec2 pos2 = positionIn;
    // Normalize to [0, 1]
    pos2 /= 4096;

    normalizedPos = pos2;

    // Flips vertically
    pos2.y = 1 - pos2.y;
    // Center the tile
    pos2 -= 0.5;

    vec4 pos4 = matrix * vec4(pos2, 0, 1);

    gl_Position = vec4(pos4.xy, 0, 1);
}
