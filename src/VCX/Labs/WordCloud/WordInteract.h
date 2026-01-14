#pragma once

#include <cmath>
#include <vector>
#include <algorithm>
#include <functional>
#include <imgui.h>
#include <glm/glm.hpp>

#include "Labs/WordCloud/WordEntity.h"

namespace VCX::Labs::labf {

    constexpr float kPi = 3.14159265358979323846f;

    // 文字测量回调类型
    using MeasureTextFunc = std::function<glm::vec2(const std::string&, float)>;

    //=========================================================================
    // Gizmo State（使用索引而非指针）
    //=========================================================================

    struct GizmoState {
        std::vector<size_t> selectedIndices;  // 选中的词索引列表

        // 拖动状态
        enum class DragMode { None, Move, Scale, Rotate };
        DragMode dragMode = DragMode::None;

        // 缩放相关
        int activeCorner = -1;
        glm::vec2 dragStartPos {0, 0};
        std::vector<float> dragStartFontSizes;

        // 旋转相关
        float lastMouseAngle = 0.f;
        std::vector<float> dragStartOrientations;
        ImVec2 rotateCenter {0, 0};  // 旋转时固定的中心点（屏幕坐标）
    };

    // 交互结果（增量变化）
    struct InteractionResult {
        bool handled = false;

        enum class ChangeType { None, Select, Deselect, Move, Scale, Rotate };
        ChangeType changeType = ChangeType::None;

        // 变更的词索引
        std::vector<size_t> changedIndices;

        // 高亮变化（Select/Deselect）
        std::vector<size_t> highlightOn;   // 新增高亮
        std::vector<size_t> highlightOff;  // 取消高亮

        // 位移增量（Move）
        glm::vec2 positionDelta { 0, 0 };

        // 角度增量（Rotate）
        float orientationDelta = 0.0f;

        // 缩放比例（Scale）
        float scaleFactor = 1.0f;

        // 新字号和 boxHalfSize（Scale）
        std::vector<std::pair<float, glm::vec2>> newFontSizes;  // (fontSize, boxHalfSize)
    };

    //=========================================================================
    // Hit Zone
    //=========================================================================

    enum class HitZone { None, Body, Rotate, Corner0, Corner1, Corner2, Corner3 };

    //=========================================================================
    // Helper Functions
    //=========================================================================

