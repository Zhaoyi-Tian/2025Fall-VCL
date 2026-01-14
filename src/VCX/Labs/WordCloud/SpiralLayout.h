#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cmath>

#include "Labs/WordCloud/WordEntity.h"
#include "Labs/WordCloud/PhysicsSimulator.h"
#include "Labs/WordCloud/Mask.h"

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

            for (int attempt = 0; attempt < maxAttempts; ++attempt) {
                // 标准阿基米德螺旋线：theta 随尝试次数线性增加
                // 这样可以确保从小到大均匀向外搜索，而不会受 totalWords 影响导致搜索范围过小
                float theta = attempt * angularOffset;
                float r     = spiralA + spiralB * theta;

                glm::vec2 spiralPos(
                    canvasCenter.x + r * std::cos(theta),
                    canvasCenter.y + r * std::sin(theta)
                );
                
                // 始终更新输出位置，作为 fallback
                outPosition = spiralPos;

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
                    return true;
                }
            }

            return false;
        }

        // 带蒙版检查的螺旋线布局
        // 只接受在蒙版区域内的位置
        static bool FindNonCollidingSpiralPositionWithMask(
            WordEntity const& newWord,
            std::vector<WordEntity> const& existingWords,
            std::size_t excludeIndex,
            glm::vec2 const& canvasCenter,
            float spiralA,
            float spiralB,
            float angularOffset,
            Mask const& mask,
            glm::vec2& outPosition,
            int maxAttempts = 3000)
        {
            if (maxAttempts <= 0) maxAttempts = 3000;

            for (int attempt = 0; attempt < maxAttempts; ++attempt) {
                // 标准阿基米德螺旋线
                float theta = attempt * angularOffset;
                float r     = spiralA + spiralB * theta;

                glm::vec2 spiralPos(
                    canvasCenter.x + r * std::cos(theta),
                    canvasCenter.y + r * std::sin(theta)
                );

                // 【新增】检查是否在蒙版区域内
                if (!mask.IsInside(spiralPos)) {
                    continue;  // 跳过蒙版外的位置
                }

                outPosition = spiralPos;

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
                    return true;
                }
            }

            return false;
        }
    };
} // namespace VCX::Labs::labf
