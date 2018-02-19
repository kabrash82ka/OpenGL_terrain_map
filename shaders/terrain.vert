#version 330
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
smooth out vec4 outputColor;
smooth out vec2 colorCoord;
uniform mat4 perspectiveMatrix;
uniform mat4 modelToCameraMatrix;
uniform vec3 lightDir;
const vec4 lightIntensity = vec4(1.0, 1.0, 1.0, 1.0);
const vec4 ambientIntensity = vec4(0.05, 0.05, 0.05, 1.0);

void main()
{
	vec4 full_pos = vec4(position.xyz, 1.0);
	gl_Position = perspectiveMatrix * (modelToCameraMatrix * full_pos);
	vec3 surfaceNormal = normal;
	
	//draw terrain a brighter color if above the waterline of y==0.0
	vec3 elevationColor = position.y >= 0.0 ? vec3(1.0, 1.0, 1.0) : vec3(0.5, 0.5, 0.5);
	float cosAngIncidence = dot(surfaceNormal, lightDir);
	cosAngIncidence = clamp(cosAngIncidence, 0, 1);
	vec4 diffuseColor = vec4(elevationColor, 1.0);
	outputColor = (diffuseColor * lightIntensity * cosAngIncidence) + (diffuseColor * ambientIntensity);
	colorCoord = texCoord;
}
