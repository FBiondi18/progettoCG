#version 460 core 
layout (location = 0) in vec3 aPosition; 
layout (location = 1) in vec3 aColor; 
layout (location = 2) in vec3 aNormal;

out vec2 vTex;
out vec3 vNormal;
out vec3 vFragPos;
out vec4 vFragPosLight;

layout (std140, binding = 0) uniform Matrices
{
	mat4 uProj;
	mat4 uView;
	mat4 uLightSpace;
	mat4 uLightSpaceMatrices[30];
};

uniform mat4 uModel;

void main(void) 
{ 
	vNormal = aNormal;
	vFragPos = vec3(uModel * vec4(aPosition, 1.0));
	vTex = vec2(vFragPos.x, vFragPos.z) * 0.5;
	vFragPosLight = uLightSpace * vec4(vFragPos, 1.0);
    gl_Position = uProj * uView * uModel * vec4(aPosition, 1.0); 
    
}
