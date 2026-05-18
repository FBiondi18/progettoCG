#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 4) in vec2 aTex;

layout (std140, binding = 0) uniform Matrices
{
    mat4 uProj;
    mat4 uView;
    mat4 uLightSpace;
    mat4 uLightSpaceMatrices[30];
};

out vec2 vTex;

uniform mat4 uModel;
uniform int indice;

void main()
{
    vTex = aTex;
    if (indice == -1)
        gl_Position = uLightSpace * uModel * vec4(aPos, 1.0);
    else
        gl_Position = uLightSpaceMatrices[indice] * uModel * vec4(aPos, 1.0);
}