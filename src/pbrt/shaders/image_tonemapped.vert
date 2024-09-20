#version 410

uniform vec2 lower_left, upper_right;  // target region coordinates on the screen
uniform vec2 window_size;       // window resolution

in vec3 pos; // World-space position
in vec2 uv;

out vec2 tex_coord;

void main() {
    tex_coord = uv;
    gl_Position = vec4((upper_right - lower_left) / window_size * pos.xy + (upper_right + lower_left) / window_size - 1, 0, 1);
}