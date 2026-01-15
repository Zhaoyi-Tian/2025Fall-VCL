#pragma once

#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <random>

#include <glm/glm.hpp>

#include "Labs/WordCloud/WordEntity.h"
#include "Labs/WordCloud/Mask.h"

namespace VCX::Labs::labf {

    struct PhysicsParams {
        glm::vec2 canvasCenter { 575.0f, 400.0f };
        float     pixelsPerUnit { 30.0f };

        float kCenter = 3.0f;            // 中心力系数
        float kNeighbor = 0.0f;          // 邻域力系数
        float lambda = 0.8f;             // 速度阻尼

        float restitution = 0.2f;        // 弹性系数

        float fixedDt = 1.0f / 60.0f;    // 物理时间步
        int maxIterations = 300;         // 最大迭代次数
    };

    // OBB 辅助函数
    // ============================================================

    // 获取词级 OBB：用于交互和碰撞快速剔除
    // 始终使用 fullHeight，确保不会漏检
    inline OBB GetWordOBB(WordEntity const& w) {
        OBB obb;
        obb.center = w.position;
        obb.halfSize = w.boxHalfSize;  // (width/2, fullHeight/2)
        obb.rotation = -w.orientation * 3.14159265f / 180.0f;
        return obb;
    }

    // 获取字符级 OBB 的世界坐标（用于大词的精细碰撞检测）
    inline OBB GetLetterOBB(WordEntity const& w, size_t letterIndex) {
        auto const& letter = w.letterOBBs[letterIndex];
        float rad = -w.orientation * 3.14159265f / 180.0f;
        float c = std::cos(rad), s = std::sin(rad);

        // 将 localCenter 旋转到世界坐标
        glm::vec2 rotatedCenter(
            letter.localCenter.x * c - letter.localCenter.y * s,
            letter.localCenter.x * s + letter.localCenter.y * c
        );

        OBB obb;
        obb.center = w.position + rotatedCenter;
        obb.halfSize = letter.halfSize;
        obb.rotation = rad;
        return obb;
    }

    // 检查点是否在 OBB 内部（用于点击测试）
    inline bool PointInOBB(glm::vec2 point, OBB const& obb) {
        // 将点转换到 OBB 本地坐标系
        glm::vec2 local = point - obb.center;
        float c = std::cos(-obb.rotation);
        float s = std::sin(-obb.rotation);
        glm::vec2 rotated(local.x * c - local.y * s, local.x * s + local.y * c);

        return std::abs(rotated.x) <= obb.halfSize.x &&
               std::abs(rotated.y) <= obb.halfSize.y;
    }

    // 前向声明
    inline bool LineIntersectsAABB(glm::vec2 p1, glm::vec2 p2, BoundingBox const& box);

    // 检查线段是否与 OBB 相交（用于邻域搜索）
    // 将线段转换到 OBB 本地坐标系，然后使用 AABB 相交检测
    inline bool LineIntersectsOBB(glm::vec2 p1, glm::vec2 p2, OBB const& obb) {
        // 将线段端点转换到 OBB 本地坐标系
        float c = std::cos(-obb.rotation);
        float s = std::sin(-obb.rotation);

        glm::vec2 local1 = p1 - obb.center;
        glm::vec2 local2 = p2 - obb.center;

        glm::vec2 rotated1(local1.x * c - local1.y * s, local1.x * s + local1.y * c);
        glm::vec2 rotated2(local2.x * c - local2.y * s, local2.x * s + local2.y * c);

        // 在本地坐标系中，OBB 是一个以原点为中心的 AABB
        BoundingBox localAABB;
        localAABB.min = -obb.halfSize;
        localAABB.max = obb.halfSize;

        return LineIntersectsAABB(rotated1, rotated2, localAABB);
    }

    // 将 OBB 投影到指定轴上，返回投影区间 [min, max]
    inline void ProjectOBB(OBB const& obb, glm::vec2 axis, float& outMin, float& outMax) {
        auto corners = obb.corners();
        outMin = outMax = glm::dot(corners[0], axis);
        for (int i = 1; i < 4; ++i) {
            float proj = glm::dot(corners[i], axis);
            outMin = std::min(outMin, proj);
            outMax = std::max(outMax, proj);
        }
    }

