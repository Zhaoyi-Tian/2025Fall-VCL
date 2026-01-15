#include "Labs/WordCloud/Mask.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

#include <spdlog/spdlog.h>
#include <stb_image.h>

namespace VCX::Labs::labf {

    bool Mask::Load(const std::string& path, glm::ivec2 canvasSize) {
        // 检查文件是否存在
        if (! std::filesystem::exists(path)) {
            spdlog::error("Mask::Load: file not found: {}", path);
            return false;
        }

        // 使用 stb_image 加载图片（默认会翻转以符合 OpenGL 坐标系）
        int width, height, channels;
        auto* imageData = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (! imageData) {
            spdlog::error("Mask::Load: failed to load image: {}", path);
            return false;
        }

        _imageSize = glm::ivec2(width, height);
        _canvasSize = canvasSize;

        // 计算缩放比例，将蒙版缩放到画布大小
        float scaleX = static_cast<float>(canvasSize.x) / static_cast<float>(width);
        float scaleY = static_cast<float>(canvasSize.y) / static_cast<float>(height);
        _scale = std::min(scaleX, scaleY);  // 保持宽高比

        // 计算缩放后的中心点
        float scaledWidth = width * _scale;
        float scaledHeight = height * _scale;
        _center = glm::vec2(
            (canvasSize.x - scaledWidth) * 0.5f + scaledWidth * 0.5f,
            (canvasSize.y - scaledHeight) * 0.5f + scaledHeight * 0.5f
        );

        // 存储 alpha 通道数据
        _alphaData.resize(width * height);
        uint8_t* data = static_cast<uint8_t*>(imageData);
        for (int i = 0; i < width * height; ++i) {
            // RGBA 格式，A 通道在第 4 个字节
            _alphaData[i] = data[i * 4 + 3];
        }

        stbi_image_free(imageData);

        _valid = true;
        spdlog::info("Mask::Load: loaded mask from {} ({}x{}), scale={}", path, width, height, _scale);
        return true;
    }

    bool Mask::IsInside(glm::vec2 pos) const {
        if (! _valid || _alphaData.empty()) {
            return false;
        }

        // 将画布坐标转换为图片像素坐标
        float scaledWidth = _imageSize.x * _scale;
        float scaledHeight = _imageSize.y * _scale;

        // 计算蒙版在画布中的左上角位置
        float offsetX = (_canvasSize.x - scaledWidth) * 0.5f;
        float offsetY = (_canvasSize.y - scaledHeight) * 0.5f;

        // 转换坐标
        float localX = (pos.x - offsetX) / _scale;
        float localY = (pos.y - offsetY) / _scale;

        // 边界检查
        if (localX < 0 || localX >= _imageSize.x || localY < 0 || localY >= _imageSize.y) {
            return false;  // 在蒙版图片范围外
        }

        // 获取像素坐标（最近邻采样）
        int px = static_cast<int>(localX);
        int py = static_cast<int>(localY);

        // 检查 alpha 值
        int index = py * _imageSize.x + px;
        if (index < 0 || index < static_cast<int>(_alphaData.size())) {
            return _alphaData[index] > 0;
        }

        return false;
    }

    // JFA SDF 生成算法
    // ============================================================

    // JFA 迭代的 8 个方向偏移
    static constexpr int JFADirs[8][2] = {
        {-1, -1}, {0, -1}, {1, -1},
        {-1,  0},          {1,  0},
        {-1,  1}, {0,  1}, {1,  1}
    };

    void Mask::GenerateSDF() {
        if (! _valid || _alphaData.empty()) {
            spdlog::warn("Mask::GenerateSDF: mask not valid");
            return;
        }

        int width = _imageSize.x;
        int height = _imageSize.y;
        int size = width * height;

        _sdfData.resize(size);

        // JFA 辅助 lambda：计算到特征点（seed）的距离场
        auto computeDistanceField = [&](auto isSeed) -> std::vector<float> {
            std::vector<float> dists(size, std::numeric_limits<float>::max());
            std::vector<glm::vec2> seeds(size, glm::vec2(std::numeric_limits<float>::max()));

            // 初始化
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int idx = y * width + x;
                    if (isSeed(x, y)) {
                        dists[idx] = 0.0f;
                        seeds[idx] = glm::vec2(static_cast<float>(x), static_cast<float>(y));
                    }
                }
            }

