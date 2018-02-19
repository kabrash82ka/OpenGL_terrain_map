#version 330
smooth in vec3 colorValue;
out vec4 fragColor;
void main()
{
	fragColor = vec4(colorValue, 1.0);
}