    // SAT 检测：检查两个 OBB 是否相交
    inline bool CheckOBBCollision(OBB const& a, OBB const& b) {
        // 需要检查 4 个分离轴：a 的两个轴 + b 的两个轴
        glm::vec2 axes[4] = {
            a.axisX(), a.axisY(),
            b.axisX(), b.axisY()
        };

        for (auto& axis : axes) {
            float minA, maxA, minB, maxB;
            ProjectOBB(a, axis, minA, maxA);
            ProjectOBB(b, axis, minB, maxB);

            // 如果在任意轴上不重叠，则不碰撞
            if (maxA < minB || maxB < minA) return false;
        }
        return true;  // 所有轴都重叠，发生碰撞
    }

    // 计算两个 OBB 的穿透深度和法线（用于碰撞响应）
    inline bool ComputeOBBPenetration(OBB const& a, OBB const& b,
                                      glm::vec2& normal, float& depth) {
        float minOverlap = FLT_MAX;
        glm::vec2 minAxis(0.0f);

        glm::vec2 axes[4] = { a.axisX(), a.axisY(), b.axisX(), b.axisY() };

        for (auto& axis : axes) {
            float minA, maxA, minB, maxB;
            ProjectOBB(a, axis, minA, maxA);
            ProjectOBB(b, axis, minB, maxB);

            float overlap = std::min(maxA - minB, maxB - minA);
            if (overlap < 0) return false;

            if (overlap < minOverlap) {
                minOverlap = overlap;
                minAxis = axis;
            }
        }

        depth = minOverlap;
        // 确保法线指向正确方向（从 a 指向 b）
        glm::vec2 centerDiff = b.center - a.center;
        normal = (glm::dot(centerDiff, minAxis) > 0) ? minAxis : -minAxis;
        return true;
    }

    // 两级盒子碰撞检测（词级 OBB + 字符级 OBB）
    // 词级 OBB 是 x-height 区域，字符级 OBB 是每个字母的边界框
    inline bool CheckTwoLevelOBBCollision(WordEntity const& a, WordEntity const& b) {
        // 1. 先用碰撞 OBB 快速剔除
        OBB obbA = GetWordOBB(a);
        OBB obbB = GetWordOBB(b);
        if (!CheckOBBCollision(obbA, obbB)) return false;

        // 2. 如果两个都不用两级盒子，直接返回碰撞
        if (!a.useTwoLevelBox && !b.useTwoLevelBox) return true;

        // 3. 收集需要检测的所有 OBB
        std::vector<OBB> boxesA, boxesB;

        if (a.useTwoLevelBox && !a.letterOBBs.empty()) {
            // 字符级 OBB
            for (size_t i = 0; i < a.letterOBBs.size(); ++i) {
                boxesA.push_back(GetLetterOBB(a, i));
            }
            // 词级 OBB（x-height 区域，防止插入字符间隙）
            float radA = -a.orientation * 3.14159265f / 180.0f;
            float cA = std::cos(radA), sA = std::sin(radA);
            // 使用 xHeightCenterY（与字符级 OBB 计算方式一致）
            glm::vec2 xHeightOffsetA(0.0f, a.xHeightCenterY);
            // 旋转偏移
            glm::vec2 rotatedOffsetA(
                xHeightOffsetA.x * cA - xHeightOffsetA.y * sA,
                xHeightOffsetA.x * sA + xHeightOffsetA.y * cA
            );
            OBB wordOBB;
            wordOBB.center = a.position + rotatedOffsetA;
            wordOBB.halfSize = glm::vec2(a.boxHalfSize.x, a.xHeight * 0.5f);
            wordOBB.rotation = radA;
            boxesA.push_back(wordOBB);
        } else {
            boxesA.push_back(obbA);
        }

        if (b.useTwoLevelBox && !b.letterOBBs.empty()) {
            for (size_t i = 0; i < b.letterOBBs.size(); ++i) {
                boxesB.push_back(GetLetterOBB(b, i));
            }
            float radB = -b.orientation * 3.14159265f / 180.0f;
            float cB = std::cos(radB), sB = std::sin(radB);
            // 使用 xHeightCenterY（与字符级 OBB 计算方式一致）
            glm::vec2 xHeightOffsetB(0.0f, b.xHeightCenterY);
            glm::vec2 rotatedOffsetB(
                xHeightOffsetB.x * cB - xHeightOffsetB.y * sB,
                xHeightOffsetB.x * sB + xHeightOffsetB.y * cB
            );
            OBB wordOBB;
            wordOBB.center = b.position + rotatedOffsetB;
            wordOBB.halfSize = glm::vec2(b.boxHalfSize.x, b.xHeight * 0.5f);
            wordOBB.rotation = radB;
            boxesB.push_back(wordOBB);
        } else {
            boxesB.push_back(obbB);
        }

        // 4. 检测所有 OBB 组合
        for (auto const& boxA : boxesA) {
            for (auto const& boxB : boxesB) {
                if (CheckOBBCollision(boxA, boxB)) return true;
            }
        }
        return false;
    }

