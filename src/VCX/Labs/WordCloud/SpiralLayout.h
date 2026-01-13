#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cmath>

#include "Labs/WordCloud/WordEntity.h"
#include "Labs/WordCloud/PhysicsSimulator.h"

namespace VCX::Labs::labf {

    // 静态螺旋线布局工具类
    class SpiralLayout {
    public:
        // 检查两个 AABB 是否重叠（快速剔除）
        static inline bool CheckAABBOverlap(
            BoundingBox const& a,
            BoundingBox const& b)
        {
            return !(a.max.x < b.min.x || a.max.y < b.min.y ||
                     b.max.x < a.min.x || b.max.y < a.min.y);
        }

        // 构建词的 AABB
        static inline BoundingBox BuildWordAABB(
            WordEntity const& w,
            glm::vec2 const& position)
        {
            BoundingBox box;
            box.min = position - w.boxHalfSize;
            box.max = position + w.boxHalfSize;
            return box;
        }

        // 计算螺旋线位置（基于权重）
        static inline void CalculateSpiralPosition(
            float normalizedWeight,  // 0~1，1 表示中心，0 表示外围
            glm::vec2 const& canvasCenter,
            float spiralA,
            float spiralB,
            float angularOffset,
            float totalWords,
            glm::vec2& outPosition)
        {
            float maxTheta = totalWords * angularOffset * 3.14159f * 2.0f;
            float theta    = (1.0f - normalizedWeight) * maxTheta;
            float r        = spiralA + spiralB * theta;

            outPosition = glm::vec2(
                canvasCenter.x + r * std::cos(theta),
                canvasCenter.y + r * std::sin(theta)
            );
        }

        // 查找不碰撞的螺旋线位置
        static bool FindNonCollidingSpiralPosition(
            WordEntity const& newWord,
            std::vector<WordEntity> const& existingWords,
            std::size_t excludeIndex,
            glm::vec2 const& canvasCenter,
            float spiralA,
            float spiralB,
            float angularOffset,
            glm::vec2& outPosition,
            int maxAttempts = 1000)
        {
            if (maxAttempts <= 0) maxAttempts = 1000;

            float totalWords = static_cast<float>(existingWords.size() + 1);

            for (int attempt = 0; attempt < maxAttempts; ++attempt) {
                float normalizedWeight = 1.0f - (static_cast<float>(attempt) / static_cast<float>(maxAttempts));

                glm::vec2 spiralPos;
                CalculateSpiralPosition(
                    normalizedWeight,
                    canvasCenter,
                    spiralA, spiralB, angularOffset,
                    totalWords,
                    spiralPos
                );

                BoundingBox newWordAABB = BuildWordAABB(newWord, spiralPos);
                bool collision = false;

                for (std::size_t i = 0; i < existingWords.size(); ++i) {
                    if (i == excludeIndex) continue;

                    auto const& existingWord = existingWords[i];
                    BoundingBox existingAABB = BuildWordAABB(existingWord, existingWord.position);

                    if (!CheckAABBOverlap(newWordAABB, existingAABB)) {
                        continue;
                    }

                    WordEntity tempNewWord = newWord;
                    tempNewWord.position = spiralPos;
                    tempNewWord.orientation = 0.0f;

                    if (CheckTwoLevelOBBCollision(tempNewWord, existingWord)) {
                        collision = true;
                        break;
                    }
                }

                if (!collision) {
                    outPosition = spiralPos;
                    return true;
                }
            }

            return false;
        }
    };
} // namespace VCX::Labs::labf
