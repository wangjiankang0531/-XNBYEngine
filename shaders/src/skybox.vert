#version 450

layout(push_constant) uniform PC {
    mat4 inv_proj_view;
};

layout(location = 0) out vec3 outDir;

void main() {
    // Full-screen triangle (no vertex buffer): covers screen with 3 vertices.
    // Completely avoids near/far plane clipping.
    vec2 ndc = vec2(
        (gl_VertexIndex & 1) != 0 ? 3.0 : -1.0,
        (gl_VertexIndex & 2) != 0 ? 3.0 : -1.0
    );

    // Unproject NDC -> world-space ray direction
    vec4 worldPos = inv_proj_view * vec4(ndc, 1.0, 1.0);
    outDir = worldPos.xyz / worldPos.w;

    gl_Position = vec4(ndc, 1.0, 1.0);
}
