#version 410

// Output for on-screen color
out vec4 out_color;

uniform sampler2D image_tex;
uniform sampler2D cmap_tex;  // 1D tonemapping

uniform float scale;
uniform float offset;
uniform int mode;
uniform int single_channel;

in vec2 tex_coord;

float luminance(vec3 color) {
	return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

void main()
{
	vec2 uv = vec2(tex_coord.x, 1 - tex_coord.y);
	vec3 color = texture(image_tex, uv).rgb;
	// Linear transformation
	color = scale * (color + offset);

	if (mode == 1) {  // no tonemapping
		if (single_channel == 1)
			out_color = vec4(color.r, color.r, color.r, 1);
		else
			out_color = vec4(color, 1);
	}
	else if (mode == 2) {  // tonemapping
		float val;
		if (single_channel == 1)
			val = color.r;
		else
			val = luminance(color);
		out_color = texture(cmap_tex, vec2(val, 0));
	}
	else {  // error
		out_color = vec4(1, 0, 1, 1);
	}
}