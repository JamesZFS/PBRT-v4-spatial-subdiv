#version 410

uniform vec2 cursor_min, cursor_max;  // target region coordinates on the screen
uniform vec2 window_size;       // window resolution

in vec3 pos; // World-space position
in vec2 uv;

out vec2 tex_coord;

void main() {
//    gl_Position = vec4(pos, 1);
    tex_coord = uv;
    gl_Position = vec4((cursor_max - cursor_min) / window_size * pos.xy + (cursor_max + cursor_min) / window_size - 1, 0, 1);
}