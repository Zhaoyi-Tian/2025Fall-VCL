#version 410 core

layout(location = 0) in  vec3 v_Position;
layout(location = 1) in  vec3 v_Normal;

layout(location = 0) out vec4 f_Color;

struct Light {
    vec3  Intensity;
    vec3  Direction;   // For spot and directional lights.
    vec3  Position;    // For point and spot lights.
    float CutOff;      // For spot lights.
    float OuterCutOff; // For spot lights.
};

layout(std140) uniform PassConstants {
    mat4  u_Projection;
    mat4  u_View;
    vec3  u_ViewPosition;
    vec3  u_AmbientIntensity;
    Light u_Lights[4];
    int   u_CntPointLights;
    int   u_CntSpotLights;
    int   u_CntDirectionalLights;
};

uniform vec3 u_CoolColor;
uniform vec3 u_WarmColor;

vec3 Shade (vec3 lightDir, vec3 normal) {
    // your code here:
    vec3 k;
    float NdotL = dot(lightDir, normal);
    float alpha = (1.0 + NdotL) / 2.0;
    int u_ColorSteps=3;
    float quantizedAlpha = floor(alpha * u_ColorSteps) / u_ColorSteps;
    k=(1-quantizedAlpha)*u_CoolColor+quantizedAlpha*u_WarmColor;
    return k;
}

void main() {
    // your code here:
    float gamma = 2.2;
    vec3 ambientLinear = pow(u_AmbientIntensity, vec3(gamma));
    vec3 normal = normalize(v_Normal);
    vec3 total = ambientLinear;
    for(int i=0;i<u_CntPointLights;i++){
        vec3 lightDir   = normalize(u_Lights[i].Position - v_Position);
        float dist      = length(u_Lights[i].Position - v_Position);
        float attenuation = 1.0 / (dist * dist);
        total += Shade(lightDir, normal)*attenuation*u_Lights[i].Intensity;
    }
    for(int i=u_CntPointLights+u_CntSpotLights;i<u_CntDirectionalLights+u_CntSpotLights+u_CntPointLights;i++){
        total += Shade(u_Lights[i].Direction, normal)*u_Lights[i].Intensity;
    }
    f_Color = vec4(pow(total, vec3(1. / gamma)), 1.);
}
