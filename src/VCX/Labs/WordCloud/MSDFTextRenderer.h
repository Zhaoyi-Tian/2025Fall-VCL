#pragma once

#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "Engine/GL/Program.h"
#include "Engine/GL/RenderItem.h"
#include "MSDFAtlas.h"
#include "TextVertex.h"

namespace VCX::Labs::labf {

/// MSDF-based text renderer with rotation support
class MSDFTextRenderer {
public:
    MSDFTextRenderer();
    ~MSDFTextRenderer();

    /// Initialize renderer with font file path
    /// @param fontPath Path to TTF/TTC font file
    bool Initialize(const std::string& fontPath);

    /// Add text to render queue
    /// @param text UTF-8 encoded text
    /// @param position Position of text center
    /// @param fontSize Font size in pixels
    /// @param color Text color (RGBA)
    /// @param rotation Rotation angle in radians
    void AddText(const std::string& text,
                 glm::vec2 position,
                 float fontSize,
                 glm::vec4 color,
                 float rotation = 0.0f);

    /// Clear all queued text
    void Clear();

    /// Render all queued text
    /// @param projection Orthographic projection matrix
    void Render(const glm::mat4& projection);

    /// Get the underlying atlas (for texture access)
    MSDFAtlas* GetAtlas() { return _atlas.get(); }

    /// Check if initialized
    bool IsInitialized() const { return _initialized; }

private:
    /// Generate vertices for a text string
    void GenerateTextVertices(const std::string& text,
                              glm::vec2 position,
                              float fontSize,
                              glm::vec4 color,
                              float rotation);

    std::unique_ptr<MSDFAtlas> _atlas;
    std::unique_ptr<Engine::GL::UniqueProgram> _program;
    std::unique_ptr<Engine::GL::UniqueIndexedRenderItem> _renderItem;

    std::vector<TextVertex> _vertices;
    std::vector<GLuint> _indices;

    bool _initialized = false;
};

} // namespace VCX::Labs::labf
