#pragma once
#include "MSDFTextRenderer.h"
#include "WordManager.h"
#include "WordEntity.h"
#include "Engine/GL/Frame.hpp"
#include "glm/glm.hpp"
#include <memory>
#include <string>

using namespace VCX::Labs::labf;

/// EdWordle 论文所需的文字度量信息
struct TextMetrics {
    float width;         // 词的总宽度（像素）
    float xHeight;       // x-height（像素）- 用于 OBB 碰撞检测
    float fullHeight;    // 完整高度（像素）- 包含 ascender/descender
    float baselineY;     // 基线位置（相对于中心，像素）
    float xHeightCenterY;// x-height 区域中心（相对于词中心，像素）
};

class WordCloudRenderer {
public:
    WordCloudRenderer();
    ~WordCloudRenderer();

    /// Initialize with font file path
    /// @param fontPath Path to TTF/TTC font file
    bool Initialize(const std::string& fontPath = "assets/fonts/NotoSansCJK-Regular.ttc");

    // 渲染所有词到内部 Frame，返回 Frame 的颜色纹理引用
    // bgColor: 背景颜色 (RGBA)
    VCX::Engine::GL::UniqueTexture2D& RenderAllWords(
        const std::vector<WordEntity>& words,
        std::pair<std::uint32_t, std::uint32_t> size,
        glm::vec4 bgColor = glm::vec4(1.0f));

    // 获取 Frame 的颜色附件
    VCX::Engine::GL::UniqueTexture2D& GetTexture() { return _frame.GetColorAttachment(); }

    /// 计算文字的真实尺寸（使用 MSDF Atlas）
    /// @param text UTF-8 编码的文字
    /// @param fontSize 字体大小（像素）
    /// @return 文字的宽度和高度（像素）
    glm::vec2 MeasureText(const std::string& text, float fontSize);

    /// 计算详细的文字度量（EdWordle 论文所需）
    /// @param text UTF-8 编码的文字
    /// @param fontSize 字体大小（像素）
    /// @return TextMetrics 包含 width, xHeight, fullHeight, baselineY
    TextMetrics MeasureTextDetailed(const std::string& text, float fontSize);

    /// 计算每个字符的 OBB（用于大词的精细碰撞检测）
    /// @param text UTF-8 编码的文字
    /// @param fontSize 字体大小（像素）
    /// @return 每个字符的 LetterOBB（相对于词中心）
    std::vector<LetterOBB> ComputeLetterOBBs(const std::string& text, float fontSize);

    /// 获取底层的 MSDFTextRenderer
    MSDFTextRenderer* GetTextRenderer() { return _textRenderer.get(); }

private:
    std::unique_ptr<MSDFTextRenderer> _textRenderer;
    VCX::Engine::GL::UniqueRenderFrame _frame;
    bool _initialized;
};