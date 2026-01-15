#pragma once

#include "Config/Fonts.h"
#include "Engine/Async.hpp"
#include "Labs/Common/ICase.h"
#include "Labs/Common/ImageRGB.h"
#include "Labs/Common/FileDialog.h"
#include "Labs/WordCloud/WordEntity.h"
#include "Labs/WordCloud/WordManager.h"
#include "Labs/WordCloud/WordCloudRenderer.h"
#include "Labs/WordCloud/WordInteract.h"
#include "Labs/WordCloud/PhysicsSimulator.h"
#include "Labs/WordCloud/PhysicsThread.h"
#include "Labs/WordCloud/PythonProcessor.h"
#include "Labs/WordCloud/SpiralLayout.h"
#include "Labs/WordCloud/Mask.h"

namespace VCX::Labs::labf {

    class WordCloud : public Common::ICase {
    public:
        WordCloud();

        virtual std::string_view const GetName() override { return "wordcloud"; }

        virtual void                     OnSetupPropsUI() override;
        virtual Common::CaseRenderResult OnRender(std::pair<std::uint32_t, std::uint32_t> const desiredSize) override;
        virtual void                     OnProcessInput(ImVec2 const & pos) override;

    private:
        struct DebugPoint {
            glm::vec2 pos;
            float life;
            glm::vec2 normal;
        };
        std::vector<DebugPoint> _debugPoints;

        Engine::GL::UniqueTexture2D _texture;

        Common::ImageRGB _empty;

        Engine::Async<Common::ImageRGB> _task;

        bool _enableZoom     = false;  // 默认禁用 zoom tooltip
        bool _enableLeftDrag = true;   // 默认启用左键拖动
        bool _showCollisionBox = false; // 显示碰撞框（调试用）
        bool _showMaskCollisionInfo = false; // 显示蒙版碰撞信息（碰撞点和力）
        bool _recompute      = true;
        ImVec4 _bgColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        float _bgAlpha = 1.0f;

        // 词云管理器：管理画布上的所有 WordEntity
        labf::WordManager _wm;

        // 词云渲染器：支持文字旋转的GPU渲染器
        std::unique_ptr<WordCloudRenderer> _wordCloudRenderer;

        // Gizmo 交互状态
        labf::GizmoState _gizmoState;

        // 全局角度锁定
        float _lockAngle = 0.0f;

        // 字体设置
        std::size_t _currentFontIndex = DefaultWordCloudFontIndex;

        // 物理模拟
        PhysicsThread _physicsThread;
        PhysicsParams _physicsParams;
        bool _enablePhysics = true;
        bool _physicsInitialized = false;
        bool _showPhysicsSettingsWindow = false; // 物理设置窗口可见性

        // 蒙版相关
        bool _enableMask = false;  // 是否启用蒙版
        bool _showMaskBoundary = false;  // 是否显示蒙版边界（调试用）
        Mask _mask;                 // 蒙版对象
        static constexpr const char* c_MaskPath = "assets/images/teapot.png";

        // Markdown 文件处理
        std::vector<std::string> _mdFilePaths;
        int _topK = 100;
        std::string _inputText = ""; // 输入文本缓冲区
        Engine::Async<std::vector<WordResult>> _pythonTask;
        std::vector<WordResult> _pythonResult;
        std::string _pythonStatusMessage = "";
        bool _pythonTaskCompleted = false;

        // 辅助函数：初始化词的三级 OBB (需要传入画布缩放比例用于质量归一化)
        void InitializeWordOBBs(WordEntity& w, float maxFontSize, float canvasScale);

        // 计算词云中最大字号
        float ComputeMaxFontSize() const;
    };
}
