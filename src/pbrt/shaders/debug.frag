#version 410

// Output for on-screen color
out vec4 out_color;
in vec2 tex_coord;

void main()
{
//    out_color = vec4(1.0, 0.0, 0.0, 1.0);
    out_color = vec4(tex_coord, 0.0, 1.0);
}