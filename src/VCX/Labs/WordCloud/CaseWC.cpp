#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>

#include "Engine/Formats.hpp"
#include "Labs/WordCloud/CaseWC.h"
#include "Labs/Common/ImGuiHelper.h"
#include "Labs/WordCloud/WordInteract.h"
#include "Labs/WordCloud/WordCloudRenderer.h"

namespace VCX::Labs::labf {

    static constexpr auto c_Size = std::pair(1500U, 1000U);
    static constexpr size_t c_MaxInputTextSize = 1024 * 50; // 50KB

    // Helper to convert HSL to RGB
    // h, l, s are in [0, 1]
    static glm::vec3 HSLToRGB(float h, float l, float s) {
        auto hue2rgb = [](float p, float q, float t) {
            if (t < 0.0f) t += 1.0f;
            if (t > 1.0f) t -= 1.0f;
            if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
            if (t < 1.0f / 2.0f) return q;
            if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
            return p;
        };

        float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        float r = hue2rgb(p, q, h + 1.0f / 3.0f);
        float g = hue2rgb(p, q, h);
        float b = hue2rgb(p, q, h - 1.0f / 3.0f);
        return glm::vec3(r, g, b);
    }

    // High weight -> Warm (Red/Orange hue 0.0), Low weight -> Cool (Blue hue 0.6)
    static glm::vec3 GetColorHeatmap(float weight, float maxWeight) {
        float normalized = (maxWeight > 0.0f) ? (weight / maxWeight) : 0.0f;
        // Map normalized [0, 1] to hue [0.6, 0.0]
        float h_value = 0.6f * (1.0f - normalized);
        return HSLToRGB(h_value, 0.5f, 0.8f);
    }


    WordCloud::WordCloud():
        _texture({ .MinFilter = Engine::GL::FilterMode::Linear, .MagFilter = Engine::GL::FilterMode::Nearest }),
        _empty(Common::CreateCheckboardImageRGB(c_Size.first, c_Size.second)),
        _wordCloudRenderer(std::make_unique<WordCloudRenderer>()),
        _currentFontIndex(DefaultWordCloudFontIndex) {

        // 初始为空词云，等待用户选择文件生成

        // 尝试加载蒙版（加载但不自动启用）
        if (_mask.Load(c_MaskPath, glm::ivec2(c_Size.first, c_Size.second))) {
            _mask.GenerateSDF();  // 生成 SDF
            spdlog::info("WordCloud: mask loaded and SDF generated from {}", c_MaskPath);
        } else {
            spdlog::warn("WordCloud: failed to load mask from {}", c_MaskPath);
        }
    }

