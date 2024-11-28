#version 410

// Output for on-screen color
out vec4 out_color;

uniform sampler2D image_tex;  // (tonemapped) image
uniform sampler2D id_tex;  // lookahead ID (rgb)

ivec2 tex_size = textureSize(image_tex, 0);
vec2 delta = 1.0 / vec2(tex_size);
in vec2 tex_coord;

void main()
{
	vec3 color = texture(image_tex, tex_coord).rgb;

	vec3 id = texture(id_tex, tex_coord).rgb;
	vec3 id_x_n = texture(id_tex, tex_coord - vec2(1, 0) * delta).rgb;
	vec3 id_x_p = texture(id_tex, tex_coord + vec2(1, 0) * delta).rgb;
	vec3 id_y_n = texture(id_tex, tex_coord - vec2(0, 1) * delta).rgb;
	vec3 id_y_p = texture(id_tex, tex_coord + vec2(0, 1) * delta).rgb;

	vec3 dx = id_x_p - 2 * id + id_x_n;
	vec3 dy = id_y_p - 2 * id + id_y_n;

	float grad = dot(dx, dx) + dot(dy, dy);

	int ix = int(tex_coord.x * tex_size.x);
	int iy = int(tex_coord.y * tex_size.y);

	id = pow(id, vec3(1.0 / 2.2));  // gamma correction
	if (grad > 0 && (ix / 5 + iy / 5) % 2 == 0)  // checkerboard pattern
		out_color = vec4(id, 1.0);
	else
		out_color = vec4(color, 1.0);
}