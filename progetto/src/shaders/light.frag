#version 460 core  
out vec4 finalColor; 

in vec2 vTex;
in vec3 vColor;
in vec3 vNormal;
in vec3 vFragPos;
in mat3 TBN;

uniform vec3 uViewPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;

uniform int  alpha_mode; // 0 = OPAQUE, 1 = MASK, 2 = BLEND
uniform float alpha_cutoff;

uniform sampler2D uTex;
uniform vec4 uColor;

uniform sampler2D uMRTex; 
uniform float uMetallic;
uniform float uRoughness;

uniform sampler2D uNormalTex;
uniform sampler2D uEmissive;
uniform sampler2D uOcclusion;
uniform float uOcclusionStrength;

uniform bool isTerrain;

const float PI = 3.14159265359;

vec3 getNormalFromMap();  
float DistributionGGX(vec3 N, vec3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3 fresnelSchlick(float cosTheta, vec3 F0);

void main(void) 
{
    vec4 texColor = texture(uTex, vTex);
    vec4 baseColor = texColor * uColor;

    if (alpha_mode == 2 && baseColor.a < alpha_cutoff) {
        discard;
    }

    vec3 albedo = pow(baseColor.rgb, vec3(2.2));
    //vec3 albedo = baseColor.rgb;

    vec4 mrTex = texture(uMRTex, vTex);
    float roughness = mrTex.g * uRoughness;
    float metallic  = mrTex.b * uMetallic;

    vec3 N = isTerrain ? normalize(vNormal) : getNormalFromMap();

    float ao = texture(uOcclusion, vTex).r * uOcclusionStrength;

    vec3 emissive = texture(uEmissive, vTex).rgb;

    //vec3 lightColor = vec3(5.0, 5.0, 5.0);

    vec3 V = normalize(uViewPos - vFragPos);

    vec3 Lout = vec3(0.0);

    vec3 L = normalize(uSunDir);
    vec3 H = normalize(V + L);

    vec3 radiance = uSunColor;

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float NDF = DistributionGGX(N, H, roughness);       
    float G   = GeometrySmith(N, V, L, roughness);   

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0)  + 0.0001;
    vec3 specular     = numerator / denominator;  

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
  
    kD *= 1.0 - metallic;	
  
    float NdotL = max(dot(N, L), 0.0);        
    Lout += (kD * albedo / PI + specular) * radiance * NdotL;

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + Lout;

    color += emissive;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2)); 

    finalColor = vec4(color, baseColor.a);
}
vec3 getNormalFromMap()
{
    vec3 tangentNormal = texture(uNormalTex, vTex).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(vFragPos);
    vec3 Q2  = dFdy(vFragPos);
    vec2 st1 = dFdx(vTex);
    vec2 st2 = dFdy(vTex);

    vec3 N   = normalize(vNormal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}  

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a  = roughness*roughness;
    float a2 = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}
