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
                    
                    // 允许一定程度的重叠 (Relaxed Collision Check for tighter packing)
                    // 使用稍微缩小一点的 OBB 进行碰撞检测，允许边缘轻微重叠
                    // 通过减小 boxHalfSize 实现
                    WordEntity shrunkNewWord = tempNewWord;
                    shrunkNewWord.boxHalfSize *= 0.95f; // 允许 20% 的重叠
                    
                    WordEntity shrunkExistingWord = existingWord;
                    shrunkExistingWord.boxHalfSize *= 0.95f;

                    if (CheckTwoLevelOBBCollision(shrunkNewWord, shrunkExistingWord)) {
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
            int maxAttempts = 30000)
        {
            if (maxAttempts <= 0) maxAttempts = 30000;

            float theta = 0.0f;
            // 使用均匀弧长步长，避免外圈搜索过疏
            // 步长与词的大小相关，取 min(halfSize) 的一部分，确保不漏掉缝隙
            float arcStep = std::min(newWord.boxHalfSize.x, newWord.boxHalfSize.y);
            arcStep = std::clamp(arcStep, 2.0f, 10.0f);

            for (int attempt = 0; attempt < maxAttempts; ++attempt) {
                // 标准阿基米德螺旋线
                // theta 由弧长控制递增
                float r     = spiralA + spiralB * theta;

                glm::vec2 spiralPos(
                    canvasCenter.x + r * std::cos(theta),
                    canvasCenter.y + r * std::sin(theta)
                );

                // 立即计算下一次迭代的 theta (dTheta = arcStep / r)
                float dTheta = (r > 1.0f) ? (arcStep / r) : angularOffset;
                theta += dTheta;

                // 【新增】检查是否在蒙版区域内
                // 1. 坐标转换：Physics (Bottom-Left) -> Mask (Top-Left)
                glm::vec2 maskPosCenter = spiralPos;
                maskPosCenter.y = float(mask.GetCanvasSize().y) - spiralPos.y;
                
                if (!mask.IsInside(maskPosCenter)) {
                    continue;  // 中心点不在蒙版内
                }

                // 2. 检查包围盒四个角点 (确保整个词都在蒙版内)
                BoundingBox box = BuildWordAABB(newWord, spiralPos);
                glm::vec2 corners[4] = {
                    {box.min.x, box.min.y}, {box.max.x, box.min.y},
                    {box.max.x, box.max.y}, {box.min.x, box.max.y}
                };
                
                bool allInside = true;
                for (auto& p : corners) {
                    glm::vec2 mp = p;
                    mp.y = float(mask.GetCanvasSize().y) - p.y;
                    if (!mask.IsInside(mp)) {
                        allInside = false;
                        break;
                    }
                }
                if (!allInside) continue;

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
                    
                    // 【改进】蒙版模式下允许更紧密的填充 (Relaxed Collision Check)
                    // 允许 10% 的初始重叠，因为后续物理模拟会把它们推开
                    // 这样可以确保小词能够填入大词的缝隙中
                    WordEntity shrunkNewWord = tempNewWord;
                    shrunkNewWord.boxHalfSize *= 0.9f; 
                    
                    WordEntity shrunkExistingWord = existingWord;
                    shrunkExistingWord.boxHalfSize *= 0.9f;

                    if (CheckTwoLevelOBBCollision(shrunkNewWord, shrunkExistingWord)) {
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
