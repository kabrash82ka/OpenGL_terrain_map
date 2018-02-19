#version 330
smooth in vec4 outputColor;
smooth in vec2 colorCoord;
out vec4 fragColor;
uniform sampler2D colorTexture;

void main()
{
	fragColor = outputColor * texture(colorTexture, colorCoord);
}
