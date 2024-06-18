#version 450

layout(std140, binding = 0) uniform buf {
    vec4 color;
};

vec2 positions[] = {
    vec2(0, 0),
    vec2(1, 0),
    vec2(0, 1),
    vec2(1, 1),
};

void main() {

    vec2 position = positions[gl_VertexIndex];

    position *= 2;
    position -= 1;

    /*
    // Flip vertically
    position.y = 1 - position.y;

    // Reproject onto the [-1,1] canvas
    position.x -= 0.5;
    position.y -= 0.5;
    position.x *= 2;
    position.y *= 2;
    */

    gl_Position = vec4(position, 0, 1.0);
}