            // JFA 迭代
            int step = std::max(1, std::max(width, height) / 2);
            while (step >= 1) {
                std::vector<glm::vec2> newSeeds = seeds;

                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        int idx = y * width + x;

                        // 检查 8 个方向的邻居
                        for (auto& d : JFADirs) {
                            int nx = x + d[0] * step;
                            int ny = y + d[1] * step;

                            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;

                            int nidx = ny * width + nx;

                            // 检查邻居的种子点是否有效
                            if (seeds[nidx].x == std::numeric_limits<float>::max()) continue;

                            // 计算到该种子的距离
                            float dx = static_cast<float>(x) - seeds[nidx].x;
                            float dy = static_cast<float>(y) - seeds[nidx].y;
                            float dist = std::sqrt(dx * dx + dy * dy);

                            if (dist < dists[idx]) {
                                dists[idx] = dist;
                                newSeeds[idx] = seeds[nidx];
                            }
                        }
                    }
                }
                seeds = std::move(newSeeds);
                step /= 2;
            }
            return dists;
        };

        // 1. 计算到“内部”（有效区域）的距离 d1
        // 内部点 d1=0，外部点 d1>0
        auto distToInside = computeDistanceField([&](int x, int y) { return HasAlphaAt(x, y); });

        // 2. 计算到“外部”（无效区域）的距离 d2
        // 外部点 d2=0，内部点 d2>0
        auto distToOutside = computeDistanceField([&](int x, int y) { return !HasAlphaAt(x, y); });

        // 3. 合并为 SDF
        // SDF = d1 - d2
        // 外部: d1 (>0) - 0 = Positive
        // 内部: 0 - d2 (>0) = Negative
        for (int i = 0; i < size; ++i) {
            _sdfData[i] = distToInside[i] - distToOutside[i];
        }

        spdlog::info("Mask::GenerateSDF: generated SDF ({}x{})", width, height);
    }

    float Mask::GetSDFDistanceCanvas(glm::vec2 pos) const {
        if (! _valid || _sdfData.empty()) {
            return std::numeric_limits<float>::max();
        }

        float scaledWidth = _imageSize.x * _scale;
        float scaledHeight = _imageSize.y * _scale;
        float offsetX = (_canvasSize.x - scaledWidth) * 0.5f;
        float offsetY = (_canvasSize.y - scaledHeight) * 0.5f;

        // 转换到局部像素坐标
        float localX = (pos.x - offsetX) / _scale;
        float localY = (pos.y - offsetY) / _scale;

        // 边界检查
        if (localX < 0 || localX >= _imageSize.x - 1 || localY < 0 || localY >= _imageSize.y - 1) {
             return 1000.0f;  // 外部点
        }

        // 双线性插值
        int x0 = static_cast<int>(floor(localX));
        int y0 = static_cast<int>(floor(localY));
        int x1 = x0 + 1;
        int y1 = y0 + 1;

        float fx = localX - x0;
        float fy = localY - y0;

        float d00 = GetSDFDistance(x0, y0);
        float d10 = GetSDFDistance(x1, y0);
        float d01 = GetSDFDistance(x0, y1);
        float d11 = GetSDFDistance(x1, y1);

        float d0 = d00 * (1.0f - fx) + d10 * fx;
        float d1 = d01 * (1.0f - fx) + d11 * fx;

        float dist = d0 * (1.0f - fy) + d1 * fy;

        // 转换回画布尺度的距离
        return dist * _scale;
    }

    glm::vec2 Mask::GetSDFNormal(glm::vec2 pos) const {
        if (! _valid || _sdfData.empty()) {
            return glm::vec2(0.0f);
        }

        // 使用中心差分计算梯度
        float h = _scale;  // 采样步长（画布单位）

        float distL = GetSDFDistanceCanvas(pos - glm::vec2(h, 0.0f));
        float distR = GetSDFDistanceCanvas(pos + glm::vec2(h, 0.0f));
        float distD = GetSDFDistanceCanvas(pos - glm::vec2(0.0f, h));
        float distU = GetSDFDistanceCanvas(pos + glm::vec2(0.0f, h));

        // 计算梯度（指向距离增加最快的方向，即指向蒙版外）
        glm::vec2 gradient(distR - distL, distU - distD);
        float len = glm::length(gradient);

        if (len < 1e-6f) {
            return glm::vec2(0.0f);
        }

        // 法线指向蒙版内（与梯度相反）
        return -gradient / len;
    }
} // namespace VCX::Labs::labf
