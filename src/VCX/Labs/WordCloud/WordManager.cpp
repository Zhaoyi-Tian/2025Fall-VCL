#include "Labs/WordCloud/WordManager.h"
#include "Labs/WordCloud/PhysicsSimulator.h"

namespace VCX::Labs::labf {

WordEntity & WordManager::add(std::string const & text, float fontSize) {
    _words.emplace_back();
    auto & w = _words.back();
    w.text = text;
    w.fontSize = fontSize;
    // 初始位置与 OBB 估计（后续由度量器精确计算）
    w.position = { 100.f * float(_words.size()), 100.f };
    float wpx = fontSize * 0.6f * float(text.size());
    float hpx = fontSize * 0.6f;
    w.boxHalfSize = { wpx * 0.5f, hpx * 0.5f };
    w.fullHeight = fontSize * 1.2f;
    w.xHeight = fontSize * 0.5f;
    return w;
}

bool WordManager::remove(std::size_t idx) {
    if (idx >= _words.size()) return false;
    _words.erase(_words.begin() + idx);
    // 标记矩阵需要重新计算
    _matrixSize = 0;
    _similarityMatrix.clear();
    return true;
}

void WordManager::clear() {
    _words.clear();
    _similarityMatrix.clear();
    _matrixSize = 0;
}

std::optional<std::size_t> WordManager::hitTest(glm::vec2 const & pt) const {
    for (std::size_t i = 0; i < _words.size(); ++i) {
        auto const & w = _words[i];
        // 使用交互 OBB 进行点击测试
        OBB obb = GetWordOBB(w);
        if (PointInOBB(pt, obb))
            return i;
    }
    return std::nullopt;
}

float WordManager::cosineSimilarity(std::vector<float> const& a, std::vector<float> const& b) {
    if (a.empty() || b.empty() || a.size() != b.size()) return 0.0f;

    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }

    float denom = std::sqrt(normA) * std::sqrt(normB);
    if (denom < 1e-8f) return 0.0f;

    return dot / denom;
}

std::size_t WordManager::getMatrixIndex(std::size_t i, std::size_t j) const {
    // 确保 i < j
    if (i > j) std::swap(i, j);
    if (i == j) return SIZE_MAX;  // 无效索引

    // 上三角矩阵索引：index = i * n - i*(i+1)/2 + (j - i - 1)
    std::size_t n = _matrixSize;
    return i * n - i * (i + 1) / 2 + (j - i - 1);
}

float WordManager::getSimilarity(std::size_t i, std::size_t j) const {
    if (i == j) return 1.0f;  // 自身相似度为 1
    if (i >= _words.size() || j >= _words.size()) return 0.0f;

    // 如果矩阵尚未计算或尺寸不匹配，返回实时计算的结果
    if (_matrixSize != _words.size() || _similarityMatrix.empty()) {
        return cosineSimilarity(_words[i].wordVector, _words[j].wordVector);
    }

    std::size_t idx = getMatrixIndex(i, j);
    if (idx >= _similarityMatrix.size()) return 0.0f;

    return _similarityMatrix[idx];
}

void WordManager::recomputeSimilarityMatrix() {
    std::size_t n = _words.size();
    _matrixSize = n;

    if (n < 2) {
        _similarityMatrix.clear();
        return;
    }

    // 上三角矩阵大小：n*(n-1)/2
    std::size_t matrixElements = n * (n - 1) / 2;
    _similarityMatrix.resize(matrixElements);

    // 计算所有词对的相似度
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            std::size_t idx = getMatrixIndex(i, j);
            _similarityMatrix[idx] = cosineSimilarity(_words[i].wordVector, _words[j].wordVector);
        }
    }
}

} // namespace VCX::Labs::labf
