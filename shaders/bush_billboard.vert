#version 330
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
smooth out vec3 outputColor;
smooth out vec2 colorCoord;
uniform mat4 perspectiveMatrix;
uniform mat4 modelToCameraMatrix;
uniform vec2 billboardSize;
const vec3 ambientIntensity = vec3(1.0, 1.0, 1.0);

void main()
{
	vec4 full_pos = vec4(position.xyz, 1.0);
	vec4 pos_in_cameraspace = modelToCameraMatrix * full_pos;
	//use the texture coordinate to place where the final vertex should go
	pos_in_cameraspace.x += (texCoord.x - 0.5)*billboardSize.x; //bias the x coord so that it centers the billboard
	pos_in_cameraspace.y += texCoord.y*billboardSize.y;
	gl_Position = perspectiveMatrix * pos_in_cameraspace;
	vec3 diffuseColor = vec3(1.0, 1.0, 1.0);
	outputColor = diffuseColor * ambientIntensity;
	colorCoord = texCoord;
}