    // 碰撞信息结构（包含所有碰撞的 OBB 对）
    struct CollisionPair {
        OBB obbA;
        OBB obbB;
    };

    struct CollisionInfo {
        bool hasCollision = false;
        std::vector<CollisionPair> pairs;  // 所有碰撞的 OBB 对
    };

    // 详细的两级碰撞检测（返回所有碰撞对，用于累加修正）
    inline CollisionInfo CheckTwoLevelOBBCollisionDetailed(WordEntity const& a, WordEntity const& b) {
        CollisionInfo info;

        // 1. 先用碰撞 OBB 快速剔除
        OBB obbA = GetWordOBB(a);
        OBB obbB = GetWordOBB(b);
        if (!CheckOBBCollision(obbA, obbB)) return info;

        // 2. 如果两个都不用两级盒子，直接返回词级碰撞
        if (!a.useTwoLevelBox && !b.useTwoLevelBox) {
            info.hasCollision = true;
            info.pairs.push_back({obbA, obbB});
            return info;
        }

        // 3. 收集需要检测的所有 OBB
        std::vector<OBB> boxesA, boxesB;

        if (a.useTwoLevelBox && !a.letterOBBs.empty()) {
            for (size_t i = 0; i < a.letterOBBs.size(); ++i) {
                boxesA.push_back(GetLetterOBB(a, i));
            }
            float radA = -a.orientation * 3.14159265f / 180.0f;
            float cA = std::cos(radA), sA = std::sin(radA);
            glm::vec2 xHeightOffsetA(0.0f, a.xHeightCenterY);
            glm::vec2 rotatedOffsetA(
                xHeightOffsetA.x * cA - xHeightOffsetA.y * sA,
                xHeightOffsetA.x * sA + xHeightOffsetA.y * cA
            );
            OBB wordOBB;
            wordOBB.center = a.position + rotatedOffsetA;
            wordOBB.halfSize = glm::vec2(a.boxHalfSize.x, a.xHeight * 0.5f);
            wordOBB.rotation = radA;
            boxesA.push_back(wordOBB);
        } else {
            boxesA.push_back(obbA);
        }

        if (b.useTwoLevelBox && !b.letterOBBs.empty()) {
            for (size_t i = 0; i < b.letterOBBs.size(); ++i) {
                boxesB.push_back(GetLetterOBB(b, i));
            }
            float radB = -b.orientation * 3.14159265f / 180.0f;
            float cB = std::cos(radB), sB = std::sin(radB);
            glm::vec2 xHeightOffsetB(0.0f, b.xHeightCenterY);
            glm::vec2 rotatedOffsetB(
                xHeightOffsetB.x * cB - xHeightOffsetB.y * sB,
                xHeightOffsetB.x * sB + xHeightOffsetB.y * cB
            );
            OBB wordOBB;
            wordOBB.center = b.position + rotatedOffsetB;
            wordOBB.halfSize = glm::vec2(b.boxHalfSize.x, b.xHeight * 0.5f);
            wordOBB.rotation = radB;
            boxesB.push_back(wordOBB);
        } else {
            boxesB.push_back(obbB);
        }

        // 4. 收集所有碰撞的 OBB 对
        for (auto const& boxA : boxesA) {
            for (auto const& boxB : boxesB) {
                if (CheckOBBCollision(boxA, boxB)) {
                    info.hasCollision = true;
                    info.pairs.push_back({boxA, boxB});
                }
            }
        }
        return info;
    }

