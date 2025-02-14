#version 410

// Output for on-screen color
out vec4 out_color;

uniform sampler2D image_tex;
uniform sampler2D basis_map;
uniform uint selected_bin_index;

in vec2 tex_coord;

void main()
{
	vec3 color = texture(image_tex, tex_coord).rgb;
	if (selected_bin_index < 8) {
		float mask = texture(basis_map, tex_coord).r;
		color = mix(color, vec3(1.0, 0.0, 0.0), mask);  // tint red
	}
	out_color = vec4(color, 1.0);
}