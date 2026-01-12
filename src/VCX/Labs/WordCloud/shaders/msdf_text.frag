#version 330 core

in vec2 TexCoord;
in vec4 Color;

out vec4 FragColor;

uniform sampler2D uMSDFTexture;

// 中值函数，用于提取 MSDF 的距离场信息
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // 1. 采样 MSDF 纹理
    vec3 msd = texture(uMSDFTexture, TexCoord).rgb;

    // 2. 获取有符号距离值 (0.5 为文字边缘)
    float sd = median(msd.r, msd.g, msd.b);

    // 3. 【抗锯齿核心】利用屏幕空间导数计算平滑范围
    // fwidth(sd) 自动根据文字大小、缩放和旋转计算出 1 像素对应的距离变化
    float unitRange = fwidth(sd);
    
    // 4. 计算透明度：将 sd 映射到 [0, 1] 的平滑过渡带
    // 这里的 0.001 用于防止除以 0 导致的闪烁（特别是极小文字移动时）
    float opacity = clamp((sd - 0.5) / max(unitRange, 0.001) + 0.5, 0.0, 1.0);

    // 5. 【预乘 Alpha 输出】
    // 将 RGB 提前乘以 Alpha，彻底消除白色背景下的黑边
    float finalAlpha = Color.a * opacity;
    FragColor = vec4(Color.rgb * finalAlpha, finalAlpha);

    // 性能优化：剔除几乎完全透明的片元
    if (FragColor.a < 0.001) {
        discard;
    }
}