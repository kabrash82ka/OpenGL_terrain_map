#version 330
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
smooth out vec2 colorCoord;
smooth out vec3 colorValue;
uniform mat4 projectionMat;
uniform vec3 fillColor;
uniform mat4 modelToCameraMat;
uniform vec2 texCoordOffset;
void main()
{
	gl_Position = projectionMat * (modelToCameraMat * vec4(position, 1.0));
	colorCoord = texCoord + texCoordOffset;
	colorValue = fillColor;
}
