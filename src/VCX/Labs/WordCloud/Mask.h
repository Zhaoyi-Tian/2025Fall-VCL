#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

namespace VCX::Labs::labf {

    // 蒙版类：加载并查询蒙版区域
    // 黑色像素 = 有效区域（可以放置词）
    // 透明像素 = 无效区域（不可以放置词）
    class Mask {
    public:
        // 加载蒙版图片
        // path: 蒙版图片路径
        // canvasSize: 画布大小，用于缩放蒙版
        bool Load(const std::string& path, glm::ivec2 canvasSize);

        // 检查点是否在蒙版区域内
        // pos: 画布坐标系中的点
        // 返回: true 表示点在蒙版（黑色）区域内
        bool IsInside(glm::vec2 pos) const;

        // 获取蒙版区域的中心点
        glm::vec2 GetCenter() const { return _center; }

        // 获取蒙版的缩放比例
        float GetScale() const { return _scale; }

        // 蒙版是否有效（加载成功）
        bool IsValid() const { return _valid; }

        // 获取原始图片尺寸
        glm::ivec2 GetImageSize() const { return _imageSize; }

        // 获取画布尺寸
        glm::ivec2 GetCanvasSize() const { return _canvasSize; }

        // 检查指定像素是否有 alpha（用于 SDF 种子点初始化）
        bool HasAlphaAt(int x, int y) const {
            if (x < 0 || x >= _imageSize.x || y < 0 || y >= _imageSize.y) {
                return false;
            }
            int index = y * _imageSize.x + x;
            return index >= 0 && index < static_cast<int>(_alphaData.size()) && _alphaData[index] > 0;
        }

        // ============================================================
        // SDF (Signed Distance Field) 方法
        // ============================================================

        // 生成 SDF（使用 JFA 算法）
        // 在加载蒙版后调用一次
        void GenerateSDF();

        // 获取 SDF 距离（像素坐标）
        // 负值表示在蒙版内部，正值表示在蒙版外部
        float GetSDFDistance(int x, int y) const {
            if (x < 0 || x >= _imageSize.x || y < 0 || y >= _imageSize.y) {
                return FLT_MAX;  // 外部返回大正数
            }
            int index = y * _imageSize.x + x;
            return _sdfData[index];
        }

        // 获取 SDF 距离（画布坐标，双线性插值）
        float GetSDFDistanceCanvas(glm::vec2 pos) const;

        // 获取 SDF 法线（画布坐标，通过梯度计算）
        glm::vec2 GetSDFNormal(glm::vec2 pos) const;

        // 检查点是否在蒙版边界上（使用 SDF）
        // threshold: 距离阈值（像素单位），默认为 1.0
        // 边界判断：距离边界足够近（用梯度幅度判断是否在边界附近）
        bool IsOnBoundary(glm::vec2 pos, float threshold = 1.0f) const {
            float dist = GetSDFDistanceCanvas(pos);
            glm::vec2 normal = GetSDFNormal(pos);

            // 梯度长度可以判断是否在边界附近：
            // - 边界附近：梯度明显，法线长度接近 1
            // - 内部深处：梯度为 0（所有内部点距离都是 0）
            float normalLen = glm::length(normal);

            // 如果法线有效（长度接近 1），说明在边界附近
            return normalLen > 0.5f && std::abs(dist) < threshold * _scale * 2.0f;
        }

        // SDF 是否已生成
        bool HasSDF() const { return !_sdfData.empty(); }

    private:
        bool _valid = false;
        float _scale = 1.0f;
        glm::ivec2 _imageSize { 0, 0 };
        glm::ivec2 _canvasSize { 0, 0 };
        glm::vec2 _center { 0.0f, 0.0f };
        std::vector<uint8_t> _alphaData; // 存储 alpha 通道数据
        std::vector<float> _sdfData;     // 存储 SDF 距离（负值在内部，正值在外部）
    };
} // namespace VCX::Labs::labf
