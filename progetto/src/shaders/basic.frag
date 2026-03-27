#version 460 core  
out vec4 color; 

in vec3 vColor; 
in vec2 vTex;

uniform vec3 uColor;
uniform sampler2D uTex;

void main(void) 
{
    color = vec4((uColor.x<0)?vColor:uColor, 1.0) + texture2D(uTex, vTex);
} 