    // 邻域搜索：线段-AABB 相交检测
    // ============================================================

    // 检查线段 (p1, p2) 是否与 AABB 相交
    inline bool LineIntersectsAABB(glm::vec2 p1, glm::vec2 p2, BoundingBox const& box) {
        glm::vec2 d = p2 - p1;
        float tmin = 0.0f;
        float tmax = 1.0f;

        // X 轴
        if (std::abs(d.x) < 1e-8f) {
            // 线段平行于 Y 轴
            if (p1.x < box.min.x || p1.x > box.max.x) return false;
        } else {
            float invD = 1.0f / d.x;
            float t1 = (box.min.x - p1.x) * invD;
            float t2 = (box.max.x - p1.x) * invD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }

        // Y 轴
        if (std::abs(d.y) < 1e-8f) {
            // 线段平行于 X 轴
            if (p1.y < box.min.y || p1.y > box.max.y) return false;
        } else {
            float invD = 1.0f / d.y;
            float t1 = (box.min.y - p1.y) * invD;
            float t2 = (box.max.y - p1.y) * invD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }

        return true;
    }

    // 物理模拟器
    // ============================================================
    class PhysicsSimulator {
    public:
        // 重置帧计数器（当词云重新布局时调用）
        void Reset() {
            _frameCount = 0;
            _arbiters.clear();
            _maskArbiters.clear();
        }

        // 每帧调用，使用指定的时间步
        void Update(std::vector<WordEntity>& words, float dt, PhysicsParams& params) {
            if (dt <= 0 || words.empty()) return;

            // 超过最大迭代次数停止模拟，节省性能
            // 解耦帧率：将 maxIterations 视为 60FPS 下的帧数（即时间计数）
            // 无论 dt 是多少，只要物理时间达到 (maxIterations / 60.0) 秒即停止
            float currentRefFrames = _frameCount * (dt * 60.0f);
            if (currentRefFrames >= params.maxIterations) return;

            Step(words, dt, params);
        }

        // 设置蒙版
        void SetMask(Mask const* mask) {
            _mask = mask;
            _maskArbiters.clear();  // 蒙版改变时清除旧的碰撞状态
        }

    private:
        int _frameCount = 0;      // 物理步计数器

        // 数学辅助函数
        static inline float Dot(glm::vec2 const& a, glm::vec2 const& b) { return glm::dot(a, b); }
        static inline float Cross(glm::vec2 const& a, glm::vec2 const& b) { return a.x * b.y - a.y * b.x; }
        static inline glm::vec2 Cross(glm::vec2 const& a, float s) { return { s * a.y, -s * a.x }; }

        // 碰撞约束结构
        struct Arbiter {
            WordEntity* body1;
            WordEntity* body2;
            glm::vec2   normal;
            float       separation; // 穿透为负值
            
            float       massNormal;
            float       massTangent;
            float       bias;
            
            float       Pn = 0.0f;  // 累积法向脉冲
            float       Pt = 0.0f;  // 累积切向脉冲
            
            float       friction;
            float       restitution;
        };

        struct ArbiterKey {
            WordEntity* body1;
            WordEntity* body2;

            ArbiterKey(WordEntity* b1, WordEntity* b2) {
                if (b1 < b2) {
                    body1 = b1;
                    body2 = b2;
                } else {
                    body1 = b2;
                    body2 = b1;
                }
            }

            bool operator<(const ArbiterKey& other) const {
                if (body1 < other.body1) return true;
                if (body1 == other.body1 && body2 < other.body2) return true;
                return false;
            }
        };

        std::map<ArbiterKey, Arbiter> _arbiters;

        // 蒙版边界碰撞相关
        // ============================================================
        struct MaskArbiter {
            WordEntity* body;
            glm::vec2   normal;
            float       separation;
            float       massNormal;
            float       bias;
            float       Pn = 0.0f;
        };

        Mask const* _mask = nullptr;
        std::map<WordEntity*, MaskArbiter> _maskArbiters;

