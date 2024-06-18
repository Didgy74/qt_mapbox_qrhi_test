#version 450

layout(location = 0) in vec2 positionIn;

layout(std140, binding = 0) uniform buf {
    vec2 posOffset;
    vec4 color;
};

void main() {
    vec2 position = positionIn;
    // Normalize
    position /= 4096;
    position.y = 1 - position.y;
    position *= 2;
    position -= 1;

    /*
    // Flip vertically


    // Reproject onto the [-1,1] canvas
    position.x -= 0.5;
    position.y -= 0.5;
    position.x *= 2;
    position.y *= 2;
    */

    gl_Position = vec4(position + posOffset, 0.1, 1.0);
}
