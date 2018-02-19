#version 330
layout(location = 0) in vec3 position;
smooth out vec3 colorValue;
uniform mat4 projectionMat;
uniform vec3 fillColor;
uniform mat4 modelToCameraMat;
void main()
{
	vec4 full_pos = vec4(position, 1.0);
	gl_Position = projectionMat * (modelToCameraMat * full_pos);
	colorValue = fillColor;
}
