#version 410

// Output for on-screen color
out vec4 out_color;

uniform sampler2D image_tex;
uniform usampler2D index_map;

uniform uint selected_index;

in vec2 tex_coord;

void main()
{
	vec3 color = texture(image_tex, tex_coord).rgb;
	uint index = texture(index_map, tex_coord).r;
	if (index == selected_index)
		color = mix(color, vec3(1.0, 0.0, 0.0), 0.5);  // tint red
	out_color = vec4(color, 1.0);
}