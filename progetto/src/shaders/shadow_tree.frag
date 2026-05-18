#version 460 core

in vec2 vTex;

uniform sampler2D uTex;
uniform float alpha_cutoff;

void main()
{             
    float alpha = texture(uTex, vTex).a;
    
    if(alpha < alpha_cutoff) {
        discard; 
    }
} 