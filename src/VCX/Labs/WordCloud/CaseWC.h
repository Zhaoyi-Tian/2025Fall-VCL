#pragma once

#include "Engine/Async.hpp"
#include "Labs/Common/ICase.h"
#include "Labs/Common/ImageRGB.h"
#include "Labs/WordCloud/WordEntity.h"
#include "Labs/WordCloud/WordManager.h"
#include "Labs/WordCloud/WordCloudRenderer.h"
#include "Labs/WordCloud/WordInteract.h"
#include "Labs/WordCloud/PhysicsSimulator.h"
#include "Labs/WordCloud/PhysicsThread.h"

namespace VCX::Labs::labf {

    class WordCloud : public Common::ICase {
    public:
        WordCloud();

        virtual std::string_view const GetName() override { return "wordcloud"; }

        virtual void                     OnSetupPropsUI() override;
        virtual Common::CaseRenderResult OnRender(std::pair<std::uint32_t, std::uint32_t> const desiredSize) override;
        virtual void                     OnProcessInput(ImVec2 const & pos) override;

    private:
        Engine::GL::UniqueTexture2D _texture;

        Common::ImageRGB _empty;

        Engine::Async<Common::ImageRGB> _task;

        bool _enableZoom     = false;  // 默认禁用 zoom tooltip
        bool _enableLeftDrag = true;   // 默认启用左键拖动
        bool _showCollisionBox = false; // 显示碰撞框（调试用）
        bool _recompute      = true;
        ImVec4 _bgColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        float _bgAlpha = 1.0f;

        int        _selectIdx = -1;
        glm::ivec2 _lineP0 { 10, 20 };
        glm::ivec2 _lineP1 { 300, 290 };

        // 词云管理器：管理画布上的所有 WordEntity
        labf::WordManager _wm;

        // 词云渲染器：支持文字旋转的GPU渲染器
        std::unique_ptr<WordCloudRenderer> _wordCloudRenderer;

        // Gizmo 交互状态
        labf::GizmoState _gizmoState;

        // 添加词的 UI 状态
        char _newWordText[256] = "";
        ImVec4 _newWordColor = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
        float _newWordFontSize = 28.0f;

        // 全局角度锁定
        float _lockAngle = 0.0f;

        // 物理模拟
        PhysicsThread _physicsThread;
        PhysicsParams _physicsParams;
        bool _enablePhysics = true;
        bool _physicsInitialized = false;

        // 辅助函数：初始化词的三级 OBB
        void InitializeWordOBBs(WordEntity& w, float maxFontSize);

        // 计算词云中最大字号
        float ComputeMaxFontSize() const;
    };
}
