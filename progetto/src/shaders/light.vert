#version 460 core 
layout (location = 0) in vec3 aPosition; 
layout (location = 1) in vec3 aColor; 
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec4 aTangent;
layout (location = 4) in vec2 aTex;

out vec3 vColor;
out vec2 vTex;
out vec3 vNormal;
out vec3 vFragPos;
out mat3 TBN;

uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;


void main(void) 
{ 

	vNormal = mat3(transpose(inverse(uModel))) * aNormal;
	//vColor = aColor;
	vTex = aTex;
	vFragPos = vec3(uModel * vec4(aPosition, 1.0));

	mat3 normalMatrix = mat3(uModel);
    vec3 T = normalize(normalMatrix * aTangent.xyz);
    vec3 N = normalize(vNormal);
	T = normalize(T - dot(T, N) * N);
	vec3 B = cross(N, T) * aTangent.w;
	TBN = mat3(T, B, N);

    gl_Position = uProj*uView*uModel*vec4(aPosition, 1.0); 
}