        float GetInvMass(const WordEntity& w) {
            if (w.isHighlighted) return 0.0f;
            if (w.mass == 0.0f) return 0.0f;
            return 1.0f / w.mass;
        }

        // 单步物理模拟
        void Step(std::vector<WordEntity>& words, float dt, PhysicsParams& params) {
            if (dt <= 0 || words.empty()) return;

            // 施加力
            ApplyEdWordleForces(words, params);

            // 积分更新速度
            for (auto& w : words) {
                if (!w.isHighlighted) {
                    float invMass = GetInvMass(w);
                    if (invMass > 0.0f) {
                        w.velocity += w.forceAccumulator * invMass * dt;
                    }
                    w.velocity *= params.lambda;
                }
            }

            // 碰撞检测与求解
            SolveCollisions(words, dt, params);

            // 蒙版边界碰撞求解
            SolveMaskCollisions(words, dt, params);

            // 积分更新位置
            for (auto& w : words) {
                if (!w.isHighlighted) {
                    w.position += w.velocity * dt;
                }
            }

            // 增加帧计数器
            _frameCount++;
        }



        // 计算词 i 的邻居列表
        std::vector<size_t> FindNeighbors(std::vector<WordEntity> const& words, size_t i) {
            std::vector<size_t> neighbors;
            size_t n = words.size();

            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;

                glm::vec2 p1 = words[i].position;
                glm::vec2 p2 = words[j].position;

                // 检查连线是否与任何第三个词的 OBB 相交
                bool blocked = false;
                for (size_t k = 0; k < n; ++k) {
                    if (k == i || k == j) continue;

                    OBB obbK = GetWordOBB(words[k]);
                    if (LineIntersectsOBB(p1, p2, obbK)) {
                        blocked = true;
                        break;
                    }
                }

                if (!blocked) {
                    neighbors.push_back(j);
                }
            }

            return neighbors;
        }

        // 检查线段是否穿过蒙版无效区域
        // p1: 起点, p2: 终点, step: 采样步长 (像素)
        bool IsLineInMask(glm::vec2 p1, glm::vec2 p2, float step) const {
            if (!_mask) return true; // 无蒙版时默认都在“内”
            
            glm::vec2 d = p2 - p1;
            float len = glm::length(d);
            if (len < 1e-4f) return true; 

            // 步长太小会影响性能，太大漏检测
            if (step < 1.0f) step = 1.0f;

            int count = static_cast<int>(len / step);
            glm::vec2 dir = d / len;
            
            for (int k = 0; k <= count; ++k) {
                glm::vec2 p = p1 + dir * (float(k) * step);
                if (!_mask->IsInside(p)) return false;
            }
            // 确保终点被检测
            if (!_mask->IsInside(p2)) return false;
            
            return true;
        }

        // 施加 EdWordle 力 (Pixel Space Version)
        // 假设 Position/Velocity 为像素单位，Mass 为公制单位
        void ApplyEdWordleForces(std::vector<WordEntity>& words, PhysicsParams& params) {
            size_t n = words.size();
            float M = 1.0f; 
            float ppp = params.pixelsPerUnit > 0.0f ? params.pixelsPerUnit : 30.0f;

            // Decay factor: 1.0 / (iteration + 1)
            float decay = 1.0f / (float(_frameCount)/5 + 1.0f);

            for (size_t i = 0; i < n; ++i) {
                auto& w = words[i];
                if (w.isHighlighted) continue;
                w.clearAccumulators();

                glm::vec2 F_total_pixel(0.0f);

                // 1. 邻域力 (Simplified Repulsion/Attraction)
                if (params.kNeighbor > 0.0f) {
                    auto neighbors = FindNeighbors(words, i);
                    for (size_t j : neighbors) {
                        glm::vec2 delta = words[j].position - w.position; // Pixels
                        float r_pixel = glm::length(delta);
                        if (r_pixel < 1.0f) r_pixel = 1.0f; 
                        glm::vec2 dir = delta / r_pixel;

                        // F_metric = k * m1 * m2 / r_metric^2
                        // F_pixel = F_metric * ppp
                        float mag = params.kNeighbor * w.mass * words[j].mass * ppp * ppp * ppp / (r_pixel * r_pixel);
                        F_total_pixel += dir * mag;
                    }
                }

                // 2. 中心力 (Reference: EdWordle show.js)
                // F_metric = m * M * r_metric^2 * kCenter
                // F_pixel = F_metric * ppp = kCenter * m * M * r_pixel^2 / ppp
                glm::vec2 toCenter = params.canvasCenter - w.position; // Pixels
                float r_pixel = glm::length(toCenter);
                glm::vec2 dirCenter = (r_pixel > 1e-8f) ? toCenter / r_pixel : glm::vec2(0.0f);

                float magCenter = params.kCenter * w.mass * M * (r_pixel * r_pixel) / ppp;
                
                // [Mask Constraint] 如果到中心的路径被蒙版阻断（穿过无效区域），则不施加中心力
                // 防止词跨越空洞移动，保持形状
                if (_mask && magCenter > 0.0f) {
                    // 使用 10像素作为采样步长，平衡性能与精度
                    if (!IsLineInMask(w.position, params.canvasCenter, 10.0f)) {
                        magCenter = 0.0f;
                    }
                }

                F_total_pixel += dirCenter * magCenter;

                w.applyForce(F_total_pixel * decay);
            }
        }

