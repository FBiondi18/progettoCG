#version 460 core  
out vec4 finalColor; 

in vec2 vTex;
in vec3 vColor;
in vec3 vNormal;
in vec3 vFragPos;
in vec4 vFragPosLight;

layout (std140, binding = 0) uniform Matrices
{
	mat4 uProj;
	mat4 uView;
	mat4 uLightSpace;
	mat4 uLightSpaceMatrices[30];
};

struct material_model {
    vec4  uColor;
    float uMetallic;
    float uRoughness;
    float uOcclusionStrength;
    float alpha_cutoff;
    int   alpha_mode;
};

layout (std140, binding = 1) uniform Material {
    material_model models[18];
};

uniform int indice;

uniform sampler2D uTex;
uniform sampler2D uMRTex; 
uniform sampler2D uNormalTex;
uniform sampler2D uEmissive;
uniform sampler2D uOcclusion;
uniform sampler2D uShadowMap;
uniform sampler2DArray uCarShadowMap;
uniform sampler2DArray uLampShadowMap;

struct light {
    vec3 position;   
    float intensity;    // 16 byte
    vec3 color;
    float cutOff;       // 16 byte
    vec3 direction;      
    float outerCutOff;  // 16 byte
};


layout (std140, binding = 2) uniform Light{
    light uLampLights[20];
    light uCarLights[20];
    vec3 uSunDir;
    vec3 uSunColor;
    vec3 uViewPos;
    float uExposure;
};


const float PI = 3.14159265359;

