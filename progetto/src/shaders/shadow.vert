#version 460 core
layout (location = 0) in vec3 aPos;

layout (std140, binding = 0) uniform Matrices
{
    mat4 uProj;
    mat4 uView;
    mat4 uLightSpace;
};

uniform mat4 uModel;

void main()
{
    gl_Position = uLightSpace * uModel * vec4(aPos, 1.0);
}