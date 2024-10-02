#version 410

// Output for on-screen color
out vec4 out_color;

uniform sampler2D image_tex;
uniform sampler2D cmap_tex;  // 1D tonemapping

uniform float scale;
uniform float offset;
uniform float clip_val;
uniform int single_channel;
uniform int tonemapped;

in vec2 tex_coord;

float luminance(vec3 color) {
	return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

void main()
{
	vec3 color = texture(image_tex, tex_coord).rgb;
	float val = single_channel == 1 ? color.r : luminance(color);
	// Linear transformation
	color = scale * (color + offset);

	if (tonemapped == 0) {  // no tonemapping
		if (single_channel == 1)
			out_color = vec4(color.r, color.r, color.r, 1);
		else
			out_color = vec4(color, 1);
		out_color = pow(out_color, vec4(1.0 / 2.2));  // gamma correction
	}
	else {  // tonemapping
		float u;  // Valid range: [0, 1]
		if (single_channel == 1)
			u = color.r;
		else
			u = luminance(color);
		out_color = texture(cmap_tex, vec2(u, 0));
		if (val > clip_val) {
			out_color = vec4(vec3(luminance(out_color.rgb) * 0.2), 1);  // desaturate
		}
	}
}