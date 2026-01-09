#include "WordCloudRenderer.h"
#include "UTF8Utils.h"
#include "glm/gtc/matrix_transform.hpp"
#include <glad/glad.h>
#include <cmath>

WordCloudRenderer::WordCloudRenderer() :
    _textRenderer(nullptr),
    _initialized(false) {
}

WordCloudRenderer::~WordCloudRenderer() {
}

bool WordCloudRenderer::Initialize(const std::string& fontPath) {
    if (_initialized) return true;

    _textRenderer = std::make_unique<MSDFTextRenderer>();
    if (!_textRenderer->Initialize(fontPath)) {
        return false;
    }

    _initialized = true;
    return true;
}

VCX::Engine::GL::UniqueTexture2D& WordCloudRenderer::RenderAllWords(
    const std::vector<WordEntity>& words,
    std::pair<std::uint32_t, std::uint32_t> size,
    glm::vec4 bgColor) {

    if (!_initialized || !_textRenderer) {
        return _frame.GetColorAttachment();
    }

    // 调整 Frame 大小
    _frame.Resize(size);

    // 清空之前的渲染数据
    _textRenderer->Clear();

    // 添加所有词到渲染队列
    for (const auto& word : words) {
        // 将角度从度转换为弧度
        // 注意：渲染坐标系 Y 轴向上，但 orientation 按屏幕坐标系定义，需要取反
        float rotation = glm::radians(-word.orientation);

        // 添加文字到渲染器（支持旋转）
        _textRenderer->AddText(
            word.text,
            word.position,
            word.fontSize,
            word.color,
            rotation
        );
    }

    // 创建正交投影矩阵（Y轴向上，适配字体坐标系）
    glm::mat4 projection = glm::ortho(0.0f, float(size.first),
                                       0.0f, float(size.second), -1.0f, 1.0f);

    // 使用 Frame 渲染
    {
        gl_using(_frame);

        // 用指定的背景色清除
        glClearColor(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 渲染文字
        _textRenderer->Render(projection);
    }

    return _frame.GetColorAttachment();
}

glm::vec2 WordCloudRenderer::MeasureText(const std::string& text, float fontSize) {
    if (!_initialized || !_textRenderer || !_textRenderer->GetAtlas()) {
        // 返回估计值
        return glm::vec2(fontSize * 0.6f * text.size(), fontSize * 1.2f);
    }

    MSDFAtlas* atlas = _textRenderer->GetAtlas();
    atlas->PreloadGlyphs(text);

    float scale = fontSize;
    float totalWidth = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;

    const char* ptr = text.c_str();
    const char* end = ptr + text.size();

    while (ptr < end) {
        uint32_t codepoint = VCX::Labs::labf::DecodeUTF8(ptr, end);
        if (codepoint == 0) break;

        const auto& glyph = atlas->GetGlyph(codepoint);
        if (glyph.valid) {
            totalWidth += glyph.advance * scale;
            // 跟踪垂直边界
            float y0 = glyph.planeBoundsMin.y * scale;
            float y1 = glyph.planeBoundsMax.y * scale;
            if (y0 < minY) minY = y0;
            if (y1 > maxY) maxY = y1;
        }
    }

    float height = maxY - minY;
    if (height < fontSize * 0.5f) {
        // 如果计算的高度太小，使用字体度量
        height = static_cast<float>(atlas->GetAscender() - atlas->GetDescender()) * fontSize;
    }

    return glm::vec2(totalWidth, height);
}

TextMetrics WordCloudRenderer::MeasureTextDetailed(const std::string& text, float fontSize) {
    TextMetrics metrics;

    if (!_initialized || !_textRenderer || !_textRenderer->GetAtlas()) {
        // 返回估计值
        metrics.width = fontSize * 0.6f * text.size();
        metrics.xHeight = fontSize * 0.5f;
        metrics.fullHeight = fontSize * 1.2f;
        metrics.baselineY = 0.0f;
        metrics.xHeightCenterY = 0.0f;
        return metrics;
    }

    MSDFAtlas* atlas = _textRenderer->GetAtlas();
    atlas->PreloadGlyphs(text);

    float scale = fontSize;
    float totalWidth = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;

    const char* ptr = text.c_str();
    const char* end = ptr + text.size();

    while (ptr < end) {
        uint32_t codepoint = VCX::Labs::labf::DecodeUTF8(ptr, end);
        if (codepoint == 0) break;

        const auto& glyph = atlas->GetGlyph(codepoint);
        if (glyph.valid) {
            totalWidth += glyph.advance * scale;
            float y0 = glyph.planeBoundsMin.y * scale;
            float y1 = glyph.planeBoundsMax.y * scale;
            if (y0 < minY) minY = y0;
            if (y1 > maxY) maxY = y1;
        }
    }

    metrics.width = totalWidth;
    metrics.fullHeight = maxY - minY;

    // x-height：使用字体的 x-height（字母 'x' 的高度）
    metrics.xHeight = static_cast<float>(atlas->GetXHeight()) * fontSize;

    // 基线位置：基线在 y=0，中心在 (minY + maxY) / 2
    // baselineY = 0 - center = -(minY + maxY) / 2
    float centerY = (minY + maxY) * 0.5f;
    metrics.baselineY = -centerY;

    // 计算 x-height 区域中心（与 ComputeLetterOBBs 完全一致的方式）
    // 使用 'x' 字符的 planeBounds
    const auto& xGlyph = atlas->GetGlyph('x');
    if (xGlyph.valid && xGlyph.uvMin != xGlyph.uvMax) {
        // 与 ComputeLetterOBBs 完全一致的计算方式：
        // localCenter.y = penY + (planeBoundsMin.y + planeBoundsMax.y) / 2 * scale
        // penY = -centerY = baselineY
        float xCenterInGlyph = (xGlyph.planeBoundsMin.y + xGlyph.planeBoundsMax.y) * 0.5f * scale;
        metrics.xHeightCenterY = metrics.baselineY + xCenterInGlyph;
    } else {
        // 降级：使用 xHeight 的中点
        metrics.xHeightCenterY = metrics.baselineY + metrics.xHeight * 0.5f;
    }

    // 如果计算的高度太小，使用降级值
    if (metrics.fullHeight < fontSize * 0.5f) {
        metrics.fullHeight = static_cast<float>(atlas->GetAscender() - atlas->GetDescender()) * fontSize;
    }
    if (metrics.xHeight < fontSize * 0.3f) {
        metrics.xHeight = fontSize * 0.5f;
    }

    return metrics;
}

std::vector<LetterOBB> WordCloudRenderer::ComputeLetterOBBs(const std::string& text, float fontSize) {
    std::vector<LetterOBB> result;

    if (!_initialized || !_textRenderer || !_textRenderer->GetAtlas()) {
        return result;  // 返回空列表
    }

    MSDFAtlas* atlas = _textRenderer->GetAtlas();
    atlas->PreloadGlyphs(text);

    float scale = fontSize;

    // 第一遍：计算总宽度和垂直范围（用于居中）
    float totalWidth = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;

    const char* ptr = text.c_str();
    const char* end = ptr + text.size();

    while (ptr < end) {
        uint32_t codepoint = VCX::Labs::labf::DecodeUTF8(ptr, end);
        if (codepoint == 0) break;

        const auto& glyph = atlas->GetGlyph(codepoint);
        if (glyph.valid) {
            totalWidth += glyph.advance * scale;
            float y0 = glyph.planeBoundsMin.y * scale;
            float y1 = glyph.planeBoundsMax.y * scale;
            if (y0 < minY) minY = y0;
            if (y1 > maxY) maxY = y1;
        }
    }

    float verticalCenter = (minY + maxY) * 0.5f;
    float halfWordWidth = totalWidth * 0.5f;  // 词级 OBB 的半宽

    // 第二遍：计算每个字符的 OBB（相对于词中心）
    float penX = -totalWidth * 0.5f;  // 从词中心左边开始
    float penY = -verticalCenter;      // 垂直居中偏移

    ptr = text.c_str();
    while (ptr < end) {
        uint32_t codepoint = VCX::Labs::labf::DecodeUTF8(ptr, end);
        if (codepoint == 0) break;

        const auto& glyph = atlas->GetGlyph(codepoint);
        if (glyph.valid) {
            // 跳过空格（没有实际渲染内容）
            if (glyph.uvMin != glyph.uvMax) {
                float x0 = penX + glyph.planeBoundsMin.x * scale;
                float y0 = penY + glyph.planeBoundsMin.y * scale;
                float x1 = penX + glyph.planeBoundsMax.x * scale;
                float y1 = penY + glyph.planeBoundsMax.y * scale;

                // 限制字符 OBB 不超出词级 OBB 的宽度范围
                x0 = std::max(x0, -halfWordWidth);
                x1 = std::min(x1, halfWordWidth);

                // 确保截断后仍有有效宽度
                if (x1 > x0) {
                    LetterOBB letter;
                    letter.localCenter = glm::vec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
                    letter.halfSize = glm::vec2((x1 - x0) * 0.5f, (y1 - y0) * 0.5f);
                    result.push_back(letter);
                }
            }

            penX += glyph.advance * scale;
        }
    }

    return result;
}