    void WordCloud::OnSetupPropsUI() {
         // === 数据源 (Group 1) ===
        if (ImGui::CollapsingHeader("数据源 (Data Source)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTabBar("DataTypeBar")) {
                if (ImGui::BeginTabItem("文件导入")) {
                    ImGui::Spacing();
                    // 显示已选择的文件（可折叠区域）
                    constexpr size_t kMaxVisibleFiles = 10;  
                    if (_mdFilePaths.empty()) {
                        ImGui::TextDisabled("未选择文件");
                    } else {
                        ImGui::Text("已选择 %zu 个文件", _mdFilePaths.size());
                        if (ImGui::TreeNode("查看文件列表")) {
                            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x - 20);
                            size_t displayCount = std::min(_mdFilePaths.size(), kMaxVisibleFiles);
                            for (size_t i = 0; i < displayCount; ++i) {
                                ImGui::BulletText("%s", _mdFilePaths[i].c_str());
                            }
                            if (_mdFilePaths.size() > kMaxVisibleFiles) {
                                ImGui::TextDisabled("... 还有 %zu 个文件", _mdFilePaths.size() - kMaxVisibleFiles);
                            }
                            ImGui::PopTextWrapPos();
                            ImGui::TreePop();
                        }
                    }

                    ImGui::Spacing();
                    
                    if (ImGui::Button("选择 Markdown 文件", ImVec2(180, 30))) {
                        auto result = Common::FileDialog::SelectFiles(
                            "Markdown Files (*.md)\0*.md\0All Files\0*.*\0\0"
                        );
                        if (result.has_value() && !result.value().empty()) {
                            _mdFilePaths = result.value();
                            _pythonStatusMessage = fmt::format("已选择 {} 个文件", _mdFilePaths.size());
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("生成词云##File", ImVec2(100, 30))) {
                        if (_mdFilePaths.empty()) {
                            _pythonStatusMessage = "请先选择文件";
                        } else {
                            // 启动异步任务
                            _pythonStatusMessage = "正在处理...";
                            _pythonTaskCompleted = false;
                            // 使用 lambda 拷贝 filePaths
                            auto paths = _mdFilePaths;
                            int k = _topK;
                            _pythonTask.Emplace([paths, k]() {
                                return PythonProcessor::ProcessMarkdownBatch(paths, k);
                            });
                        }
                    }

                    ImGui::SameLine();
                    ImGui::Separator();
                    ImGui::SameLine();
                    if (ImGui::Button("清除所有词##File", ImVec2(120, 30))) {
                        _wm.clear();
                        _physicsThread.Stop();
                        _physicsInitialized = false;
                        _recompute = true;
                        _pythonStatusMessage = "已清除所有词";
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("文本输入")) {
                    ImGui::Spacing();
                    ImGui::Text("请输入用于生成词云的文本：");
                    
                    // 使用 InputTextMultiline
                    // 注意：需要确保 _inputText 容量足够，或者通过 callback 调整
                    // 为了简化，这里预留较大容量并在必要时使用
                    if (_inputText.empty()) _inputText.resize(c_MaxInputTextSize, '\0');
                    else if (_inputText.size() < c_MaxInputTextSize) _inputText.resize(c_MaxInputTextSize, '\0');
                    
                    ImGui::InputTextMultiline("##InputText", 
                        _inputText.data(), 
                        _inputText.size() - 1,  
                        ImVec2(-FLT_MIN, 200), 
                        ImGuiInputTextFlags_AllowTabInput
                    );

                    ImGui::Spacing();
                    if (ImGui::Button("生成词云##Text", ImVec2(100, 40))) {
                        std::string rawText = _inputText.c_str(); 
                        if (rawText.empty()) {
                            _pythonStatusMessage = "文本内容为空";
                        } else {
                            _pythonStatusMessage = "正在处理文本...";
                            _pythonTaskCompleted = false;

                            try {
                                std::string tempPath = "assets/misc/temp_input.md";
                                std::filesystem::create_directories("assets/misc");
                                std::ofstream out(tempPath);
                                out << rawText;
                                out.close();

                                std::vector<std::string> paths = { tempPath };
                                int k = _topK;
                                _pythonTask.Emplace([paths, k]() {
                                    return PythonProcessor::ProcessMarkdownBatch(paths, k);
                                });
                            } catch (const std::exception& e) {
                                _pythonStatusMessage = fmt::format("文本保存失败: {}", e.what());
                            }
                        }
                    }

                    ImGui::SameLine();
                    ImGui::Separator();
                    ImGui::SameLine();
                    if (ImGui::Button("清除所有词##Text", ImVec2(120, 40))) {
                        _wm.clear();
                        _physicsThread.Stop();
                        _physicsInitialized = false;
                        _recompute = true;
                        _pythonStatusMessage = "已清除所有词";
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::Spacing();
            ImGui::SliderInt("返回词数", &_topK, 10, 200);
            
            if (!_pythonStatusMessage.empty()) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s", _pythonStatusMessage.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // === 布局调试与模拟 (Group 2) ===
        if (ImGui::CollapsingHeader("布局与模拟 (Simulation)", ImGuiTreeNodeFlags_DefaultOpen)) {
            // 物理控制
            if (ImGui::Checkbox("启用物理模拟", &_enablePhysics)) {
                if (_enablePhysics && _physicsInitialized) {
                     _physicsParams.canvasCenter = glm::vec2(c_Size.first * 0.5f, c_Size.second * 0.5f);
                    _physicsThread.Start(_wm.items(), _physicsParams);
                } else {
                    if (_physicsThread.IsRunning()) {
                        auto const& currentState = _physicsThread.GetReadBuffer();
                        auto& items = _wm.items();
                        for (size_t i = 0; i < std::min(currentState.size(), items.size()); ++i) {
                            items[i].position = currentState[i].position;
                            items[i].orientation = currentState[i].orientation;
                            // ... other properties usually don't change by physics
                        }
                    }
                    _physicsThread.Stop();
                }
            }

            if (_enablePhysics) {
                ImGui::Indent();
                if (ImGui::Button("打开物理参数面板")) {
                    _showPhysicsSettingsWindow = !_showPhysicsSettingsWindow;
                }
                ImGui::Unindent();

                // 物理参数设置窗口 (Modal or separate window logic remains similar)
                if (_showPhysicsSettingsWindow) {
                     ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                    ImGui::SetNextWindowSize(ImVec2(350, 280), ImGuiCond_Always);
                    ImGui::Begin("物理参数设置", &_showPhysicsSettingsWindow);
                    bool paramsChanged = false;
                    paramsChanged |= ImGui::SliderFloat("中心力权重", &_physicsParams.kCenter, 0.0f, 2.0f);
                    paramsChanged |= ImGui::SliderFloat("邻域力权重", &_physicsParams.kNeighbor, 0.0f, 30.0f);
                    paramsChanged |= ImGui::SliderFloat("速度阻尼", &_physicsParams.lambda, 0.5f, 0.99f);
                    paramsChanged |= ImGui::SliderFloat("弹性系数", &_physicsParams.restitution, 0.0f, 1.0f);
                    float physicsHz = 1.0f / _physicsParams.fixedDt;
                    if (ImGui::SliderFloat("物理频率", &physicsHz, 30.0f, 240.0f, "%.0f Hz")) {
                        _physicsParams.fixedDt = 1.0f / physicsHz;
                        paramsChanged = true;
                    }
                    if (paramsChanged) {
                        _physicsThread.SetParams(_physicsParams);
                        _physicsThread.ResetSimulator();
                    }
                    ImGui::End();
                }
            }
            
            // 角度控制
            if (ImGui::SliderFloat("全局角度锁定", &_lockAngle, 0.0f, 360.0f, "%.1f°")) {
                for (auto& w : _wm.items()) w.orientation = _lockAngle;
                if (_enablePhysics) {
                    _physicsThread.Stop();
                    _physicsParams.canvasCenter = glm::vec2(c_Size.first * 0.5f, c_Size.second * 0.5f);
                    _physicsThread.Start(_wm.items(), _physicsParams);
                    _physicsInitialized = true;
                    if (_enableMask) {
                        _physicsThread.SetMask(&_mask);
                    }
                }
                _recompute = true;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // === 外观与蒙版 (Group 3) ===
        if (ImGui::CollapsingHeader("外观与蒙版 (Appearance)", ImGuiTreeNodeFlags_DefaultOpen)) {
            // 背景设置
            ImGui::Text("背景设置:");
            ImGui::SameLine();
            if (ImGui::ColorEdit3("##BGColor", (float*)&_bgColor, ImGuiColorEditFlags_NoInputs)) _recompute = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (ImGui::SliderFloat("透明度", &_bgAlpha, 0.0f, 1.0f, "%.2f")) _recompute = true;

            Common::ImGuiHelper::SaveImage(_texture, c_Size, true);
            
            ImGui::Spacing();

            // 字体选择
            const auto& fonts = GetWordCloudFonts();
            if (ImGui::BeginCombo("字体选择", (_currentFontIndex < fonts.size() ? fonts[_currentFontIndex].name.c_str() : "None"))) {
                 for (std::size_t i = 0; i < fonts.size(); ++i) {
                    if (ImGui::Selectable(fonts[i].name.c_str(), i == _currentFontIndex)) {
                        if (i != _currentFontIndex) {
                            _currentFontIndex = i;
                            _wordCloudRenderer->SetFont(fonts[i].path);
                            // 换字体时刷新所有词的三级 OBB
                            float maxFontSize = ComputeMaxFontSize();
                            for (auto& w : _wm.items()) {
                                InitializeWordOBBs(w, maxFontSize, _physicsParams.pixelsPerUnit);
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

            ImGui::Spacing();

            // 蒙版设置
            if (ImGui::Checkbox("启用蒙版约束", &_enableMask)) {
                if (_enableMask && !_mask.IsValid()) {
                    if (!_mask.Load(c_MaskPath, glm::ivec2(c_Size.first, c_Size.second))) {
                        _enableMask = false;
                        _pythonStatusMessage = "蒙版加载失败";
                    }
                }
                _physicsThread.SetMask(_enableMask ? &_mask : nullptr);
               _physicsThread.ResetSimulator();
                _recompute = true;
            }
            if (_enableMask) {
                ImGui::SameLine();
                if (ImGui::Checkbox("显示边界", &_showMaskBoundary)) _recompute = true;
                ImGui::SameLine();
                ImGui::Checkbox("显示碰撞力", &_showMaskCollisionInfo);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // === 调试选项 (Group 4) ===
        if (ImGui::CollapsingHeader("调试 (Debug)")) {
             ImGui::Checkbox("显示碰撞包围盒 (OBB)", &_showCollisionBox);
        }

        // 处理完成结果
        if (!_pythonTaskCompleted && _pythonTask.HasValue()) {
            auto results = _pythonTask.Value();
            _pythonTaskCompleted = true;

            if (!results.empty()) {
                _pythonStatusMessage = fmt::format("成功解析 {} 个词", results.size());

                // 计算权重的最小值和最大值（用于对数映射）
                float minWeight = results[0].weight;
                float maxWeight = results[0].weight;
                for (auto const& r : results) {
                    minWeight = std::min(minWeight, r.weight);
                    maxWeight = std::max(maxWeight, r.weight);
                }

                // 对数平滑映射参数
                constexpr float SizeMin = 25.0f;  // 最小字号 (Reduced from 30.0f)
                constexpr float SizeMax = 80.0f;  // 最大字号
                constexpr float LogOffset = 1.0f; // log(v + 1) 中的 +1 偏移

                // 预计算分母，避免重复计算
                float logMaxPlus1 = std::log(maxWeight + LogOffset);
                float logMinPlus1 = std::log(minWeight + LogOffset);
                float logDenom = logMaxPlus1 - logMinPlus1;

                for (size_t i = 0; i < results.size(); ++i) {
                    auto const& r = results[i];
                    // 对数平滑映射：Size = SizeMin + (SizeMax - SizeMin) * (log(v+1) - log(v_min+1)) / (log(v_max+1) - log(v_min+1))
                    float normalized = (logDenom > 0.0f)
                        ? (std::log(r.weight + LogOffset) - logMinPlus1) / logDenom
                        : 0.5f;  // 避免除零
                    float fontSize = SizeMin + (SizeMax - SizeMin) * normalized;

                    auto& w = _wm.add(r.text, fontSize);

                    // Apply Heatmap Color Strategy B
                    glm::vec3 colorRGB = GetColorHeatmap(r.weight, maxWeight);
                    w.color = glm::vec4(colorRGB, 1.0f);

                    // 先初始化 OBB（使用像素单位，scale=1.0，确保渲染和碰撞检测尺度一致）
                    float ppp = _physicsParams.pixelsPerUnit > 0 ? _physicsParams.pixelsPerUnit : 30.0f;
                    InitializeWordOBBs(w, ComputeMaxFontSize(), 1.0f);
                    
                    // 单独更新质量为公制单位 (Mass = AreaMeters = AreaPixels / ppp^2)
                    w.updateMassFromArea(ppp * ppp);

                    // 使用静态螺旋线布局计算初始位置
                    glm::vec2 spiralPos;
                    glm::vec2 canvasCenter(c_Size.first * 0.5f, c_Size.second * 0.5f);

                    // 螺旋线参数
                    constexpr float spiralA = 0.0f;       // 起始半径
                    constexpr float spiralB = 5.0f;       // 增长速率
                    constexpr float angularOffset = 0.1f; // 角度偏移（弧度），控制螺旋线密度

                    // 排除当前词（索引为 _wm.items().size() - 1）
                    // 根据是否启用蒙版选择布局函数
                    int iterations = 0;
                    bool foundPosition = false;
                    std::size_t currentIdx = _wm.items().size() - 1;
                    if (_enableMask) {
                        foundPosition = SpiralLayout::FindNonCollidingSpiralPositionWithMask(
                            w, _wm.items(),
                            currentIdx,  // 排除新添加的词
                            canvasCenter,
                            spiralA, spiralB, angularOffset,
                            _mask,  // 传入蒙版
                            spiralPos,
                            iterations
                        );
                    } else {
                        foundPosition = SpiralLayout::FindNonCollidingSpiralPosition(
                            w, _wm.items(),
                            currentIdx,  // 排除新添加的词
                            canvasCenter,
                            spiralA, spiralB, angularOffset,
                            spiralPos,
                            iterations
                        );
                    }

                    // 如果迭代次数超过阈值，跳过该词不渲染
                    if (!foundPosition) {
                        _wm.remove(currentIdx);
                        continue;
                    }

                    w.position = spiralPos;
                    w.orientation = 0.0f;  // 水平方向

                    if (_enablePhysics && _physicsThread.IsRunning()) {
                        _physicsThread.AddWord(w);
                    }
                }
                _pythonResult = results;
            } else {
                _pythonStatusMessage = "未解析到词语";
            }
            _pythonTask.Reset();
            _recompute = true;
        }

        ImGui::Spacing();
    }

    Common::CaseRenderResult WordCloud::OnRender(std::pair<std::uint32_t, std::uint32_t> const desiredSize) {

        // 获取字体路径
        const auto& fonts = GetWordCloudFonts();
        std::string fontPath = fonts[_currentFontIndex].path;

        // 延迟初始化（确保 ImGui 字体已经准备好）
        if (_wordCloudRenderer->Initialize(fontPath)) {
            // 初始化物理参数的画布中心
            _physicsParams.canvasCenter = glm::vec2(c_Size.first * 0.5f, c_Size.second * 0.5f);
            
            // EdWordle 使用 30
            _physicsParams.pixelsPerUnit = 30.0f; 

            // 初始化成功后，计算最大字号并初始化所有词的三级 OBB
            float maxFontSize = ComputeMaxFontSize();
            for (auto& w : _wm.items()) {
                InitializeWordOBBs(w, maxFontSize, _physicsParams.pixelsPerUnit);
            }

            // 启动物理线程
            if (_enablePhysics && !_physicsInitialized) {
                _physicsThread.Start(_wm.items(), _physicsParams);
                _physicsInitialized = true;
                // 设置蒙版
                if (_enableMask) {
                    _physicsThread.SetMask(&_mask);
                }
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

        // 保存渲染结果到 _texture（供保存图片使用）
        auto textureData = textTexture.Download<Engine::Formats::RGBA8>();
        _texture.Update(textureData);

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

            // 绘制蒙版边界（使用 SDF）
            if (_showMaskBoundary && _mask.IsValid() && _mask.HasSDF()) {
                float scaledWidth = _mask.GetImageSize().x * _mask.GetScale();
                float scaledHeight = _mask.GetImageSize().y * _mask.GetScale();
                float offsetX = (c_Size.first - scaledWidth) * 0.5f;
                float offsetY = (c_Size.second - scaledHeight) * 0.5f;

                ImU32 boundaryColor = IM_COL32(255, 0, 0, 255);  // 红色边界

                // 使用 SDF 采样边界点
                int step = 1;  // 每个像素都检查，更精确

                for (int imgY = 0; imgY < _mask.GetImageSize().y; imgY += step) {
                    for (int imgX = 0; imgX < _mask.GetImageSize().x; imgX += step) {
                        // 转换为画布坐标
                        float canvasX = offsetX + imgX * _mask.GetScale();
                        float canvasY = offsetY + imgY * _mask.GetScale();

                        // 使用 SDF 检查是否是边界点
                        // SDF 也是基于像素距离的，现在的 SDF 在边界处（整数坐标上）绝对值最小为 1.0
                        // 所以需要增大阈值才能显示出边界
                        if (_mask.IsOnBoundary(glm::vec2(canvasX, canvasY), 0.6f)) {
                            dl->AddRect(
                                ImVec2(canvasOrigin.x + canvasX, canvasOrigin.y + canvasY),
                                ImVec2(canvasOrigin.x + canvasX + _mask.GetScale(), canvasOrigin.y + canvasY + _mask.GetScale()),
                                boundaryColor
                            );
                        }
                    }
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
                            InitializeWordOBBs(mutableW, std::max(maxFontSize, fontSize), _physicsParams.pixelsPerUnit);
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
                            float scale = _physicsParams.pixelsPerUnit > 0 ? _physicsParams.pixelsPerUnit : 30.0f;
                            InitializeWordOBBs(w, std::max(maxFontSize, fontSize), scale);
                        }
                    }
                }
            }

            _recompute = true;
        }

        // ============================================================
        // 渲染调试碰撞点
        // ============================================================
        if (_showMaskCollisionInfo) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            float dt = ImGui::GetIO().DeltaTime;
            
            // 收集新的碰撞点
            for (const auto& w : *currentWordsPtr) {
                if (w.maskCollision) {
                    glm::vec2 screenPos;
                    screenPos.x = canvasOrigin.x + w.maskCollisionPoint.x;
                    screenPos.y = canvasOrigin.y + (c_Size.second - w.maskCollisionPoint.y);

                    // 转换法线到屏幕空间（Y轴翻转：向上 -> 向下）
                    glm::vec2 screenNormal = w.maskCollisionNormal;
                    screenNormal.y = -screenNormal.y;

                    _debugPoints.push_back({screenPos, 1.0f, screenNormal}); // 持续 1.0 秒，包含法线
                }
            }

            // 绘制并更新调试点
            for (auto it = _debugPoints.begin(); it != _debugPoints.end();) {
                // 绘制点
                dl->AddCircleFilled(ImVec2(it->pos.x, it->pos.y), 3.0f, IM_COL32(255, 0, 0, 200));
                
                // 绘制法线
                ImVec2 p1(it->pos.x, it->pos.y);
                ImVec2 p2(it->pos.x + it->normal.x * 20.0f, it->pos.y + it->normal.y * 20.0f);
                dl->AddLine(p1, p2, IM_COL32(0, 255, 0, 255), 2.0f); // 绿色法线

                it->life -= dt;
                if (it->life <= 0) {
                    it = _debugPoints.erase(it);
                } else {
                    ++it;
                }
            }
        } else {
             _debugPoints.clear();
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
    void WordCloud::InitializeWordOBBs(WordEntity& w, float maxFontSize, float canvasScale) {
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

        // 计算质量 (归一化质量)
        float scaleSquared = (canvasScale > 0.0f) ? canvasScale * canvasScale : 1.0f;
        w.updateMassFromArea(scaleSquared);
    }
} // namespace VCX::Labs::labf
