#version 410

out vec4 out_color;
in vec2 tex_coord;

void main()
{
    out_color = vec4(tex_coord, 0.0, 1.0);
//    float pi = 3.14159265359;
//    float val = sin(tex_coord.x * 2 * 500);
//    out_color = vec4(val,val,val, 1.0);
}