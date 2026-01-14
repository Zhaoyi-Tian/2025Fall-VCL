#pragma once

#include <string>
#include <vector>
#include <array>
#include <cmath>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "Labs/WordCloud/UTF8Utils.h"

namespace VCX::Labs::labf {

    struct BoundingBox {
        glm::vec2 min { 0.f, 0.f };
        glm::vec2 max { 0.f, 0.f };

        glm::vec2 size() const { return glm::vec2(max.x - min.x, max.y - min.y); }
        glm::vec2 center() const { return glm::vec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f); }
        bool      contains(glm::vec2 const & p) const { return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y; }
    };

    // OBB (Oriented Bounding Box) 方向包围盒
    // 用于支持旋转的精确碰撞检测
    struct OBB {
        glm::vec2 center { 0.f, 0.f };    // 中心点（世界坐标）
        glm::vec2 halfSize { 0.f, 0.f };  // 半宽、半高（本地坐标）
        float rotation = 0.f;              // 旋转角度（弧度）

        // 获取旋转后的 X 轴向量（单位向量）
        glm::vec2 axisX() const {
            float c = std::cos(rotation);
            float s = std::sin(rotation);
            return glm::vec2(c, s);
        }

        // 获取旋转后的 Y 轴向量（单位向量）
        glm::vec2 axisY() const {
            float c = std::cos(rotation);
            float s = std::sin(rotation);
            return glm::vec2(-s, c);
        }

        // 获取四个角点（世界坐标）
        std::array<glm::vec2, 4> corners() const {
            glm::vec2 ax = axisX() * halfSize.x;
            glm::vec2 ay = axisY() * halfSize.y;
            return {
                center - ax - ay,  // 左下
                center + ax - ay,  // 右下
                center + ax + ay,  // 右上
                center - ax + ay   // 左上
            };
        }
    };

    // 字符级 OBB（用于大词的精细碰撞检测）
    struct LetterOBB {
        glm::vec2 localCenter { 0.f, 0.f };  // 相对于词中心的偏移（未旋转）
        glm::vec2 halfSize { 0.f, 0.f };     // 半宽、半高
    };

    class WordEntity {
    public:
        // === 基本属性 ===
        std::string text;                         // 原始文本（UTF-8）
        float       fontSize  = 12.f;             // 像素大小（渲染用）
        glm::vec4   color { 1.f, 1.f, 1.f, 1.f }; // RGBA 颜色
        bool        isHighlighted = false;        // 高亮状态（选中/悬停等）

        // === 位置与方向 ===
        glm::vec2   position { 0.f, 0.f };        // 文本中心点坐标
        float       orientation = 0.f;            // 旋转角度（度数）

        // === OBB 系统 ===
        // 词级包围盒半尺寸：用于交互（点击、拖拽）和碰撞快速剔除
        // 始终使用 fullHeight，确保不会漏检
        glm::vec2   boxHalfSize { 0.f, 0.f };        // (width/2, fullHeight/2)

        // 两级盒子（英文大词专用）—— 通过快速剔除后的精细检测
        bool        useTwoLevelBox = false;          // 是否使用两级盒子
        OBB         wordLevelOBB;                    // 词级 OBB（x-height 区域）
        std::vector<LetterOBB> letterOBBs;           // 字符级 OBB（每个字符一个）

        // 度量信息
        float       fullHeight = 0.f;                // 完整渲染高度（含 ascender/descender）
        float       xHeight = 0.f;                   // x-height（小写字母高度）
        float       baselineY = 0.f;                 // 基线相对于词中心的 Y 偏移
        float       xHeightCenterY = 0.f;            // x-height 区域中心（相对于词中心）
        bool        isChinese = false;               // 是否包含中文

        // === 物理属性 ===
        float       mass = 1.f;                      // 质量
        glm::vec2   velocity { 0.f, 0.f };           // 线速度
        glm::vec2   forceAccumulator { 0.f, 0.f };   // 累积外力

        // === 语义 ===
        std::vector<float> wordVector;               // 词向量

        // === 调试信息 ===
        bool        maskCollision = false;           // 本帧是否与蒙版碰撞
        glm::vec2   maskCollisionPoint { 0.f, 0.f }; // 碰撞点（物理坐标）
        glm::vec2   maskCollisionNormal { 0.f, 0.f };// 碰撞法线（指向有效区域内部）

        // === 辅助方法 ===
        void clearAccumulators() {
            forceAccumulator = glm::vec2(0.f, 0.f);
        }

        void applyForce(glm::vec2 const& f) {
            forceAccumulator += f;
        }

        // 根据包围盒面积计算质量 (需传入画布尺度的平方进行归一化)
        // normalizedArea = area / (scale * scale)
        void updateMassFromArea(float scaleSquared) {
            float area = boxHalfSize.x * boxHalfSize.y * 4.0f;
            mass = area / scaleSquared;
            if (mass < 1e-6f) mass = 1e-6f; // 防止质量过小
        }
    };

    // ============================================================
    // 辅助函数
    // ============================================================

    // 判断字符串是否包含中文字符
    inline bool ContainsChinese(const std::string& text) {
        const char* ptr = text.c_str();
        const char* end = ptr + text.size();

        while (ptr < end) {
            uint32_t cp = DecodeUTF8(ptr, end);
            // CJK 统一汉字: U+4E00 - U+9FFF
            if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
            // CJK 扩展 A: U+3400 - U+4DBF
            if (cp >= 0x3400 && cp <= 0x4DBF) return true;
            // CJK 扩展 B-F: U+20000 - U+2CEAF
            if (cp >= 0x20000 && cp <= 0x2CEAF) return true;
        }
        return false;
    }

} // namespace VCX::Labs::labf
