#version 430 core

layout(std140, binding = 2) uniform Camera {
    mat4 uViewProj;
    vec4 uLightDir;
    vec4 uEyePos;
};

in vec4 vColor;
in vec3 vNormal;

out vec4 FragColor;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir.xyz);

    float diffuse = max(dot(N, L), 0.0);

    // Simple CAD-like lighting.
    float shade = 0.35 + 0.65 * diffuse;

    FragColor = vec4(vColor.rgb * shade, vColor.a);
}