    inline void DrawDashedLine(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 col, float thickness = 1.5f, float dashLen = 5.0f) {
        ImVec2 dir = ImVec2(p2.x - p1.x, p2.y - p1.y);
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 0.001f) return;
        dir.x /= len; dir.y /= len;
        float drawn = 0.f;
        bool draw = true;
        while (drawn < len) {
            float segLen = std::min(dashLen, len - drawn);
            if (draw) {
                ImVec2 a = ImVec2(p1.x + dir.x * drawn, p1.y + dir.y * drawn);
                ImVec2 b = ImVec2(p1.x + dir.x * (drawn + segLen), p1.y + dir.y * (drawn + segLen));
                dl->AddLine(a, b, col, thickness);
            }
            drawn += dashLen;
            draw = !draw;
        }
    }

    inline float DistSq(ImVec2 a, ImVec2 b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    inline bool PointInRotatedRect(ImVec2 p, ImVec2 corners[4]) {
        auto cross = [](ImVec2 o, ImVec2 a, ImVec2 b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        float c0 = cross(corners[0], corners[1], p);
        float c1 = cross(corners[1], corners[2], p);
        float c2 = cross(corners[2], corners[3], p);
        float c3 = cross(corners[3], corners[0], p);
        return (c0 >= 0 && c1 >= 0 && c2 >= 0 && c3 >= 0) ||
               (c0 <= 0 && c1 <= 0 && c2 <= 0 && c3 <= 0);
    }

    //=========================================================================
    // Collision Box Drawing (调试用)
    //=========================================================================

    // 绘制 OBB（用红色实线）
    inline void DrawOBB(ImDrawList* dl, ImVec2 canvasOrigin, float canvasHeight, OBB const& obb, ImU32 color) {
        auto toScreen = [&](glm::vec2 p) {
            return ImVec2(canvasOrigin.x + p.x, canvasOrigin.y + (canvasHeight - p.y));
        };

        auto corners = obb.corners();
        ImVec2 screenCorners[4];
        for (int i = 0; i < 4; ++i) {
            screenCorners[i] = toScreen(corners[i]);
        }

        for (int i = 0; i < 4; ++i) {
            dl->AddLine(screenCorners[i], screenCorners[(i + 1) % 4], color, 1.5f);
        }
    }

    // 绘制词的碰撞框
    inline void DrawCollisionBox(ImDrawList* dl, ImVec2 canvasOrigin, float canvasHeight, const WordEntity& w) {
        ImU32 redColor = IM_COL32(255, 50, 50, 255);
        ImU32 orangeColor = IM_COL32(255, 150, 50, 200);  // 字符级用橙色

        float rad = -w.orientation * 3.14159265f / 180.0f;

        if (w.useTwoLevelBox) {
            // 词级 OBB（x-height 区域，红色）
            float c = std::cos(rad), s = std::sin(rad);
            // 使用 xHeightCenterY（与字符级 OBB 计算方式一致）
            glm::vec2 xHeightOffset(0.0f, w.xHeightCenterY);
            // 旋转偏移
            glm::vec2 rotatedOffset(
                xHeightOffset.x * c - xHeightOffset.y * s,
                xHeightOffset.x * s + xHeightOffset.y * c
            );
            OBB wordOBB;
            wordOBB.center = w.position + rotatedOffset;
            wordOBB.halfSize = glm::vec2(w.boxHalfSize.x, w.xHeight * 0.5f);
            wordOBB.rotation = rad;
            DrawOBB(dl, canvasOrigin, canvasHeight, wordOBB, redColor);

            // 字符级 OBB（橙色）
            for (auto const& letter : w.letterOBBs) {
                // 将 localCenter 旋转到世界坐标
                glm::vec2 rotatedCenter(
                    letter.localCenter.x * c - letter.localCenter.y * s,
                    letter.localCenter.x * s + letter.localCenter.y * c
                );

                OBB letterOBB;
                letterOBB.center = w.position + rotatedCenter;
                letterOBB.halfSize = letter.halfSize;
                letterOBB.rotation = rad;
                DrawOBB(dl, canvasOrigin, canvasHeight, letterOBB, orangeColor);
            }
        } else {
            // 普通：显示 collisionHalfSize
            OBB collisionOBB;
            collisionOBB.center = w.position;
            collisionOBB.halfSize = w.boxHalfSize;
            collisionOBB.rotation = rad;
            DrawOBB(dl, canvasOrigin, canvasHeight, collisionOBB, redColor);
        }
    }

    //=========================================================================
    // Gizmo Drawing
    //=========================================================================

    inline void DrawGizmo(ImDrawList* dl, ImVec2 canvasOrigin, float canvasHeight, const WordEntity& w) {
        auto toScreen = [&](glm::vec2 p) {
            return ImVec2(canvasOrigin.x + p.x, canvasOrigin.y + (canvasHeight - p.y));
        };

        glm::vec2 c = w.position;
        glm::vec2 half = w.boxHalfSize;  // 使用交互 OBB（fullHeight）
        float rad = glm::radians(-w.orientation);
        auto rot = [&](glm::vec2 v) {
            return glm::vec2(v.x * std::cos(rad) - v.y * std::sin(rad),
                             v.x * std::sin(rad) + v.y * std::cos(rad));
        };

        glm::vec2 corners[4] = {
            c + rot({-half.x, -half.y}),
            c + rot({ half.x, -half.y}),
            c + rot({ half.x,  half.y}),
            c + rot({-half.x,  half.y})
        };

        // 使用词的颜色（带透明度）
        ImU32 lineCol = IM_COL32(
            static_cast<int>(w.color.r * 255),
            static_cast<int>(w.color.g * 255),
            static_cast<int>(w.color.b * 255),
            200
        );

        // 绘制实线边框
        for (int i = 0; i < 4; ++i) {
            dl->AddLine(toScreen(corners[i]), toScreen(corners[(i + 1) % 4]), lineCol, 1.5f);
        }
    }

    //=========================================================================
    // Hit Testing
    //=========================================================================

    inline HitZone HitTestGizmo(ImVec2 mousePos, ImVec2 canvasOrigin, float canvasHeight, const WordEntity& w) {
        auto toScreen = [&](glm::vec2 p) {
            return ImVec2(canvasOrigin.x + p.x, canvasOrigin.y + (canvasHeight - p.y));
        };

        glm::vec2 c = w.position;
        glm::vec2 half = w.boxHalfSize;  // 使用交互 OBB（fullHeight）
        float rad = glm::radians(-w.orientation);
        auto rot = [&](glm::vec2 v) {
            return glm::vec2(v.x * std::cos(rad) - v.y * std::sin(rad),
                             v.x * std::sin(rad) + v.y * std::cos(rad));
        };

        glm::vec2 gcorners[4] = {
            c + rot({-half.x, -half.y}),
            c + rot({ half.x, -half.y}),
            c + rot({ half.x,  half.y}),
            c + rot({-half.x,  half.y})
        };

        ImVec2 corners[4];
        for (int i = 0; i < 4; ++i) {
            corners[i] = toScreen(gcorners[i]);
        }

        const float cornerRadius = 6.0f;
        // 旋转区域按词的大小比例计算，最小 15 像素，最大 50 像素
        float halfSize = std::max(half.x, half.y);
        float rotateBufferRadius = std::clamp(halfSize * 0.25f, 15.0f, 50.0f);

        for (int i = 0; i < 4; ++i) {
            float distSq = DistSq(mousePos, corners[i]);
            if (distSq < cornerRadius * cornerRadius) {
                return static_cast<HitZone>(static_cast<int>(HitZone::Corner0) + i);
            }
            if (distSq < rotateBufferRadius * rotateBufferRadius) {
                return HitZone::Rotate;
            }
        }

        if (PointInRotatedRect(mousePos, corners)) {
            return HitZone::Body;
        }

        return HitZone::None;
    }

    inline bool IsPointInWord(ImVec2 mousePos, ImVec2 canvasOrigin, float canvasHeight, const WordEntity& w) {
        auto toScreen = [&](glm::vec2 p) {
            return ImVec2(canvasOrigin.x + p.x, canvasOrigin.y + (canvasHeight - p.y));
        };

        glm::vec2 c = w.position;
        glm::vec2 half = w.boxHalfSize;  // 使用交互 OBB（fullHeight）
        float rad = glm::radians(-w.orientation);
        auto rot = [&](glm::vec2 v) {
            return glm::vec2(v.x * std::cos(rad) - v.y * std::sin(rad),
                             v.x * std::sin(rad) + v.y * std::cos(rad));
        };

        ImVec2 corners[4] = {
            toScreen(c + rot({-half.x, -half.y})),
            toScreen(c + rot({ half.x, -half.y})),
            toScreen(c + rot({ half.x,  half.y})),
            toScreen(c + rot({-half.x,  half.y}))
        };

        return PointInRotatedRect(mousePos, corners);
    }

    //=========================================================================
    // Main Interaction Handler（返回增量变化）
    //=========================================================================

    inline InteractionResult HandleWordGizmo(
        std::vector<WordEntity> const& words,  // 只读
        GizmoState& state,
        ImVec2 canvasOrigin,
        ImVec2 canvasSize,
        bool enableLeftDrag,
        MeasureTextFunc measureText
    ) {
        InteractionResult result;
        ImGuiMouseButton btn = enableLeftDrag ? ImGuiMouseButton_Left : ImGuiMouseButton_Right;
        ImVec2 mousePos = ImGui::GetMousePos();
        bool ctrlHeld = ImGui::GetIO().KeyCtrl;
        float canvasHeight = canvasSize.y;

        auto toScreen = [&](glm::vec2 p) {
            return ImVec2(canvasOrigin.x + p.x, canvasOrigin.y + (canvasHeight - p.y));
        };

        // 鼠标按下：选择或开始拖动
        if (ImGui::IsMouseClicked(btn)) {
            // 1. 检测是否点击了选中词的 Gizmo
            for (size_t idx : state.selectedIndices) {
                if (idx >= words.size()) continue;
                HitZone zone = HitTestGizmo(mousePos, canvasOrigin, canvasHeight, words[idx]);
                if (zone != HitZone::None) {
                    state.dragStartPos = glm::vec2(mousePos.x, mousePos.y);

                    if (zone == HitZone::Body) {
                        state.dragMode = GizmoState::DragMode::Move;
                    } else if (zone == HitZone::Rotate) {
                        state.dragMode = GizmoState::DragMode::Rotate;
                        ImVec2 center = toScreen(words[idx].position);
                        state.rotateCenter = center;  // 记录旋转开始时的中心点
                        state.lastMouseAngle = std::atan2(mousePos.y - center.y, mousePos.x - center.x);
                        state.dragStartOrientations.clear();
                        for (size_t si : state.selectedIndices) {
                            if (si < words.size()) {
                                state.dragStartOrientations.push_back(words[si].orientation);
                            }
                        }
                    } else {
                        state.dragMode = GizmoState::DragMode::Scale;
                        state.activeCorner = static_cast<int>(zone) - static_cast<int>(HitZone::Corner0);
                        state.dragStartFontSizes.clear();
                        for (size_t si : state.selectedIndices) {
                            if (si < words.size()) {
                                state.dragStartFontSizes.push_back(words[si].fontSize);
                            }
                        }
                    }
                    result.handled = true;
                    break;
                }
            }

            // 2. 检测是否点击了其他词
            if (!result.handled) {
                for (size_t i = 0; i < words.size(); ++i) {
                    if (IsPointInWord(mousePos, canvasOrigin, canvasHeight, words[i])) {
                        if (ctrlHeld) {
                            // 多选：切换选中状态
                            auto it = std::find(state.selectedIndices.begin(), state.selectedIndices.end(), i);
                            if (it != state.selectedIndices.end()) {
                                state.selectedIndices.erase(it);
                                result.highlightOff.push_back(i);
                            } else {
                                state.selectedIndices.push_back(i);
                                result.highlightOn.push_back(i);
                            }
                        } else {
                            // 单选：清除其他，选中当前
                            result.highlightOff = state.selectedIndices;
                            state.selectedIndices.clear();
                            state.selectedIndices.push_back(i);
                            result.highlightOn.push_back(i);
                            // 从 highlightOff 中移除新选中的
                            auto it = std::find(result.highlightOff.begin(), result.highlightOff.end(), i);
                            if (it != result.highlightOff.end()) {
                                result.highlightOff.erase(it);
                            }
                        }
                        result.handled = true;
                        result.changeType = InteractionResult::ChangeType::Select;
                        break;
                    }
                }
            }

            // 3. 点击空白：取消选择
            if (!result.handled && !ctrlHeld && !state.selectedIndices.empty()) {
                result.highlightOff = state.selectedIndices;
                state.selectedIndices.clear();
                result.handled = true;
                result.changeType = InteractionResult::ChangeType::Deselect;
            }
        }

        // 拖动中：计算增量变化
        if (ImGui::IsMouseDragging(btn) && state.dragMode != GizmoState::DragMode::None) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            result.changedIndices = state.selectedIndices;

            switch (state.dragMode) {
            case GizmoState::DragMode::Move:
                result.changeType = InteractionResult::ChangeType::Move;
                result.positionDelta = glm::vec2(delta.x, -delta.y);  // Y 翻转
                result.handled = true;
                break;

            case GizmoState::DragMode::Scale:
                if (!state.selectedIndices.empty() && state.selectedIndices[0] < words.size()) {
                    size_t primaryIdx = state.selectedIndices[0];
                    ImVec2 center = toScreen(words[primaryIdx].position);
                    glm::vec2 startPos = state.dragStartPos;
                    float initialDist = std::sqrt((startPos.x - center.x) * (startPos.x - center.x) +
                                                   (startPos.y - center.y) * (startPos.y - center.y));
                    float currentDist = std::sqrt((mousePos.x - center.x) * (mousePos.x - center.x) +
                                                   (mousePos.y - center.y) * (mousePos.y - center.y));
                    if (initialDist > 1.0f) {
                        float scaleFactor = std::clamp(currentDist / initialDist, 0.2f, 5.0f);
                        result.changeType = InteractionResult::ChangeType::Scale;
                        result.scaleFactor = scaleFactor;

                        // 计算每个词的新字号和 boxHalfSize
                        for (size_t i = 0; i < state.selectedIndices.size() && i < state.dragStartFontSizes.size(); ++i) {
                            size_t idx = state.selectedIndices[i];
                            if (idx >= words.size()) continue;
                            float newFontSize = std::clamp(state.dragStartFontSizes[i] * scaleFactor, 8.0f, 200.0f);
                            glm::vec2 size = measureText(words[idx].text, newFontSize);
                            result.newFontSizes.push_back({newFontSize, size * 0.5f});
                        }
                    }
                }
                result.handled = true;
                break;

            case GizmoState::DragMode::Rotate:
                if (!state.selectedIndices.empty() && state.selectedIndices[0] < words.size()) {
                    // 使用开始旋转时记录的固定中心点，而不是实时词位置
                    ImVec2 center = state.rotateCenter;
                    float currentAngle = std::atan2(mousePos.y - center.y, mousePos.x - center.x);
                    float deltaAngle = currentAngle - state.lastMouseAngle;

                    if (deltaAngle > kPi) deltaAngle -= 2 * kPi;
                    if (deltaAngle < -kPi) deltaAngle += 2 * kPi;

                    result.changeType = InteractionResult::ChangeType::Rotate;
                    result.orientationDelta = glm::degrees(deltaAngle);
                    state.lastMouseAngle = currentAngle;
                }
                result.handled = true;
                break;

            default:
                break;
            }
        }

        // 释放鼠标：结束拖动
        if (ImGui::IsMouseReleased(btn)) {
            state.dragMode = GizmoState::DragMode::None;
            state.activeCorner = -1;
        }

        return result;
    }

} // namespace VCX::Labs::labf
