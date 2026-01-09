#pragma once
#include <glm/glm.hpp>

struct TextVertex {
    glm::vec2 position;    // 顶点位置（屏幕坐标）
    glm::vec2 texCoord;    // 字体纹理UV坐标
    glm::vec4 color;       // 顶点颜色
};

struct GlyphInfo {
    glm::vec2 uv0;         // UV左上角
    glm::vec2 uv1;         // UV右下角
    glm::vec2 size;        // 字形尺寸（像素）
    glm::vec2 bearing;     // 字形基点偏移
    float advance;         // 到下一个字符的距离（像素）
};