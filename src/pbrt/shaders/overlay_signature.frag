#version 410

// Output for on-screen color
out vec4 out_color;

uniform sampler2D image_tex;

uniform uint show_vmf;  // 0: disable, 1: show mean dirs, 2: show kappa, 3: show 95% confidence interval
uniform vec3 mean_dir1;
uniform vec3 mean_dir2;
uniform float kappa1;
uniform float kappa2;
uniform float sigma1;
uniform float sigma2;

#define INVALID 255 // (uint8_t) -1
#define PI 3.14159265359
#define CENTER_SIZE (1.5 / 180.0 * PI)

in vec2 tex_coord;  // [0, 1]

void main()
{
	vec3 color = texture(image_tex, tex_coord).rgb;
	if (show_vmf != 0) {
		color = clamp(color, 0.0, 1.0);
		float theta = PI * tex_coord.y;
		float phi = 2 * PI * tex_coord.x;
		vec3 direction = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
		float dot1 = dot(mean_dir1, direction);
		float dot2 = dot(mean_dir2, direction);
		vec3 c1 = vec3(41, 123, 255) / 255.0;
		vec3 c2 = vec3(255, 106, 0) / 255.0;

		if (show_vmf == 2) {
			// Show concentration
			float blend1 = exp(kappa1 * dot1);
			float blend2 = exp(kappa2 * dot2);
			if (kappa1 * dot1 > kappa1 - 2) {
				float bmin = exp(max(-kappa1, kappa1 - 2));
				float bmax = exp(kappa1);
				blend1 = (blend1 - bmin) / (bmax - bmin + 1e-6);
			} else {
				blend1 = 0;
			}
			if (kappa2 * dot2 > kappa2 - 2) {
				float bmin = exp(max(-kappa2, kappa2 - 2));
				float bmax = exp(kappa2);
				blend2 = (blend2 - bmin) / (bmax - bmin + 1e-6);
			} else {
				blend2 = 0;
			}
			float b = max(blend1, blend2);
			if (b > 0)
				color = mix(color, (c1 * blend1 + c2 * blend2) / (blend1 + blend2), b);
		} else if (show_vmf == 3) {
			// Show 99.99% confidence interval
			float blend1 = 0;
			float blend2 = 0;
			if (dot1 > sqrt(1 + log(0.001) * sigma1*sigma1)) {
				blend1 = 0.5;
			}
			if (dot2 > sqrt(1 + log(0.001) * sigma2*sigma2)) {
				blend2 = 0.5;
			}
			float b = max(blend1, blend2);
			if (b > 0)
				color = mix(color, (c1 * blend1 + c2 * blend2) / (blend1 + blend2), b);
		}

		// Draw centers
		if (dot1 > cos(CENTER_SIZE)) {
			color = c1;
			if (dot2 > cos(CENTER_SIZE)) color += c2;
		}
		else if (dot2 > cos(CENTER_SIZE)) color = c2;

	}
	out_color = vec4(color, 1.0);
}