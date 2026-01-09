#include "Labs/5-Visualization/tasks.h"
#include <algorithm>
#include <vector>
#include <string>
#include <cmath>
#include <sstream>

using VCX::Labs::Common::ImageRGB;
using glm::vec2;
using glm::vec3;
using glm::vec4;

namespace VCX::Labs::Visualization {

    struct CoordinateStates {
        static const int DIM_COUNT = 7;
        const std::vector<std::string> labels = {
            "cylinders", "displacement", "weight", "horsepower", 
            "acceleration", "mileage", "year"
        };

        std::vector<float> minVal, maxVal;
        bool initialized = false;
        bool rendered    = false; // 用于性能优化的标志位

        float GetCarValue(const Car& car, int dim) const {
            switch (dim) {
                case 0: return (float)car.cylinders;
                case 1: return (float)car.displacement;
                case 2: return (float)car.weight;
                case 3: return (float)car.horsepower;
                case 4: return (float)car.acceleration;
                case 5: return (float)car.mileage;
                case 6: return (float)car.year;
                default: return 0.0f;
            }
        }

        void Initialize(std::vector<Car> const & data) {
            minVal.assign(DIM_COUNT, std::numeric_limits<float>::max());
            maxVal.assign(DIM_COUNT, std::numeric_limits<float>::lowest());
            for (const auto& car : data) {
                for (int i = 0; i < DIM_COUNT; ++i) {
                    float v = GetCarValue(car, i);
                    minVal[i] = std::min(minVal[i], v);
                    maxVal[i] = std::max(maxVal[i], v);
                }
            }
            initialized = true;
            rendered    = false; // 初始化后需要重绘
        }

        float Normalize(int dim, float val) const {
            float range = maxVal[dim] - minVal[dim];
            return (std::abs(range) < 1e-5f) ? 0.5f : (val - minVal[dim]) / range;
        }
    };

    bool PaintParallelCoordinates(Common::ImageRGB & input, InteractProxy const & proxy, std::vector<Car> const & data, bool force) {
        static CoordinateStates states;

        // 1. 初始化数据
        if (! states.initialized) {
            states.Initialize(data);
        }

        // 2. 性能优化：检查是否需要重绘
        if (! force && states.rendered) {
            return false;
        }

        // --- 开始重绘逻辑 ---

        // 3. 背景
        SetBackGround(input, vec4(1, 1, 1, 1));

        // 4. 布局参数
        const float topPad    = 0.12f;
        const float bottomPad = 0.10f;
        const float sidePad   = 0.08f;
        const float usableW   = 1.0f - 2.0f * sidePad;
        const float usableH   = 1.0f - topPad - bottomPad;
        const float stepX     = usableW / (CoordinateStates::DIM_COUNT - 1);

        auto GetX = [&](int i) { return sidePad + i * stepX; };
        auto GetY = [&](float normVal) { return (1.0f - bottomPad) - normVal * usableH; };

        // 5. 绘制数据线 (包含抗锯齿)
        vec4 colorSteelBlue(0.27f, 0.51f, 0.71f, 0.3f);
        vec4 colorBrown(0.65f, 0.16f, 0.16f, 0.3f);

        for (const auto& car : data) {
            float activeNorm = states.Normalize(5, states.GetCarValue(car, 5));
            vec4 lineColor = glm::mix(colorSteelBlue, colorBrown, activeNorm);

            for (int i = 0; i < CoordinateStates::DIM_COUNT - 1; ++i) {
                vec2 p0(GetX(i),   GetY(states.Normalize(i, states.GetCarValue(car, i))));
                vec2 p1(GetX(i + 1), GetY(states.Normalize(i + 1, states.GetCarValue(car, i + 1))));
                DrawLine(input, lineColor, p0, p1, 1.0f);
            }
        }

        // 6. 绘制轴和精简文本（字号缩小，去单位）
        vec4 labelColor(0.2f, 0.2f, 0.2f, 1.0f);
        vec4 axisColor(0.85f, 0.85f, 0.85f, 1.0f); 

        for (int i = 0; i < CoordinateStates::DIM_COUNT; ++i) {
            float x = GetX(i);
            
            // 绘制轴线
            DrawLine(input, axisColor, vec2(x, topPad), vec2(x, 1.0f - bottomPad), 1.0f);

            // 属性标题
            PrintText(input, labelColor, vec2(x, topPad - 0.05f), 0.020f, states.labels[i]);

            // 最小值数字
            PrintText(input, labelColor, vec2(x, 1.0f - bottomPad + 0.025f), 0.015f, std::to_string((int)states.minVal[i]));

            // 最大值数字
            PrintText(input, labelColor, vec2(x, topPad - 0.015f), 0.015f, std::to_string((int)states.maxVal[i]));
        }

        // 7. 标记已完成渲染
        states.rendered = true;
        return true;
    }

    void LIC(ImageRGB & output, Common::ImageRGB const & noise, VectorField2D const & field, int const & step) {
        // 确保输出图像尺寸与输入一致

        int width = output.GetSizeX();
        int height = output.GetSizeY();

        // 遍历输出图像的每一个像素
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                
                // 当前像素作为流线中心
                float accumNoise = 0.0f;
                int sampleCount = 0;

                // 我们需要向两个方向积分：正向和反向
                // direction: 1 (Forward), -1 (Backward)
                for (int direction = -1; direction <= 1; direction += 2) {
                    float curX = x + 0.5f; // 使用像素中心坐标
                    float curY = y + 0.5f;
                    
                    // 初始位置采样（只在正向循环时采一次，或者两个循环各采一次但不重复计算中心点）
                    // 简单的写法是：正向积分包含中心点，反向积分从中心点偏移一步开始
                    // 这里采用简单的双向累加策略
                    
                    if (direction == 1) {
                         // 中心点采样一次
                        accumNoise += noise.At(x, y).x; // 假设噪声是灰度的，取 R 通道
                        sampleCount++;
                    }

                    for (int k = 0; k < step; ++k) {
                        // 1. 获取当前位置的整数坐标用于查找向量场
                        int ix = std::clamp((int)curX, 0, width - 1);
                        int iy = std::clamp((int)curY, 0, height - 1);

                        // 2. 获取向量
                        glm::vec2 v = field.At(ix, iy);
                        
                        // 如果速度极小，视为静止，停止该方向积分
                        if (glm::length(v) < 1e-4f) break;

                        // 3. 归一化向量 (LIC 通常沿流线方向以固定步长移动，而非物理速度)
                        v = glm::normalize(v);

                        // 4. 更新位置 (积分)
                        if (direction == 1) {
                            curX += v.x;
                            curY += v.y;
                        } else {
                            curX -= v.x;
                            curY -= v.y;
                        }

                        // 5. 边界检查
                        if (curX < 0 || curX >= width || curY < 0 || curY >= height) break;

                        // 6. 采样噪声并累加
                        int nx = std::clamp((int)curX, 0, width - 1);
                        int ny = std::clamp((int)curY, 0, height - 1);
                        accumNoise += noise.At(nx, ny).x;
                        sampleCount++;
                    }
                }

                // 计算平均值
                float result = accumNoise / (float)sampleCount;
                
                // 写入输出像素
                output.At(x, y) = glm::vec3(result, result, result);
            }
        }
    }

}; // namespace VCX::Labs::Visualization