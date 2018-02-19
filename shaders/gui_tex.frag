#version 330
smooth in vec2 colorCoord;
smooth in vec3 colorValue;
out vec4 fragColor;
uniform sampler2D colorTexture;
void main()
{
	vec4 full_colorValue = vec4(colorValue, 1.0);
	fragColor = full_colorValue*texture(colorTexture, colorCoord);
}
