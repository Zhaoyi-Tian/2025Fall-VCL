#include <algorithm>
#include <cstring>
#include <cstdio>
#include <chrono>

#include "Labs/WordCloud/CaseWC.h"
#include "Labs/Common/ImGuiHelper.h"
#include "Labs/WordCloud/WordInteract.h"
#include "Labs/WordCloud/WordCloudRenderer.h"

namespace VCX::Labs::labf {

    static constexpr auto c_Size = std::pair(1150U, 800U);


    WordCloud::WordCloud():
        _texture({ .MinFilter = Engine::GL::FilterMode::Linear, .MagFilter = Engine::GL::FilterMode::Nearest }),
        _empty(Common::CreateCheckboardImageRGB(c_Size.first, c_Size.second)),
        _wordCloudRenderer(std::make_unique<WordCloudRenderer>()),
        _currentFontIndex(DefaultWordCloudFontIndex) {

        float cx = c_Size.first * 0.5f;
        float cy = c_Size.second * 0.5f;

        auto& w1 = _wm.add("Hello", 48.0f);
        w1.color = { 0.2f, 0.6f, 1.0f, 1.0f };
        w1.position = { cx - 100, cy - 50 };
        w1.wordVector = { 0.9f, 0.8f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f };  // 问候语

        auto& w2 = _wm.add("世界", 42.0f);
        w2.color = { 1.0f, 0.4f, 0.2f, 1.0f };
        w2.position = { cx + 80, cy - 30 };
        w2.wordVector = { 0.85f, 0.75f, 0.15f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f };  // 问候语（与Hello相似）

        auto& w3 = _wm.add("WordCloud", 36.0f);
        w3.color = { 0.3f, 0.8f, 0.4f, 1.0f };
        w3.position = { cx, cy + 60 };
        w3.wordVector = { 0.1f, 0.1f, 0.8f, 0.7f, 0.6f, 0.5f, 0.0f, 0.0f };  // 图形技术

        auto& w4 = _wm.add("物理模拟", 32.0f);
        w4.color = { 0.9f, 0.7f, 0.1f, 1.0f };
        w4.position = { cx - 120, cy + 100 };
        w4.wordVector = { 0.0f, 0.0f, 0.2f, 0.3f, 0.1f, 0.1f, 0.9f, 0.85f };  // 物理（独立）

        auto& w5 = _wm.add("OpenGL", 28.0f);
        w5.color = { 0.6f, 0.2f, 0.8f, 1.0f };
        w5.position = { cx + 100, cy + 80 };
        w5.wordVector = { 0.05f, 0.1f, 0.75f, 0.8f, 0.7f, 0.6f, 0.0f, 0.0f };  // 图形技术（与WordCloud相似）

        auto& w6 = _wm.add("MSDF", 24.0f);
        w6.color = { 0.1f, 0.5f, 0.7f, 1.0f };
        w6.position = { cx - 80, cy - 120 };
        w6.wordVector = { 0.1f, 0.05f, 0.7f, 0.75f, 0.65f, 0.55f, 0.0f, 0.0f };  // 图形技术

        // 预计算相似度矩阵
        _wm.recomputeSimilarityMatrix();

        // OBB 会在 OnRender 中使用 MeasureTextDetailed 更新为真实尺寸
    }

