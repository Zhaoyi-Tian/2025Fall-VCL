#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <glad/glad.h>
#include <glm/glm.hpp>

// msdfgen headers
#include <msdfgen.h>
#include <msdfgen-ext.h>
#include <msdf-atlas-gen.h>

namespace VCX::Labs::labf {

/// Glyph info for rendering
struct MSDFGlyphInfo {
    glm::vec2 uvMin;        // UV coordinates in atlas (bottom-left)
    glm::vec2 uvMax;        // UV coordinates in atlas (top-right)
    glm::vec2 planeBoundsMin;  // Quad bounds relative to baseline (in em units)
    glm::vec2 planeBoundsMax;
    float advance;          // Horizontal advance (in em units)
    bool valid = false;     // Whether this glyph was successfully loaded
};

/// Dynamic MSDF font atlas with lazy glyph loading
class MSDFAtlas {
public:
    MSDFAtlas();
    ~MSDFAtlas();

    /// Initialize with font file path
    /// @param fontPath Path to TTF/TTC font file
    /// @param initialSize Initial atlas size (e.g., 2048)
    /// @param maxSize Maximum atlas size (e.g., 4096)
    /// @param glyphSize Size of each glyph in pixels (affects quality)
    /// @param pxRange Pixel range for distance field (larger = better anti-aliasing but uses more atlas space)
    /// @param glyphPadding Extra padding between glyphs to prevent bleeding
    bool Initialize(const std::string& fontPath,
                    int initialSize = 2048,
                    int maxSize = 4096,
                    int glyphSize = 48,
                    double pxRange = 8.0,
                    int glyphPadding = 2);

    /// Get glyph info for a Unicode codepoint (loads if not cached)
    const MSDFGlyphInfo& GetGlyph(uint32_t codepoint);

    /// Ensure all glyphs for a string are loaded
    void PreloadGlyphs(const std::string& utf8Text);

    /// Get OpenGL texture ID
    GLuint GetTextureID() const { return _textureID; }

    /// Get atlas size
    int GetAtlasSize() const { return _currentSize; }

    /// Get pixel range (needed for shader)
    double GetPxRange() const { return _pxRange; }

    /// Get font metrics
    double GetEmSize() const { return _fontMetrics.emSize; }
    double GetAscender() const { return _fontMetrics.ascenderY; }
    double GetDescender() const { return _fontMetrics.descenderY; }
    double GetLineHeight() const { return _fontMetrics.lineHeight; }
    double GetXHeight() const { return _xHeight; }

    /// Check if initialized
    bool IsInitialized() const { return _initialized; }

private:
    /// Load a single glyph into the atlas
    bool LoadGlyph(uint32_t codepoint);

    /// Rebuild atlas texture from bitmap data
    void UpdateTexture();

    /// Expand atlas size if needed
    bool ExpandAtlas();

    // FreeType handles
    msdfgen::FreetypeHandle* _ftHandle = nullptr;
    msdfgen::FontHandle* _fontHandle = nullptr;

    // Font metrics
    msdfgen::FontMetrics _fontMetrics;
    double _xHeight = 0.5;  // x-height（em 单位）

    // Atlas configuration
    int _initialSize;
    int _maxSize;
    int _currentSize;
    int _glyphSize;
    double _pxRange;
    double _geometryScale;
    int _glyphPadding;  // Extra padding between glyphs to prevent bleeding

    // Glyph cache
    std::unordered_map<uint32_t, MSDFGlyphInfo> _glyphCache;
    std::unordered_map<uint32_t, msdf_atlas::GlyphGeometry> _glyphGeometries;

    // Atlas bitmap (MSDF uses 3 channels: RGB)
    std::vector<float> _atlasBitmap;

    // OpenGL texture
    GLuint _textureID = 0;
    bool _textureDirty = false;

    // Rectangle packer for dynamic placement
    std::unique_ptr<msdf_atlas::RectanglePacker> _packer;

    // Placeholder for missing glyphs
    MSDFGlyphInfo _missingGlyph;

    bool _initialized = false;
};

} // namespace VCX::Labs::labf