vec3 getNormalFromMap();  
float DistributionGGX(vec3 N, vec3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3 fresnelSchlick(float cosTheta, vec3 F0);
vec3 CalculatePBR(vec3 L, vec3 V, vec3 N, vec3 F0, vec3 albedo, float metallic, float roughness, vec3 radiance);
float ShadowCalculation(vec4 fragPosLightSpace, vec3 N, vec3 L);
float CarShadowCalculation(vec4 fragPosLightSpace, vec3 N, vec3 L, int carIndex);
float LampShadowCalculation(vec4 fragPosLightSpace, vec3 N, vec3 L, int lampIndex);

void main(void) 
{
    float carLinear = 0.22f;
    float carQuadratic = 0.30f;

    float linear = 0.22f;
    float quadratic = 0.20f;

    vec4 texColor = texture(uTex, vTex);
    vec4 baseColor = texColor * models[indice].uColor;

    if (models[indice].alpha_mode == 1 && baseColor.a < models[indice].alpha_cutoff) {
        discard;
    }

    vec3 albedo = pow(baseColor.rgb, vec3(2.2));

    vec4 mrTex = texture(uMRTex, vTex);
    float roughness = mrTex.g * models[indice].uRoughness;
    float metallic  = mrTex.b * models[indice].uMetallic;

    vec3 N = getNormalFromMap();

    float ao = texture(uOcclusion, vTex).r * models[indice].uOcclusionStrength;
    vec3 emissive = texture(uEmissive, vTex).rgb;
    vec3 V = normalize(uViewPos - vFragPos);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    vec3 Lout = vec3(0.0);

    // 1. Luce del Sole (Direzionale)
    vec3 light = normalize(uSunDir);
    vec3 sunLight = CalculatePBR(light, V, N, F0, albedo, metallic, roughness, uSunColor);

    float sunAngleY = normalize(uSunDir).y; 

    float shadowFade = smoothstep(0.0, 0.17, sunAngleY);

    float shadow = ShadowCalculation(vFragPosLight, N, light);

    shadow *= shadowFade;

    Lout += sunLight * (1.0 - shadow);

    // 2. Lampioni (Faretti con attenuazione quadratica custom)
    for (int i = 0; i < 20; ++i) {
        vec3 lightVec = uLampLights[i].position - vFragPos;
        float distance = length(lightVec);
        vec3 L = lightVec / distance;

        // Calcolo Spot
        vec3 spotDir = normalize(-uLampLights[i].direction);
        float theta = dot(L, spotDir);
        float epsilon = uLampLights[i].cutOff - uLampLights[i].outerCutOff;
        float spot = clamp((theta - uLampLights[i].outerCutOff) / epsilon, 0.0, 1.0);

       
        if (spot > 0.0) {
            float d2 = distance * distance;
            float attenuation = 1.0 / (1.0 + linear * distance + quadratic * d2);

            vec4 fragLampPosLight = uLightSpaceMatrices[10 + i] * vec4(vFragPos, 1.0);
            float shadowFactor = LampShadowCalculation(fragLampPosLight, N, L, i);

            float visibility = 1.0 - shadowFactor;

            vec3 radiance = uLampLights[i].color * uLampLights[i].intensity * attenuation * spot * visibility;
            Lout += CalculatePBR(L, V, N, F0, albedo, metallic, roughness, radiance);
        }
    }

    // 3. Luci delle Auto (Faretti)
    for (int i = 0; i < 20; ++i) {
        vec3 lightVec = uCarLights[i].position - vFragPos;
        float distance = length(lightVec);
        if (distance > 0.5) continue;
        vec3 L = lightVec / distance; 

        // Calcolo Spot
        vec3 spotDir = normalize(-uCarLights[i].direction);
        float theta = dot(L, spotDir); 
        float epsilon = uCarLights[i].cutOff - uCarLights[i].outerCutOff;
        float spotIntensity = clamp((theta - uCarLights[i].outerCutOff) / epsilon, 0.0, 1.0);

        if (spotIntensity > 0.0) {
            float d2 = distance * distance;
            float attenuation = 1.0 / (1.0 + carLinear * distance + carQuadratic * d2);

            int carIndex = i/2;
            vec4 fragCarPosLight = uLightSpaceMatrices[carIndex] * vec4(vFragPos, 1.0);
            float shadowFactor = CarShadowCalculation(fragCarPosLight, N, L, carIndex);

            float visibility = 1.0 - shadowFactor;

            vec3 radiance = uCarLights[i].color * uCarLights[i].intensity * attenuation * spotIntensity * visibility;
            Lout += CalculatePBR(L, V, N, F0, albedo, metallic, roughness, radiance);
        }
    }
    
    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + Lout + emissive;

    // HDR Tonemapping
    float exposure = 1.0;
    color = vec3(1.0) - exp(-color * uExposure);
    
    // Gamma Correction
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

vec3 CalculatePBR(vec3 L, vec3 V, vec3 N, vec3 F0, vec3 albedo, float metallic, float roughness, vec3 radiance) {
    vec3 H = normalize(V + L);
    
    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);
    
    vec3 numerator    = NDF * G * F; 
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;	  

    float NdotL = max(dot(N, L), 0.0);        
    
    return (kD * albedo / PI + specular) * radiance * NdotL;
}
float ShadowCalculation(vec4 fragPosLightSpace, vec3 N, vec3 L)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0)
        return 0.0;

    float currentDepth = projCoords.z;
    
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001); // Bias ridotto per PCF
    
    // FILTRO PCF (Ombre morbide)
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);
    
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(uShadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
            shadow += currentDepth - bias > pcfDepth  ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;

    return shadow;
}
float CarShadowCalculation(vec4 fragPosLightSpace, vec3 N, vec3 L, int carIndex)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001); 
    
    float shadow = 0.0;
    
    vec2 texelSize = 1.0 / textureSize(uCarShadowMap, 0).xy; 
    
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            vec3 uvCoords = vec3(projCoords.xy + vec2(x, y) * texelSize, float(carIndex));
            
            float pcfDepth = texture(uCarShadowMap, uvCoords).r; 
            shadow += currentDepth - bias > pcfDepth  ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;

    return shadow;
}
float LampShadowCalculation(vec4 fragPosLightSpace, vec3 N, vec3 L, int lampIndex)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001); 
    
    float shadow = 0.0;
    
    vec2 texelSize = 1.0 / textureSize(uLampShadowMap, 0).xy; 
    
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            vec3 uvCoords = vec3(projCoords.xy + vec2(x, y) * texelSize, float(lampIndex));
            
            float pcfDepth = texture(uLampShadowMap, uvCoords).r; 
            shadow += currentDepth - bias > pcfDepth  ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;

    return shadow;
}