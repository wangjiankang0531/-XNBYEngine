#version 450

layout(set = 0, binding = 0) uniform sampler2D skyTex;
layout(location = 0) in  vec3 inDir;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

void main() {
    vec3 d = normalize(inDir);
    float u = atan(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5;
    outColor = texture(skyTex, vec2(u, v));
}
