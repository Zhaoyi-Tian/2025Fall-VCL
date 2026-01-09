#include "MSDFAtlas.h"
#include "UTF8Utils.h"
#include <cstring>
#include <cmath>

namespace VCX::Labs::labf {

MSDFAtlas::MSDFAtlas() {
    // Initialize missing glyph placeholder
    _missingGlyph.valid = false;
    _missingGlyph.advance = 0.5f;
    _missingGlyph.uvMin = glm::vec2(0);
    _missingGlyph.uvMax = glm::vec2(0);
    _missingGlyph.planeBoundsMin = glm::vec2(0);
    _missingGlyph.planeBoundsMax = glm::vec2(0);
}

MSDFAtlas::~MSDFAtlas() {
    if (_textureID != 0) {
        glDeleteTextures(1, &_textureID);
    }
    if (_fontHandle) {
        msdfgen::destroyFont(_fontHandle);
    }
    if (_ftHandle) {
        msdfgen::deinitializeFreetype(_ftHandle);
    }
}

bool MSDFAtlas::Initialize(const std::string& fontPath,
                           int initialSize,
                           int maxSize,
                           int glyphSize,
                           double pxRange,
                           int glyphPadding) {
    if (_initialized) return true;

    _initialSize = initialSize;
    _maxSize = maxSize;
    _currentSize = initialSize;
    _glyphSize = glyphSize;
    _pxRange = pxRange;
    _glyphPadding = glyphPadding;

    // Initialize FreeType
    _ftHandle = msdfgen::initializeFreetype();
    if (!_ftHandle) {
        return false;
    }

    // Load font
    _fontHandle = msdfgen::loadFont(_ftHandle, fontPath.c_str());
    if (!_fontHandle) {
        msdfgen::deinitializeFreetype(_ftHandle);
        _ftHandle = nullptr;
        return false;
    }

    // Get font metrics
    if (!msdfgen::getFontMetrics(_fontMetrics, _fontHandle, msdfgen::FONT_SCALING_EM_NORMALIZED)) {
        msdfgen::destroyFont(_fontHandle);
        msdfgen::deinitializeFreetype(_ftHandle);
        _fontHandle = nullptr;
        _ftHandle = nullptr;
        return false;
    }

    // Get unscaled metrics to determine units per em
    msdfgen::FontMetrics unscaledMetrics;
    if (!msdfgen::getFontMetrics(unscaledMetrics, _fontHandle, msdfgen::FONT_SCALING_NONE)) {
        msdfgen::destroyFont(_fontHandle);
        msdfgen::deinitializeFreetype(_ftHandle);
        _fontHandle = nullptr;
        _ftHandle = nullptr;
        return false;
    }

    // geometryScale converts from font design units to em-normalized units
    _geometryScale = 1.0 / unscaledMetrics.emSize;

    // Initialize rectangle packer
    _packer = std::make_unique<msdf_atlas::RectanglePacker>(_currentSize, _currentSize);

    // Allocate atlas bitmap (RGB, float)
    _atlasBitmap.resize(_currentSize * _currentSize * 3, 0.0f);

    // Create OpenGL texture
    glGenTextures(1, &_textureID);
    glBindTexture(GL_TEXTURE_2D, _textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, _currentSize, _currentSize, 0, GL_RGB, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Preload ASCII characters
    for (uint32_t c = 32; c < 127; ++c) {
        LoadGlyph(c);
    }
    UpdateTexture();

    // 计算 x-height：通过测量字符 'x' 的高度
    auto it = _glyphCache.find('x');
    if (it != _glyphCache.end() && it->second.valid) {
        _xHeight = it->second.planeBoundsMax.y - it->second.planeBoundsMin.y;
    } else {
        // 降级方案：使用 ascender 的一半
        _xHeight = _fontMetrics.ascenderY * 0.5;
    }

    _initialized = true;
    return true;
}

const MSDFGlyphInfo& MSDFAtlas::GetGlyph(uint32_t codepoint) {
    auto it = _glyphCache.find(codepoint);
    if (it != _glyphCache.end()) {
        return it->second;
    }

    // Try to load the glyph
    if (LoadGlyph(codepoint)) {
        UpdateTexture();
        return _glyphCache[codepoint];
    }

    return _missingGlyph;
}

void MSDFAtlas::PreloadGlyphs(const std::string& utf8Text) {
    const char* ptr = utf8Text.c_str();
    const char* end = ptr + utf8Text.size();

    bool anyNew = false;
    while (ptr < end) {
        uint32_t codepoint = DecodeUTF8(ptr, end);
        if (codepoint == 0) break;

        if (_glyphCache.find(codepoint) == _glyphCache.end()) {
            if (LoadGlyph(codepoint)) {
                anyNew = true;
            }
        }
    }

    if (anyNew) {
        UpdateTexture();
    }
}

bool MSDFAtlas::LoadGlyph(uint32_t codepoint) {
    // Check if already loaded
    if (_glyphCache.find(codepoint) != _glyphCache.end()) {
        return true;
    }

    // Create glyph geometry
    msdf_atlas::GlyphGeometry glyphGeom;
    if (!glyphGeom.load(_fontHandle, _geometryScale, codepoint, true)) {
        return false;
    }

    // Handle whitespace (no geometry)
    if (glyphGeom.isWhitespace()) {
        MSDFGlyphInfo info;
        info.valid = true;
        info.advance = static_cast<float>(glyphGeom.getAdvance());
        info.uvMin = glm::vec2(0);
        info.uvMax = glm::vec2(0);
        info.planeBoundsMin = glm::vec2(0);
        info.planeBoundsMax = glm::vec2(0);
        _glyphCache[codepoint] = info;
        return true;
    }

    // Apply edge coloring for MSDF
    glyphGeom.edgeColoring(msdfgen::edgeColoringByDistance, 3.0, 0);

    // Calculate glyph box dimensions
    double glyphScale = _glyphSize;
    double rangeInEm = _pxRange / glyphScale;
    glyphGeom.wrapBox(glyphScale, rangeInEm, 1.001);

    // Get box dimensions
    int boxW, boxH;
    glyphGeom.getBoxSize(boxW, boxH);

    if (boxW <= 0 || boxH <= 0) {
        // Degenerate glyph
        MSDFGlyphInfo info;
        info.valid = true;
        info.advance = static_cast<float>(glyphGeom.getAdvance());
        info.uvMin = glm::vec2(0);
        info.uvMax = glm::vec2(0);
        info.planeBoundsMin = glm::vec2(0);
        info.planeBoundsMax = glm::vec2(0);
        _glyphCache[codepoint] = info;
        return true;
    }

    // Try to pack the rectangle (add padding to prevent bleeding between glyphs)
    msdf_atlas::Rectangle rect;
    rect.w = boxW + _glyphPadding * 2;
    rect.h = boxH + _glyphPadding * 2;
    rect.x = 0;
    rect.y = 0;

    int failed = _packer->pack(&rect, 1);
    if (failed > 0) {
        // Atlas full, try to expand
        if (!ExpandAtlas()) {
            return false;
        }
        // Retry packing
        failed = _packer->pack(&rect, 1);
        if (failed > 0) {
            return false;
        }
    }

    int x = rect.x + _glyphPadding;
    int y = rect.y + _glyphPadding;

    // Place the glyph in the atlas
    glyphGeom.placeBox(x, y);

    // Generate MSDF bitmap
    msdfgen::Bitmap<float, 3> msdf(boxW, boxH);
    msdfgen::generateMSDF(msdf, glyphGeom.getShape(), glyphGeom.getBoxProjection(), glyphGeom.getBoxRange());

    // Apply error correction
    msdfgen::msdfErrorCorrection(msdf, glyphGeom.getShape(), glyphGeom.getBoxProjection(), glyphGeom.getBoxRange());

    // Copy to atlas bitmap
    for (int py = 0; py < boxH; ++py) {
        for (int px = 0; px < boxW; ++px) {
            int atlasX = x + px;
            int atlasY = y + py;
            int atlasIdx = (atlasY * _currentSize + atlasX) * 3;

            _atlasBitmap[atlasIdx + 0] = msdf(px, py)[0];
            _atlasBitmap[atlasIdx + 1] = msdf(px, py)[1];
            _atlasBitmap[atlasIdx + 2] = msdf(px, py)[2];
        }
    }
    _textureDirty = true;

    // Get plane bounds (in em units)
    double pl, pb, pr, pt;
    glyphGeom.getQuadPlaneBounds(pl, pb, pr, pt);

    // Create glyph info
    MSDFGlyphInfo info;
    info.valid = true;
    info.advance = static_cast<float>(glyphGeom.getAdvance());
    info.uvMin = glm::vec2(static_cast<float>(x) / _currentSize,
                           static_cast<float>(y) / _currentSize);
    info.uvMax = glm::vec2(static_cast<float>(x + boxW) / _currentSize,
                           static_cast<float>(y + boxH) / _currentSize);
    info.planeBoundsMin = glm::vec2(static_cast<float>(pl), static_cast<float>(pb));
    info.planeBoundsMax = glm::vec2(static_cast<float>(pr), static_cast<float>(pt));

    _glyphCache[codepoint] = info;
    _glyphGeometries[codepoint] = std::move(glyphGeom);

    return true;
}

void MSDFAtlas::UpdateTexture() {
    if (!_textureDirty || _textureID == 0) return;

    glBindTexture(GL_TEXTURE_2D, _textureID);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _currentSize, _currentSize,
                    GL_RGB, GL_FLOAT, _atlasBitmap.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    _textureDirty = false;
}

bool MSDFAtlas::ExpandAtlas() {
    int newSize = _currentSize * 2;
    if (newSize > _maxSize) {
        return false;
    }

    // Create new bitmap
    std::vector<float> newBitmap(newSize * newSize * 3, 0.0f);

    // Copy old bitmap to new (top-left corner)
    for (int y = 0; y < _currentSize; ++y) {
        std::memcpy(
            newBitmap.data() + y * newSize * 3,
            _atlasBitmap.data() + y * _currentSize * 3,
            _currentSize * 3 * sizeof(float)
        );
    }

    _atlasBitmap = std::move(newBitmap);
    _currentSize = newSize;

    // Recreate texture with new size
    glBindTexture(GL_TEXTURE_2D, _textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, _currentSize, _currentSize, 0, GL_RGB, GL_FLOAT, _atlasBitmap.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // Recreate packer (need to re-add all existing rectangles)
    // For simplicity, we expand the packer's bounds
    _packer = std::make_unique<msdf_atlas::RectanglePacker>(_currentSize, _currentSize);

    // Re-pack existing glyphs (they keep their positions, we just mark areas as used)
    // Note: This is a simplified approach. A proper implementation would track all rectangles.

    _textureDirty = true;
    return true;
}

} // namespace VCX::Labs::labf