    void WordCloud::OnSetupPropsUI() {
        // === 背景设置 ===
        bool colorChanged = ImGui::ColorEdit3("背景色", (float*)&_bgColor);
        bool alphaChanged = ImGui::SliderFloat("透明度", &_bgAlpha, 0.0f, 1.0f, "%.2f");

        if (colorChanged || alphaChanged) {
            _recompute = true;
        }

        Common::ImGuiHelper::SaveImage(_texture, c_Size);

        ImGui::Separator();

        // === 全局角度控制 ===
        if (ImGui::SliderFloat("锁定角度", &_lockAngle, 0.0f, 360.0f, "%.1f")) {
            for (auto& w : _wm.items()) {
                w.orientation = _lockAngle;
            }
            // 同步到物理线程：只更新角度，不覆盖其他属性
            if (_enablePhysics && _physicsThread.IsRunning()) {
                for (size_t i = 0; i < _wm.items().size(); ++i) {
                    _physicsThread.UpdateWordOrientation(i, _lockAngle);
                }
            }
            _recompute = true;
        }

        ImGui::Separator();

        // === 物理模拟控制 ===
        if (ImGui::Checkbox("启用物理", &_enablePhysics)) {
            if (_enablePhysics && _physicsInitialized) {
                // 重新启动物理线程，从当前 _wm 状态开始
                _physicsThread.Start(_wm.items(), _physicsParams);
            } else {
                // 停止物理线程前，把当前状态同步回 _wm
                if (_physicsThread.IsRunning()) {
                    auto const& currentState = _physicsThread.GetReadBuffer();
                    auto& items = _wm.items();
                    for (size_t i = 0; i < std::min(currentState.size(), items.size()); ++i) {
                        items[i].position = currentState[i].position;
                        items[i].orientation = currentState[i].orientation;
                        items[i].fontSize = currentState[i].fontSize;
                        items[i].color = currentState[i].color;
                        items[i].boxHalfSize = currentState[i].boxHalfSize;
                        items[i].useTwoLevelBox = currentState[i].useTwoLevelBox;
                        items[i].xHeight = currentState[i].xHeight;
                        items[i].mass = currentState[i].mass;
                    }
                }
                _physicsThread.Stop();
            }
        }
        if (_enablePhysics) {
            ImGui::Text("EdWordle 参数");
            bool paramsChanged = false;
            paramsChanged |= ImGui::SliderFloat("中心力权重", &_physicsParams.alpha, 0.0f, 1.0f);
            paramsChanged |= ImGui::SliderFloat("速度阻尼", &_physicsParams.lambda, 0.5f, 0.99f);
            paramsChanged |= ImGui::SliderFloat("弹性系数", &_physicsParams.restitution, 0.0f, 1.0f);

            // 物理频率调节（以 Hz 显示，内部转换为 fixedDt）
            float physicsHz = 1.0f / _physicsParams.fixedDt;
            if (ImGui::SliderFloat("物理频率", &physicsHz, 30.0f, 240.0f, "%.0f Hz")) {
                _physicsParams.fixedDt = 1.0f / physicsHz;
                paramsChanged = true;
            }

            if (paramsChanged) {
                _physicsThread.SetParams(_physicsParams);
                _physicsThread.ResetSimulator();  // 参数变化时重置 t
            }

            if (ImGui::Button("重置模拟")) {
                _physicsThread.ResetSimulator();
            }
        }

        ImGui::Separator();

        // === 字体设置 ===
        ImGui::Text("词云字体");
        const auto& fonts = GetWordCloudFonts();
        if (!fonts.empty() && _currentFontIndex < fonts.size()) {
            if (ImGui::BeginCombo("##font", fonts[_currentFontIndex].name.c_str())) {
                for (std::size_t i = 0; i < fonts.size(); ++i) {
                    if (ImGui::Selectable(fonts[i].name.c_str(), i == _currentFontIndex)) {
                        if (i != _currentFontIndex) {
                            _currentFontIndex = i;
                            _wordCloudRenderer->SetFont(fonts[i].path);
                            // 换字体时刷新所有词的三级 OBB
                            float maxFontSize = ComputeMaxFontSize();
                            for (auto& w : _wm.items()) {
                                InitializeWordOBBs(w, maxFontSize);
                            }
                            // 如果物理线程运行中，同步 OBB 更新并重置模拟
                            if (_enablePhysics && _physicsThread.IsRunning()) {
                                for (size_t idx = 0; idx < _wm.items().size(); ++idx) {
                                    _physicsThread.UpdateWordOBB(idx, _wm.items()[idx]);
                                }
                                _physicsThread.ResetSimulator();
                            }
                            _recompute = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("未找到字体");
        }

        ImGui::Separator();

        // === 调试选项 ===
        ImGui::Checkbox("显示碰撞框", &_showCollisionBox);

        ImGui::Separator();

        // === 添加词 ===
        ImGui::Text("添加词");
        ImGui::InputText("文本##new", _newWordText, sizeof(_newWordText));
        ImGui::ColorEdit4("颜色##new", (float*)&_newWordColor, ImGuiColorEditFlags_AlphaBar);
        ImGui::SliderFloat("字号##new", &_newWordFontSize, 8.0f, 100.0f);

        if (ImGui::Button("+ 添加") && strlen(_newWordText) > 0) {
            auto& w = _wm.add(_newWordText, _newWordFontSize);
            w.color = glm::vec4(_newWordColor.x, _newWordColor.y, _newWordColor.z, _newWordColor.w);
            w.position = glm::vec2(c_Size.first * 0.5f, c_Size.second * 0.5f);

            // 使用三级 OBB 初始化
            float maxFontSize = ComputeMaxFontSize();
            InitializeWordOBBs(w, maxFontSize);

            // 同步到物理线程
            if (_enablePhysics && _physicsThread.IsRunning()) {
                _physicsThread.AddWord(w);
            }

            _newWordText[0] = '\0';  // 清空输入
            _recompute = true;
        }

        ImGui::Separator();

        // === 选中词属性 ===
        // 获取当前词数据来源
        std::vector<WordEntity> const* wordsPtr = nullptr;
        if (_enablePhysics && _physicsThread.IsRunning()) {
            wordsPtr = &_physicsThread.GetReadBuffer();
        } else {
            wordsPtr = &_wm.items();
        }

        if (!_gizmoState.selectedIndices.empty() && wordsPtr != nullptr) {
            ImGui::Text("选中词属性 (%zu个)", _gizmoState.selectedIndices.size());

            size_t primaryIdx = _gizmoState.selectedIndices[0];
            if (primaryIdx < wordsPtr->size()) {
                WordEntity const& w = (*wordsPtr)[primaryIdx];

                // 文本编辑（仅单选时）- 注意：文本修改需要重建，暂时禁用物理模式下的文本编辑
                if (_gizmoState.selectedIndices.size() == 1 && !_enablePhysics) {
                    static char editBuf[256];
                    strncpy(editBuf, w.text.c_str(), sizeof(editBuf) - 1);
                    editBuf[sizeof(editBuf) - 1] = '\0';
                    if (ImGui::InputText("文本##edit", editBuf, sizeof(editBuf))) {
                        auto& mutableW = _wm.items()[primaryIdx];
                        mutableW.text = editBuf;
                        // 使用三级 OBB 更新
                        float maxFontSize = ComputeMaxFontSize();
                        InitializeWordOBBs(mutableW, maxFontSize);
                        _recompute = true;
                    }
                }

                // 颜色编辑（应用于所有选中）- 颜色不影响物理，直接修改 _wm
                ImVec4 col(w.color.r, w.color.g, w.color.b, w.color.a);
                if (ImGui::ColorEdit4("颜色##edit", (float*)&col, ImGuiColorEditFlags_AlphaBar)) {
                    glm::vec4 newColor(col.x, col.y, col.z, col.w);
                    for (size_t idx : _gizmoState.selectedIndices) {
                        if (idx < _wm.items().size()) {
                            _wm.items()[idx].color = newColor;
                        }
                        // 同步颜色到物理线程
                        if (_enablePhysics && _physicsThread.IsRunning()) {
                            _physicsThread.UpdateWordColor(idx, newColor);
                        }
                    }
                    _recompute = true;
                }

                // 字号编辑（应用于所有选中）
                float fontSize = w.fontSize;
                if (ImGui::SliderFloat("字号##edit", &fontSize, 8.0f, 100.0f)) {
                    float maxFontSize = std::max(ComputeMaxFontSize(), fontSize);
                    for (size_t idx : _gizmoState.selectedIndices) {
                        if (idx < _wm.items().size()) {
                            auto& mutableW = _wm.items()[idx];
                            mutableW.fontSize = fontSize;
                            // 使用三级 OBB 更新
                            InitializeWordOBBs(mutableW, maxFontSize);
                            // 发送 OBB 更新命令
                            if (_enablePhysics && _physicsThread.IsRunning()) {
                                _physicsThread.UpdateWordOBB(idx, mutableW);
                            }
                        }
                    }
                    _recompute = true;
                }

                // 角度编辑（仅单选时）
                if (_gizmoState.selectedIndices.size() == 1) {
                    float orientation = w.orientation;
                    if (ImGui::SliderFloat("角度##edit", &orientation, -180.0f, 180.0f, "%.1f")) {
                        _wm.items()[primaryIdx].orientation = orientation;
                        if (_enablePhysics && _physicsThread.IsRunning()) {
                            _physicsThread.UpdateWordOrientation(primaryIdx, orientation);
                        }
                        _recompute = true;
                    }
                }

                // 删除按钮
                if (ImGui::Button("删除选中")) {
                    // 已经有索引了，直接用
                    std::vector<size_t> indicesToRemove = _gizmoState.selectedIndices;
                    // 从后往前删除
                    std::sort(indicesToRemove.rbegin(), indicesToRemove.rend());
                    for (size_t idx : indicesToRemove) {
                        _wm.remove(idx);
                        // 同步到物理线程
                        if (_enablePhysics && _physicsThread.IsRunning()) {
                            _physicsThread.RemoveWord(idx);
                        }
                    }
                    // 清空选中
                    _gizmoState.selectedIndices.clear();
                    _recompute = true;
                }
            }
        }

        ImGui::Spacing();
    }

    Common::CaseRenderResult WordCloud::OnRender(std::pair<std::uint32_t, std::uint32_t> const desiredSize) {

        // 获取字体路径
        const auto& fonts = GetWordCloudFonts();
        std::string fontPath = fonts[_currentFontIndex].path;

        // 延迟初始化（确保 ImGui 字体已经准备好）
        if (_wordCloudRenderer->Initialize(fontPath)) {
            // 初始化成功后，计算最大字号并初始化所有词的三级 OBB
            float maxFontSize = ComputeMaxFontSize();
            for (auto& w : _wm.items()) {
                InitializeWordOBBs(w, maxFontSize);
            }
            // 初始化物理参数的画布中心
            _physicsParams.canvasCenter = glm::vec2(c_Size.first * 0.5f, c_Size.second * 0.5f);

            // 启动物理线程
            if (_enablePhysics && !_physicsInitialized) {
                _physicsThread.Start(_wm.items(), _physicsParams);
                _physicsInitialized = true;
            }
        }

        // 从物理线程获取最新的词位置（零拷贝，无回写）
        std::vector<WordEntity> const* renderWordsPtr = nullptr;
        if (_enablePhysics && _physicsThread.IsRunning()) {
            renderWordsPtr = &_physicsThread.GetReadBuffer();
        } else {
            renderWordsPtr = &_wm.items();
        }

        // 使用 GPU 渲染器渲染文字到 FBO（支持旋转），传入背景颜色
        glm::vec4 bgColorVec(_bgColor.x, _bgColor.y, _bgColor.z, _bgAlpha);
        auto& textTexture = _wordCloudRenderer->RenderAllWords(*renderWordsPtr, c_Size, bgColorVec);

        // Gizmo 绘制已移动到 OnProcessInput，因为需要在 Image 绘制之后才能获得正确的坐标

        return Common::CaseRenderResult {
            .Fixed     = true,
            .Flipped   = true,  // FBO 渲染需要翻转 Y
            .Image     = textTexture,
            .ImageSize = c_Size,
        };
    }

    void WordCloud::OnProcessInput(ImVec2 const & pos) {
        auto         window  = ImGui::GetCurrentWindow();
        bool         hovered = false;
        bool         anyHeld = false;
        ImVec2 const delta   = ImGui::GetIO().MouseDelta;
        ImGui::ButtonBehavior(window->Rect(), window->GetID("##io"), &hovered, &anyHeld);
        if (! hovered) return;

        // 滚动处理（仅在未使用左键拖动时）
        if (!_enableLeftDrag && ImGui::IsMouseDown(ImGuiMouseButton_Left) && delta.x != 0.f)
            ImGui::SetScrollX(window, window->Scroll.x - delta.x);
        if (!_enableLeftDrag && ImGui::IsMouseDown(ImGuiMouseButton_Left) && delta.y != 0.f)
            ImGui::SetScrollY(window, window->Scroll.y - delta.y);

        if (_enableZoom && ! anyHeld && ImGui::IsItemHovered())
            Common::ImGuiHelper::ZoomTooltip(_texture, c_Size, pos);

        // 计算正确的画布屏幕坐标原点
        // pos 是相对于画布的鼠标位置，所以 canvasOrigin = mouseScreenPos - pos
        ImVec2 mouseScreenPos = ImGui::GetIO().MousePos;
        ImVec2 canvasOrigin = ImVec2(mouseScreenPos.x - pos.x, mouseScreenPos.y - pos.y);

        // 从物理线程获取最新状态用于 Gizmo 绘制（零拷贝）
        std::vector<WordEntity> const* currentWordsPtr = nullptr;
        if (_enablePhysics && _physicsThread.IsRunning()) {
            currentWordsPtr = &_physicsThread.GetReadBuffer();
        } else {
            currentWordsPtr = &_wm.items();
        }

        // 绘制选中词的 Gizmo（使用前景绘制列表，确保在图像上方）
        {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            for (size_t idx : _gizmoState.selectedIndices) {
                if (idx < currentWordsPtr->size()) {
                    labf::DrawGizmo(dl, canvasOrigin, float(c_Size.second), (*currentWordsPtr)[idx]);
                }
            }

            // 绘制碰撞框（调试用）
            if (_showCollisionBox) {
                for (size_t i = 0; i < currentWordsPtr->size(); ++i) {
                    labf::DrawCollisionBox(dl, canvasOrigin, float(c_Size.second), (*currentWordsPtr)[i]);
                }
            }
        }

        // 使用 Gizmo 交互系统处理词云交互（只读，返回增量变化）
        auto result = labf::HandleWordGizmo(
            *currentWordsPtr,
            _gizmoState,
            canvasOrigin,
            ImVec2(float(c_Size.first), float(c_Size.second)),
            _enableLeftDrag,
            [this](const std::string& text, float fontSize) {
                return _wordCloudRenderer->MeasureText(text, fontSize);
            }
        );

        // 根据交互结果更新词云状态
        if (result.handled) {
            if (_enablePhysics && _physicsThread.IsRunning()) {
                // 物理模式：发送增量命令到物理线程
                // 处理高亮变化
                for (size_t idx : result.highlightOn) {
                    _physicsThread.UpdateWordHighlight(idx, true);
                }
                for (size_t idx : result.highlightOff) {
                    _physicsThread.UpdateWordHighlight(idx, false);
                }

                // 处理移动（使用增量命令，避免竞态条件）
                if (result.changeType == labf::InteractionResult::ChangeType::Move) {
                    for (size_t idx : result.changedIndices) {
                        _physicsThread.UpdateWordPositionDelta(idx, result.positionDelta);
                    }
                    _physicsThread.ResetSimulator();  // 交互时重置 t
                }

                // 处理旋转（使用增量命令，避免竞态条件）
                if (result.changeType == labf::InteractionResult::ChangeType::Rotate) {
                    for (size_t idx : result.changedIndices) {
                        _physicsThread.UpdateWordOrientationDelta(idx, result.orientationDelta);
                    }
                    _physicsThread.ResetSimulator();  // 交互时重置 t
                }

                // 处理缩放
                if (result.changeType == labf::InteractionResult::ChangeType::Scale) {
                    float maxFontSize = ComputeMaxFontSize();
                    for (size_t i = 0; i < result.changedIndices.size() && i < result.newFontSizes.size(); ++i) {
                        size_t idx = result.changedIndices[i];
                        if (idx < _wm.items().size()) {
                            auto& mutableW = _wm.items()[idx];
                            auto& [fontSize, boxHalfSize] = result.newFontSizes[i];
                            mutableW.fontSize = fontSize;
                            // 使用三级 OBB 更新
                            InitializeWordOBBs(mutableW, std::max(maxFontSize, fontSize));
                            // 发送 OBB 更新命令
                            _physicsThread.UpdateWordOBB(idx, mutableW);
                        }
                    }
                }
            } else {
                // 非物理模式：直接更新 _wm 中的词
                // 处理移动
                if (result.changeType == labf::InteractionResult::ChangeType::Move) {
                    for (size_t idx : result.changedIndices) {
                        if (idx < _wm.items().size()) {
                            auto& w = _wm.items()[idx];
                            w.position += result.positionDelta;
                        }
                    }
                }

                // 处理旋转
                if (result.changeType == labf::InteractionResult::ChangeType::Rotate) {
                    for (size_t idx : result.changedIndices) {
                        if (idx < _wm.items().size()) {
                            auto& w = _wm.items()[idx];
                            w.orientation += result.orientationDelta;
                            while (w.orientation > 180.0f) w.orientation -= 360.0f;
                            while (w.orientation < -180.0f) w.orientation += 360.0f;
                        }
                    }
                }

                // 处理缩放（使用三级 OBB 更新）
                if (result.changeType == labf::InteractionResult::ChangeType::Scale) {
                    float maxFontSize = ComputeMaxFontSize();
                    for (size_t i = 0; i < result.changedIndices.size() && i < result.newFontSizes.size(); ++i) {
                        size_t idx = result.changedIndices[i];
                        if (idx < _wm.items().size()) {
                            auto& w = _wm.items()[idx];
                            auto& [fontSize, boxHalfSize] = result.newFontSizes[i];
                            w.fontSize = fontSize;
                            // 使用三级 OBB 更新
                            InitializeWordOBBs(w, std::max(maxFontSize, fontSize));
                        }
                    }
                }
            }

            _recompute = true;
        }
    }

    // 计算词云中最大字号
    float WordCloud::ComputeMaxFontSize() const {
        float maxSize = 0.0f;
        for (auto const& w : _wm.items()) {
            maxSize = std::max(maxSize, w.fontSize);
        }
        return maxSize > 0.0f ? maxSize : 48.0f;  // 默认最大 48
    }

    // 初始化词的 OBB 系统
    void WordCloud::InitializeWordOBBs(WordEntity& w, float maxFontSize) {
        // 使用 MeasureTextDetailed 获取完整度量信息
        TextMetrics metrics = _wordCloudRenderer->MeasureTextDetailed(w.text, w.fontSize);

        // 保存度量信息
        w.fullHeight = metrics.fullHeight;
        w.xHeight = metrics.xHeight;
        w.baselineY = metrics.baselineY;
        w.xHeightCenterY = metrics.xHeightCenterY;  // 新增：x-height 区域中心

        // 词级包围盒：始终使用 fullHeight，用于交互和碰撞快速剔除
        w.boxHalfSize = glm::vec2(metrics.width * 0.5f, metrics.fullHeight * 0.5f);

        // 判断词类型
        w.isChinese = ContainsChinese(w.text);

        if (w.isChinese) {
            // 中文词：不使用两级盒子
            w.useTwoLevelBox = false;
            w.letterOBBs.clear();
        } else if (w.fontSize > 0.5f * maxFontSize) {
            // 英文大词：启用两级盒子（词级 x-height + 字符级 OBB）
            w.useTwoLevelBox = true;
            w.wordLevelOBB.halfSize = glm::vec2(metrics.width * 0.5f, metrics.xHeight * 0.5f);
            // 计算字符级 OBB
            w.letterOBBs = _wordCloudRenderer->ComputeLetterOBBs(w.text, w.fontSize);
        } else {
            // 英文小词：不使用两级盒子
            w.useTwoLevelBox = false;
            w.letterOBBs.clear();
        }

        // 计算质量
        w.updateMassFromArea();
    }
} // namespace VCX::Labs::labf