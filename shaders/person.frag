#version 330
smooth in vec3 vertex_normal;
smooth in vec3 modelspace_pos;
smooth in vec2 colorCoord;
out vec4 outputColor;
const vec3 specularColor = vec3(0.2, 0.2, 0.2);
const vec3 lightIntensity = vec3(0.5, 0.5, 0.5);
const vec3 ambientIntensity = vec3(0.5, 0.5, 0.5);
const float shininessFactor = 1.0;
const vec3 lightDir = vec3(0.0, 1.0, 0.0);
uniform vec3 modelSpaceCameraPos;
uniform sampler2D colorTexture;
void main()
{
	vec3 surfaceNormal = normalize(vertex_normal);
	float cosAngIncidence = dot(surfaceNormal, lightDir);
	cosAngIncidence = clamp(cosAngIncidence, 0, 1);
	vec3 viewDirection = normalize(modelSpaceCameraPos - modelspace_pos);
	vec3 halfAngleDir = normalize(lightDir + viewDirection);
	float blinnTerm = dot(surfaceNormal, halfAngleDir);
	blinnTerm = clamp(blinnTerm, 0, 1);
	blinnTerm = cosAngIncidence != 0.0 ? blinnTerm : 0.0;
	blinnTerm = pow(blinnTerm, shininessFactor);
	vec4 texColor = texture(colorTexture, colorCoord);
	vec3 finalColor = (texColor.rgb * lightIntensity * cosAngIncidence)
		+ (specularColor * blinnTerm)
		+ (texColor.rgb * ambientIntensity);
	outputColor = vec4(finalColor, 1.0);
}
