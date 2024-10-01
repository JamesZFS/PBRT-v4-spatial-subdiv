#version 410

in vec2 pos;
in vec2 uv;

out vec2 tex_coord;

void main() {
    gl_Position = vec4(pos.xy, 0, 1);
    tex_coord = uv;
}