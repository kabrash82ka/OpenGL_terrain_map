#version 330
smooth in vec3 outputColor;
smooth in vec2 colorCoord;
out vec4 fragColor;
uniform sampler2D colorTexture;

void main()
{
	vec4 sampledColor = (vec4(outputColor,1.0))*texture(colorTexture, colorCoord);
	if(sampledColor.a == 0.0)
		discard;
	else
		fragColor = sampledColor;
}
