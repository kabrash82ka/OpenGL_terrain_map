#version 330
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
smooth out vec3 outputColor;
smooth out vec2 colorCoord;
uniform mat4 perspectiveMatrix;
uniform mat4 modelToCameraMatrix;
uniform vec3 lightDir;
const vec3 lightIntensity = vec3(1.0, 1.0, 1.0);
const vec3 ambientIntensity = vec3(1.0, 1.0, 1.0);

void main()
{
	vec4 full_pos = vec4(position.xyz, 1.0);
	gl_Position = perspectiveMatrix * (modelToCameraMatrix * full_pos);
	vec3 surfaceNormal = normal;
	float cosAngIncidence = dot(surfaceNormal, lightDir);
	cosAngIncidence = clamp(cosAngIncidence, 0, 1);
	vec3 diffuseColor = vec3(1.0, 1.0, 1.0);
	outputColor = (diffuseColor * lightIntensity * cosAngIncidence) + (diffuseColor * ambientIntensity);
	colorCoord = texCoord;
}