        void SolveCollisions(std::vector<WordEntity>& words, float dt, PhysicsParams& params) {
            std::map<ArbiterKey, Arbiter> newArbiters;
            size_t n = words.size();
            float friction = 0.2f; // 给定一个默认摩擦系数

            // --- Broadphase & Narrowphase (生成 Arbiters) ---
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = i + 1; j < n; ++j) {
                    WordEntity& a = words[i];
                    WordEntity& b = words[j];

                    float invMassA = GetInvMass(a);
                    float invMassB = GetInvMass(b);

                    // 如果两个都是静态物体，则忽略
                    if (invMassA == 0.0f && invMassB == 0.0f) continue;

                    // 使用两级碰撞检测获取详细碰撞对
                    CollisionInfo info = CheckTwoLevelOBBCollisionDetailed(a, b);
                    if (!info.hasCollision) continue;

                    
                    glm::vec2 bestNormal(0.0f);
                    float maxDepth = -FLT_MAX;
                    bool found = false;

                    for (auto const& pair : info.pairs) {
                        glm::vec2 normal;
                        float depth;
                        if (ComputeOBBPenetration(pair.obbA, pair.obbB, normal, depth)) {
                            if (depth > maxDepth) {
                                maxDepth = depth;
                                bestNormal = normal;
                                found = true;
                            }
                        }
                    }

                    if (found) {
                        Arbiter arb;
                        arb.body1 = &a;
                        arb.body2 = &b;
                        arb.normal = bestNormal;
                        arb.separation = -maxDepth; 
                        arb.friction = friction;
                        arb.restitution = params.restitution;

                        // Warm Starting: 检查是否存在旧的 Arbiter
                        ArbiterKey key(&a, &b);
                        auto iter = _arbiters.find(key);
                        if (iter != _arbiters.end()) {
                            arb.Pn = iter->second.Pn;
                            arb.Pt = iter->second.Pt;
                        }

                        newArbiters.insert({key, arb});
                    }
                }
            }

            // 更新持久化存储 (移除不再碰撞的对，添加/更新碰撞对)
            _arbiters = newArbiters;

            // --- PreStep (计算质量和 Bias) & Warm Starting ---
            // 调整参数以适应像素坐标系：
            // 1. allowedPenetration: 从 0.01 增加到 2.0，允许轻微重叠以减少抖动
            // 2. biasFactor: 从 0.2 降低到 0.1，使位置修正更柔和
            float k_allowedPenetration = 0.0f;
            float k_biasFactor = 0.1f; 
            float inv_dt = dt > 0.0f ? 1.0f / dt : 0.0f;

            for (auto& [key, arb] : _arbiters) {
                WordEntity* b1 = arb.body1;
                WordEntity* b2 = arb.body2;
                float invMass1 = GetInvMass(*b1);
                float invMass2 = GetInvMass(*b2);

                // 计算有效质量
                float kNormal = invMass1 + invMass2;
                arb.massNormal = kNormal > 0.0f ? 1.0f / kNormal : 0.0f;

                float kTangent = invMass1 + invMass2;
                arb.massTangent = kTangent > 0.0f ? 1.0f / kTangent : 0.0f;

                // 计算 Bias (位置修正)
                arb.bias = -k_biasFactor * inv_dt * std::min(0.0f, arb.separation + k_allowedPenetration);

                // --- Warm Starting: 应用上一帧的冲量 ---
                glm::vec2 P = arb.Pn * arb.normal + arb.Pt * Cross(arb.normal, 1.0f);
                b1->velocity -= P * invMass1;
                b2->velocity += P * invMass2;
            }

            // --- ApplyImpulse (迭代求解) ---
            int iterations = 20; // 增加迭代次数以提高致密堆叠的稳定性
            for (int i = 0; i < iterations; ++i) {
                for (auto& [key, arb] : _arbiters) {
                    WordEntity* b1 = arb.body1;
                    WordEntity* b2 = arb.body2;
                    float invMass1 = GetInvMass(*b1);
                    float invMass2 = GetInvMass(*b2);

                    // 1. Normal Impulse (法向脉冲)
                    glm::vec2 dv = b2->velocity - b1->velocity;
                    
                    float vn = Dot(dv, arb.normal);
                    float dPn = arb.massNormal * (-vn + arb.bias);

                    if (params.restitution > 0.0f) {
                         dPn -= arb.massNormal * params.restitution * std::min(vn, 0.0f); 
                    }

                    // Clamp
                    float Pn0 = arb.Pn;
                    arb.Pn = std::max(Pn0 + dPn, 0.0f);
                    dPn = arb.Pn - Pn0;

                    // 应用法向脉冲
                    glm::vec2 Pn = dPn * arb.normal;
                    b1->velocity -= Pn * invMass1;
                    b2->velocity += Pn * invMass2;

                    // 2. Tangent Impulse (Friction / 切向摩擦)
                    dv = b2->velocity - b1->velocity;
                    glm::vec2 tangent = Cross(arb.normal, 1.0f);
                    float vt = Dot(dv, tangent);
                    float dPt = arb.massTangent * (-vt);

                    float maxPt = arb.friction * arb.Pn;

                    // Clamp friction
                    float oldPt = arb.Pt;
                    arb.Pt = std::clamp(oldPt + dPt, -maxPt, maxPt);
                    dPt = arb.Pt - oldPt;

                    // 应用切向脉冲
                    glm::vec2 Pt = dPt * tangent;
                    b1->velocity -= Pt * invMass1;
                    b2->velocity += Pt * invMass2;
                }
            }
        }

        // ============================================================
        // 蒙版边界碰撞求解（使用 SDF）
        // ============================================================
        void SolveMaskCollisions(std::vector<WordEntity>& words, float dt, PhysicsParams& params) {
            if (!_mask || !_mask->HasSDF()) return;

            std::map<WordEntity*, MaskArbiter> newArbiters;

            // 获取画布高度，用于坐标系翻转
            // Physics: Y 轴向上 (Bottom-Left)
            // Mask:    Y 轴向下 (Top-Left)
            float canvasHeight = static_cast<float>(_mask->GetCanvasSize().y);

            // --- Broadphase & Narrowphase ---
            for (auto& w : words) {
                w.maskCollision = false; // 重置碰撞标记

                if (w.isHighlighted) continue;

                // 获取词的 OBB
                OBB obb = GetWordOBB(w);
                auto corners = obb.corners();

                // 找到最深的穿透点
                float maxPenetration = 0.0f;
                glm::vec2 bestNormal(0.0f);
                glm::vec2 collisionPoint(0.0f);
                bool hasCollision = false;

                // 增加采样点：角点 + 中心点 + 边中点
                std::vector<glm::vec2> testPoints;
                testPoints.reserve(9);
                for (auto& c : corners) testPoints.push_back(c);
                testPoints.push_back(obb.center);
                glm::vec2 axisX = obb.axisX() * obb.halfSize.x;
                glm::vec2 axisY = obb.axisY() * obb.halfSize.y;
                testPoints.push_back(obb.center + axisX);
                testPoints.push_back(obb.center - axisX);
                testPoints.push_back(obb.center + axisY);
                testPoints.push_back(obb.center - axisY);

                for (auto& p : testPoints) {
                    // 1. 坐标翻转：Physics (Bottom-Left) -> Mask (Top-Left)
                    glm::vec2 p_mask = p;
                    // Physics Y is up, Mask Y is down.
                    p_mask.y = canvasHeight - p.y;

                    // 使用 SDF 获取精确距离
                    float sdfDist = _mask->GetSDFDistanceCanvas(p_mask);

                    // 穿透判定 (sdfDist > 0 表示在外部)
                    if (sdfDist > 0) {
                        hasCollision = true;

                        // 2. 获取 Mask 坐标系下的法线
                        glm::vec2 normalMask = _mask->GetSDFNormal(p_mask);

                        // 3. 法线翻转：Mask (Top-Left) -> Physics (Bottom-Left)
                        // Y 轴方向相反，导数取反
                        glm::vec2 normalPhys = normalMask;
                        normalPhys.y = -normalMask.y; 
                        
                        // 归一化以防万一
                        if (glm::length(normalPhys) > 1e-6) {
                            normalPhys = glm::normalize(normalPhys);
                        }

                        // 穿透深度
                        float penetration = sdfDist;

                        if (penetration > maxPenetration) {
                            maxPenetration = penetration;
                            bestNormal = normalPhys;
                            collisionPoint = p;
                        }
                    }
                }

                if (hasCollision) {
                    // 更新调试信息
                    w.maskCollision = true;
                    w.maskCollisionPoint = collisionPoint;
                    w.maskCollisionNormal = bestNormal;
                    
                    MaskArbiter arb;
                    arb.body = &w;
                    arb.normal = bestNormal;
                    arb.separation = -maxPenetration;  // 穿透深度记为负值，用于 Solver
                    arb.bias = 0.0f;

                    // Warm Starting: 检查是否存在旧的 Arbiter
                    auto iter = _maskArbiters.find(&w);
                    if (iter != _maskArbiters.end()) {
                        arb.Pn = iter->second.Pn;
                    }

                    newArbiters.insert({&w, arb});
                }
            }

            // 更新持久化存储
            _maskArbiters = newArbiters;

            // --- PreStep & Warm Starting ---
            float k_allowedPenetration = 0.5f; // 允许少量穿透以减少抖动 (px)
            float k_biasFactor = 0.2f;         // 降低 Bias 系数 (0.6 -> 0.2) 以减少震荡
            float inv_dt = dt > 0.0f ? 1.0f / dt : 0.0f;

            for (auto& [body, arb] : _maskArbiters) {
                float invMass = GetInvMass(*body);
                if (invMass == 0.0f) continue;

                // 计算有效质量
                arb.massNormal = invMass > 0.0f ? 1.0f / invMass : 0.0f;

                // 计算 Bias
                arb.bias = -k_biasFactor * inv_dt * std::min(0.0f, arb.separation + k_allowedPenetration);

                // Warm Starting: 应用上一帧的脉冲
                body->velocity += arb.Pn * arb.normal * invMass;
            }

            // --- ApplyImpulse（迭代求解）---
            // 增加迭代次数 (10 -> 20)，保证复杂边界（如茶壶柄）的约束能被满足
            int iterations = 20;  
            for (int i = 0; i < iterations; ++i) {
                for (auto& [body, arb] : _maskArbiters) {
                    float invMass = GetInvMass(*body);
                    if (invMass == 0.0f) continue;

                    // 计算相对速度（这里只有被约束物体的速度）
                    float vn = glm::dot(body->velocity, arb.normal);

                    // 计算法向脉冲
                    float dPn = arb.massNormal * (-vn + arb.bias);

                    // Clamp
                    float oldPn = arb.Pn;
                    arb.Pn = std::max(oldPn + dPn, 0.0f);
                    dPn = arb.Pn - oldPn;

                    // 应用脉冲
                    body->velocity += dPn * arb.normal * invMass;
                }
            }
        }
    };

} // namespace VCX::Labs::labf
