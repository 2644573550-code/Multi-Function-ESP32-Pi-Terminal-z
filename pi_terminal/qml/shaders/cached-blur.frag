#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 sampleRect;
    vec2 outputSize;
    float cornerRadius;
} ub;
layout(binding = 1) uniform sampler2D source;
void main() {
    vec2 uv = ub.sampleRect.xy + qt_TexCoord0 * ub.sampleRect.zw;
    vec4 color = texture(source, uv);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
        color = vec4(0.0);
    if (ub.cornerRadius > 0.0) {
        float r = min(ub.cornerRadius, min(ub.outputSize.x, ub.outputSize.y) * 0.5);
        vec2 q = abs((qt_TexCoord0 - 0.5) * ub.outputSize) - (ub.outputSize * 0.5 - vec2(r));
        float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
        color *= 1.0 - smoothstep(-1.0, 1.0, d);
    }
    fragColor = color * ub.qt_Opacity;
}
