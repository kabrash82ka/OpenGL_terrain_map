#version 330
smooth in vec3 modelSpacePosition;
out vec4 outputColor;
uniform vec3 lightDir;
uniform float shininessFactor;
uniform vec3 modelSpaceCameraPos;
const vec3 surfaceNormal = vec3(0.0, 1.0, 0.0);
const vec4 specularColor = vec4(1.0, 1.0, 1.0, 1.0);
const vec4 refractBaseColor = vec4(0.003921569, 0.215686275, 0.2, 1.0);
const vec4 reflectColorStart = vec4(0.850980392,0.968627451,0.984313725, 1.0);
const vec4 reflectColorEnd = vec4(0.0,0.91372549,0.925490196, 1.0);
const vec4 white = vec4(1.0, 1.0, 1.0, 1.0);

void CalcFresnel(in float theta, out float reflectCoeff, out float refractCoeff)
{
	float r0 = 0.020408163;
	float ir0 = 0.979591837;
	float cosTerm = 1.0 - theta;
	cosTerm = pow(cosTerm, 5.0);
	float tReflCoeff = r0 + (ir0*cosTerm);
	reflectCoeff = tReflCoeff;
	refractCoeff = 1.0 - reflectCoeff;
}

void main()
{
	float cosAngIncidence = dot(surfaceNormal, lightDir);
	cosAngIncidence = clamp(cosAngIncidence, 0, 1);
	vec3 viewDirection = normalize(modelSpaceCameraPos - modelSpacePosition);
	vec3 viewIncidentVec = normalize(modelSpacePosition - modelSpaceCameraPos);
	vec3 reflectionVec = reflect(viewIncidentVec, surfaceNormal);
	float cosAngReflect = dot(reflectionVec, surfaceNormal);
	cosAngReflect = clamp(cosAngReflect, 0, 1);
	vec3 halfAngleDir = normalize(lightDir + viewDirection);
	float blinnTerm = dot(surfaceNormal, halfAngleDir);
	float angleViewToHalf = dot(halfAngleDir, viewDirection);
	angleViewToHalf = clamp(angleViewToHalf, 0, 1);
	float reflectCoeff;
	float refractCoeff;
	CalcFresnel(angleViewToHalf, reflectCoeff, refractCoeff);
	blinnTerm = clamp(blinnTerm, 0, 1);
	blinnTerm = cosAngIncidence != 0.0 ? blinnTerm : 0.0;
	blinnTerm = pow(blinnTerm, shininessFactor);
	vec4 refractColor = refractBaseColor;
	vec4 reflectColor = mix(reflectColorStart, reflectColorEnd, cosAngReflect);
	reflectColor = reflectColor + (specularColor * blinnTerm);
	outputColor = mix(reflectColor, refractColor, reflectCoeff);
}
