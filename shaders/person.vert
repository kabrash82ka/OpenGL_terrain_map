#version 330
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 norm;
layout(location = 2) in vec2 texcoord;
smooth out vec3 vertex_normal;
smooth out vec3 modelspace_pos;
smooth out vec2 colorCoord;
uniform mat4 perspectiveMatrix;
uniform mat4 modelToCameraMatrix;
void main()
{
	vec4 full_pos = vec4(pos, 1.0);
	gl_Position = perspectiveMatrix * (modelToCameraMatrix * full_pos);
	modelspace_pos = pos;
	vertex_normal = norm;
	colorCoord = texcoord;
}
