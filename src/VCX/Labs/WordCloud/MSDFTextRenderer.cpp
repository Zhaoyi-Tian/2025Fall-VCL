#include "MSDFTextRenderer.h"
#include "UTF8Utils.h"
#include <cmath>
#include <glad/glad.h>

namespace VCX::Labs::labf {

MSDFTextRenderer::MSDFTextRenderer() {}

MSDFTextRenderer::~MSDFTextRenderer() {}

bool MSDFTextRenderer::Initialize(const std::string& fontPath) {
    if (_initialized) return true;

    // Initialize MSDF atlas
    // pxRange=8.0: larger pixel range for better anti-aliasing
    // glyphPadding=2: extra padding between glyphs to prevent bleeding artifacts (vertical lines)
    _atlas = std::make_unique<MSDFAtlas>();
    if (!_atlas->Initialize(fontPath, 2048, 4096, 48, 8.0, 2)) {
        return false;
    }

    // Load shaders
    try {
        Engine::GL::SharedShader vertexShader("assets/shaders/msdf_text.vert");
        Engine::GL::SharedShader fragmentShader("assets/shaders/msdf_text.frag");

        _program = std::make_unique<Engine::GL::UniqueProgram>(
            std::initializer_list<Engine::GL::SharedShader>{
                vertexShader, fragmentShader
            }
        );
    } catch (...) {
        return false;
    }

    // Create vertex layout
    Engine::GL::VertexLayout layout = Engine::GL::VertexLayout()
        .Add<TextVertex>("vertex", Engine::GL::DrawFrequency::Static)
        .At(0, &TextVertex::position)
        .At(1, &TextVertex::texCoord)
        .At(2, &TextVertex::color);

    _renderItem = std::make_unique<Engine::GL::UniqueIndexedRenderItem>(
        layout, Engine::GL::PrimitiveType::Triangles);

    _initialized = true;
    return true;
}

void MSDFTextRenderer::AddText(const std::string& text,
                                glm::vec2 position,
                                float fontSize,
                                glm::vec4 color,
                                float rotation) {
    if (!_initialized) return;

    // Preload all glyphs for this text
    _atlas->PreloadGlyphs(text);

    GenerateTextVertices(text, position, fontSize, color, rotation);
}

void MSDFTextRenderer::Clear() {
    _vertices.clear();
    _indices.clear();
}

void MSDFTextRenderer::GenerateTextVertices(const std::string& text,
                                             glm::vec2 position,
                                             float fontSize,
                                             glm::vec4 color,
                                             float rotation) {
    if (!_atlas) return;

    // Font metrics
    float scale = fontSize;

    // First pass: calculate total width and height for centering
    float totalWidth = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;

    const char* ptr = text.c_str();
    const char* end = ptr + text.size();

    while (ptr < end) {
        uint32_t codepoint = DecodeUTF8(ptr, end);
        if (codepoint == 0) break;

        const auto& glyph = _atlas->GetGlyph(codepoint);
        if (glyph.valid) {
            totalWidth += glyph.advance * scale;
            float y0 = glyph.planeBoundsMin.y * scale;
            float y1 = glyph.planeBoundsMax.y * scale;
            if (y0 < minY) minY = y0;
            if (y1 > maxY) maxY = y1;
        }
    }

    // Calculate vertical center offset
    float totalHeight = maxY - minY;
    float verticalCenter = (minY + maxY) * 0.5f;

    // Rotation matrix
    float cosR = std::cos(rotation);
    float sinR = std::sin(rotation);

    // Pen position (starts at left edge of text, vertically centered)
    float penX = position.x - totalWidth * 0.5f;
    float penY = position.y - verticalCenter;  // Offset to center vertically
    glm::vec2 center = position;

    // Second pass: generate vertices
    ptr = text.c_str();

    while (ptr < end) {
        uint32_t codepoint = DecodeUTF8(ptr, end);
        if (codepoint == 0) break;

        const auto& glyph = _atlas->GetGlyph(codepoint);
        if (!glyph.valid) continue;

        // Skip whitespace (no geometry to render)
        if (glyph.uvMin == glyph.uvMax) {
            penX += glyph.advance * scale;
            continue;
        }

        // Quad corners in local space
        float x0 = penX + glyph.planeBoundsMin.x * scale;
        float y0 = penY + glyph.planeBoundsMin.y * scale;
        float x1 = penX + glyph.planeBoundsMax.x * scale;
        float y1 = penY + glyph.planeBoundsMax.y * scale;

        // Apply rotation around text center
        glm::vec2 corners[4] = {
            {x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}
        };

        for (int i = 0; i < 4; ++i) {
            glm::vec2 v = corners[i] - center;
            corners[i].x = v.x * cosR - v.y * sinR + center.x;
            corners[i].y = v.x * sinR + v.y * cosR + center.y;
        }

        // UV coordinates
        glm::vec2 uvs[4] = {
            {glyph.uvMin.x, glyph.uvMin.y},
            {glyph.uvMax.x, glyph.uvMin.y},
            {glyph.uvMin.x, glyph.uvMax.y},
            {glyph.uvMax.x, glyph.uvMax.y}
        };

        // Generate vertices
        GLuint baseIndex = static_cast<GLuint>(_vertices.size());

        _vertices.push_back({corners[0], uvs[0], color});
        _vertices.push_back({corners[1], uvs[1], color});
        _vertices.push_back({corners[2], uvs[2], color});
        _vertices.push_back({corners[3], uvs[3], color});

        // Two triangles per glyph
        _indices.push_back(baseIndex + 0);
        _indices.push_back(baseIndex + 1);
        _indices.push_back(baseIndex + 2);
        _indices.push_back(baseIndex + 1);
        _indices.push_back(baseIndex + 3);
        _indices.push_back(baseIndex + 2);

        penX += glyph.advance * scale;
    }
}

void MSDFTextRenderer::Render(const glm::mat4& projection) {
    if (!_initialized || !_renderItem || !_program) return;
    if (_vertices.empty()) return;

    // 1. 更新 GPU 缓冲区数据
    _renderItem->UpdateVertexBuffer("vertex",
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(_vertices.data()),
            _vertices.size() * sizeof(TextVertex)
        )
    );
    _renderItem->UpdateElementBuffer(_indices);

    // 2. 设置着色器 Uniform 变量
    _program->GetUniforms().SetByName("uProjection", projection);
    _program->GetUniforms().SetByName("uMSDFTexture", 0);

    // 3. 绑定纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _atlas->GetTextureID());

    // 4. 【抗锯齿设置】开启硬件多重采样（需窗口初始化支持）
    glEnable(GL_MULTISAMPLE);

    // 5. 【预乘 Alpha 混合模式】
    // Shader 里已经将 RGB 乘以 Alpha，这里不需要重复乘
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // 6. 执行绘制
    _renderItem->Draw({_program->Use()});

    // 7. 恢复状态
    glDisable(GL_BLEND);
    glDisable(GL_MULTISAMPLE);
}

} // namespace VCX::Labs::labf
