#version 330
layout(location = 0) in vec3 position;
smooth out vec3 modelSpacePosition;
uniform mat4 perspectiveMatrix;
uniform mat4 modelToCameraMatrix;
uniform vec3 modelSpaceCameraPos;

void main()
{
	vec4 full_pos = vec4(position.xyz, 1.0);
	full_pos.x = full_pos.x + modelSpaceCameraPos.x; //move the ocean plane to be around the camera
	full_pos.z = full_pos.z + modelSpaceCameraPos.z;
	modelSpacePosition = full_pos.xyz;
	gl_Position = perspectiveMatrix * (modelToCameraMatrix * full_pos);
}
