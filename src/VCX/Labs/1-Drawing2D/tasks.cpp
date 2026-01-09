#include <algorithm>
#include <cstddef>
#include <cstdlib>

#include "Labs/1-Drawing2D/tasks.h"
#include <random>
#include <spdlog/spdlog.h>
#include <vector>

using VCX::Labs::Common::ImageRGB;

namespace VCX::Labs::Drawing2D {
    /******************* 1.Image Dithering *****************/
    void DitheringThreshold(
        ImageRGB &       output,
        ImageRGB const & input) {
        for (std::size_t x = 0; x < input.GetSizeX(); ++x)
            for (std::size_t y = 0; y < input.GetSizeY(); ++y) {
                glm::vec3 color = input.At(x, y);
                output.At(x, y) = {
                    color.r > 0.5 ? 1 : 0,
                    color.g > 0.5 ? 1 : 0,
                    color.b > 0.5 ? 1 : 0,
                };
            }
    }

    void DitheringRandomUniform(
        ImageRGB &       output,
        ImageRGB const & input) {
        // your code here:
        static std::random_device             rd;
        static std::mt19937                   engine(rd());
        std::uniform_real_distribution<float> dist(-0.5, 0.5);
        for (std::size_t x = 0; x < input.GetSizeX(); ++x)
            for (std::size_t y = 0; y < input.GetSizeY(); ++y) {
                glm::vec3 color = input.At(x, y);
                float     r     = dist(engine);
                color += r;
                output.At(x, y) = {
                    color.r > 0.5 ? 1 : 0,
                    color.g > 0.5 ? 1 : 0,
                    color.b > 0.5 ? 1 : 0,
                };
            }
    }

    void DitheringRandomBlueNoise(
        ImageRGB &       output,
        ImageRGB const & input,
        ImageRGB const & noise) {
        // your code here:
        for (std::size_t x = 0; x < input.GetSizeX(); ++x)
            for (std::size_t y = 0; y < input.GetSizeY(); ++y) {
                glm::vec3 color = input.At(x, y) + noise.At(x, y) - 0.5f;
                output.At(x, y) = {
                    color.r > 0.5 ? 1 : 0,
                    color.g > 0.5 ? 1 : 0,
                    color.b > 0.5 ? 1 : 0,
                };
            }
    }

    void DitheringOrdered(
        ImageRGB &       output,
        ImageRGB const & input) {
        // your code here:
        std::vector<std::vector<float>> M = {
            { 6, 8, 4 },
            { 1, 0, 3 },
            { 5, 2, 7 }
        };
        std::size_t iwidth  = input.GetSizeX();
        std::size_t iheight = input.GetSizeY();
        for (size_t x = 0; x < iwidth; x++) {
            for (size_t y = 0; y < iheight; y++) {
                for (size_t i = 0; i < 3; i++) {
                    for (size_t j = 0; j < 3; j++) {
                        if (input.At(x, y).r > (M[i][j] / 9)) {
                            output.At(3 * x + i, 3 * y + j) = glm::vec3(1.0f);
                        } else {
                            output.At(3 * x + i, 3 * y + j) = glm::vec3(0.0f);
                        }
                    }
                }
            }
        }
    }

    void DitheringErrorDiffuse(
        ImageRGB &       output,
        ImageRGB const & input) {
        // your code here:
        ImageRGB    temp(input);
        std::size_t iwidth  = input.GetSizeX();
        std::size_t iheight = input.GetSizeY();
        for (size_t y = 0; y < iheight; y++) {
            for (size_t x = 0; x < iwidth; x++) {
                glm::vec3 color = temp.At(x, y);
                glm::vec3 after = {
                    color.r > 0.5 ? 1 : 0,
                    color.g > 0.5 ? 1 : 0,
                    color.b > 0.5 ? 1 : 0,
                };
                output.At(x, y) = after;
                glm::vec3 error = -(after - color);
                int       ix;
                int       iy;
                ix = x + 1;
                iy = y;
                if (ix >= 0 && ix < iwidth && iy >= 0 && iy < iheight) {
                    glm::vec3 icolor = temp.At(ix, iy);
                    icolor += error * 7.0f / 16.0f;
                    temp.At(ix, iy) = icolor;
                }
                ix = x - 1;
                iy = y + 1;
                if (ix >= 0 && ix < iwidth && iy >= 0 && iy < iheight) {
                    glm::vec3 icolor = temp.At(ix, iy);
                    icolor += error * 3.0f / 16.0f;
                    temp.At(ix, iy) = icolor;
                }
                ix = x;
                iy = y + 1;
                if (ix >= 0 && ix < iwidth && iy >= 0 && iy < iheight) {
                    glm::vec3 icolor = temp.At(ix, iy);
                    icolor += error * 5.0f / 16.0f;
                    temp.At(ix, iy) = icolor;
                }
                ix = x + 1;
                iy = y + 1;
                if (ix >= 0 && ix < iwidth && iy >= 0 && iy < iheight) {
                    glm::vec3 icolor = temp.At(ix, iy);
                    icolor += error * 1.0f / 16.0f;
                    temp.At(ix, iy) = icolor;
                }
            }
        }
    }

    /******************* 2.Image Filtering *****************/
    void Blur(
        ImageRGB &       output,
        ImageRGB const & input) {
        // your code here:
        std::vector<std::vector<float>> kernel = {
            { 1, 1, 1 },
            { 1, 1, 1 },
            { 1, 1, 1 }
        };
        std::size_t iwidth  = input.GetSizeX();
        std::size_t iheight = input.GetSizeY();
        std::size_t owidth  = output.GetSizeX();
        std::size_t oheight = output.GetSizeY();

        for (int x = 0; x < owidth; x++) {
            for (int y = 0; y < oheight; y++) {
                glm::vec3 sum(0.0f, 0.0f, 0.0f);
                for (int kx = 0; kx < 3; kx++) {
                    for (int ky = 0; ky < 3; ky++) {
                        int ix = x + kx - 1;
                        int iy = y + ky - 1;

                        if (ix >= 0 && ix < iwidth && iy >= 0 && iy < iheight) {
                            sum += input.At(ix, iy) * kernel[kx][ky];
                        }
                    }
                }
                sum /= 9;
                output.At(x, y) = sum;
            }
        }
    }

    void Edge(
        ImageRGB &       output,
        ImageRGB const & input) {
        // your code here:
        std::vector<std::vector<float>> vkernel = {
            { -1, 0, 1 },
            { -2, 0, 2 },
            { -1, 0, 1 }
        };
        std::vector<std::vector<float>> hkernel = {
            { -1, -2, -1 },
            {  0,  0,  0 },
            {  1,  2,  1 }
        };
        std::size_t iwidth  = input.GetSizeX();
        std::size_t iheight = input.GetSizeY();
        std::size_t owidth  = input.GetSizeX();
        std::size_t oheight = input.GetSizeY();

        for (int x = 0; x < owidth; x++) {
            for (int y = 0; y < oheight; y++) {
                glm::vec3 sumx(0.0f, 0.0f, 0.0f);
                glm::vec3 sumy(0.0f, 0.0f, 0.0f);
                for (int kx = 0; kx < 3; kx++) {
                    for (int ky = 0; ky < 3; ky++) {
                        int ix = x + kx - 1;
                        int iy = y + ky - 1;

                        if (ix >= 0 && ix < iwidth && iy >= 0 && iy < iheight) {
                            sumx += input.At(ix, iy) * (hkernel[kx][ky]);
                            sumy += input.At(ix, iy) * (vkernel[kx][ky]);
                        }
                    }
                }
                output.At(x, y) = sqrt(sumx * sumx + sumy * sumy);
            }
        }
    }

    /******************* 3. Image Inpainting *****************/
    void Inpainting(
        ImageRGB &         output,
        ImageRGB const &   inputBack,
        ImageRGB const &   inputFront,
        const glm::ivec2 & offset) {
        output             = inputBack;
        std::size_t width  = inputFront.GetSizeX();
        std::size_t height = inputFront.GetSizeY();
        glm::vec3 * g      = new glm::vec3[width * height];
        memset(g, 0, sizeof(glm::vec3) * width * height);
        // set boundary condition
        for (std::size_t y = 0; y < height; ++y) {
            // set boundary for (0, y), your code: g[y * width] = ?
            // set boundary for (width - 1, y), your code: g[y * width + width - 1] = ?
            glm::vec3 color          = output.At(offset.x, y + offset.y);
            g[y * width]             = color - inputFront.At(0, y);
            color                    = output.At(offset.x + width - 1, y + offset.y);
            g[y * width + width - 1] = color - inputFront.At(width - 1, y);
        }
        for (std::size_t x = 0; x < width; ++x) {
            // set boundary for (x, 0), your code: g[x] = ?
            // set boundary for (x, height - 1), your code: g[(height - 1) * width + x] = ?
            glm::vec3 color             = output.At(offset.x + x, offset.y);
            g[x]                        = color - inputFront.At(x, 0);
            color                       = output.At(offset.x + x, offset.y + height - 1);
            g[(height - 1) * width + x] = color - inputFront.At(x, height - 1);
        }

        // Jacobi iteration, solve Ag = b
        for (int iter = 0; iter < 8000; ++iter) {
            for (std::size_t y = 1; y < height - 1; ++y)
                for (std::size_t x = 1; x < width - 1; ++x) {
                    g[y * width + x] = (g[(y - 1) * width + x] + g[(y + 1) * width + x] + g[y * width + x - 1] + g[y * width + x + 1]);
                    g[y * width + x] = g[y * width + x] * glm::vec3(0.25);
                }
        }

        for (std::size_t y = 0; y < inputFront.GetSizeY(); ++y)
            for (std::size_t x = 0; x < inputFront.GetSizeX(); ++x) {
                glm::vec3 color                       = g[y * width + x] + inputFront.At(x, y);
                output.At(x + offset.x, y + offset.y) = color;
            }
        delete[] g;
    }

    /******************* 4. Line Drawing *****************/
    void DrawLine(
        ImageRGB &       canvas,
        glm::vec3 const  color,
        glm::ivec2 const p0,
        glm::ivec2 const p1) {
        // your code here:
        int x0;
        int x1;
        int y0;
        int y1;
        int dx = abs(p0.x - p1.x);
        int dy = abs(p0.y - p1.y);
        // 假如|k|<=1,则y随x递增而决策
        if (dx >= dy) {
            if (p0.x < p1.x) {
                x0 = p0.x;
                y0 = p0.y;
                x1 = p1.x;
                y1 = p1.y;
            } else {
                x0 = p1.x;
                y0 = p1.y;
                x1 = p0.x;
                y1 = p0.y;
            }
            int stepy = 1;
            if ((y1 - y0) < 0) {
                stepy = -1;
            }
            int F    = 2 * dy - dx;
            int dxdy = 2 * dy - 2 * dx;
            int y    = y0;
            int d2y  = 2 * dy;
            for (int i = x0; i <= x1; i++) {
                canvas.At(i, y) = color;
                if (F < 0) {
                    F += d2y;
                } else {
                    y += stepy;
                    F += dxdy;
                }
            }

        } else {
            if (p0.y < p1.y) {
                x0 = p0.x;
                y0 = p0.y;
                x1 = p1.x;
                y1 = p1.y;
            } else {
                x0 = p1.x;
                y0 = p1.y;
                x1 = p0.x;
                y1 = p0.y;
            }
            int stepx = 1;
            if ((x1 - x0) < 0) {
                stepx = -1;
            }
            int F    = 2 * dx - dy;
            int d2x  = 2 * dx;
            int dydx = 2 * (dx - dy);

            int x = x0;
            for (int y = y0; y <= y1; y++) {
                canvas.At(x, y) = color;
                if (F < 0) {
                    F += d2x;
                } else {
                    x += stepx;
                    F += dydx;
                }
            }
        }
    }

    /******************* 5. Triangle Drawing *****************/
    void DrawTriangleFilled(
        ImageRGB &       canvas,
        glm::vec3 const  color,
        glm::ivec2 const p0,
        glm::ivec2 const p1,
        glm::ivec2 const p2) {
        // your code here:
        struct Edge {
            float m;
            int   maxy;
            int   miny;
            float x;

            Edge(glm::ivec2 a, glm::ivec2 b) {
                if (a.y > b.y) std::swap(a, b);
                miny = a.y;
                maxy = b.y;
                x    = a.x;

                if (b.y == maxy) maxy--;

                if (a.y != b.y) {
                    m = static_cast<float>(b.x - a.x) / (b.y - a.y);
                } else {
                    m = 0;
                }
            }
        };
        std::vector<Edge> edges;
        edges.push_back(Edge(p0, p1));
        edges.push_back(Edge(p0, p2));
        edges.push_back(Edge(p1, p2));
        edges.erase(std::remove_if(edges.begin(), edges.end(), [](const Edge e) {
                        if (e.maxy == e.miny) {
                            return true;
                        } else {
                            return false;
                        }
                    }),
                    edges.end());

        if (edges.size() < 2) return;
        std::sort(edges.begin(), edges.end(), [](const Edge a, const Edge b) {
            return a.miny > b.miny;
        });
        std::vector<Edge> active_edges;
        int               ymin = edges.back().miny;
        int               y    = ymin;
        while (! edges.empty() && edges.back().miny == ymin) {
            active_edges.push_back(edges.back());
            edges.pop_back();
        }
        std::vector<float> l;
        while (! active_edges.empty()) {
            for (Edge & e : active_edges) {
                l.push_back(e.x);
                e.x += e.m;
            }
            std::sort(l.begin(), l.end());
            for (size_t i = 0; i < l.size(); i += 2) {
                if (i + 1 >= l.size()) break; // 防止奇数个交点
                int a = static_cast<int>(std::round(l[i]));
                int b = static_cast<int>(std::round(l[i + 1]));
                for (int x = a; x <= b; x++) {
                    canvas.At(x, y) = color;
                }
            }
            l.clear();

            active_edges.erase(
                std::remove_if(active_edges.begin(), active_edges.end(), [y](const Edge e) {
                    return y >= e.maxy; // 删除已经到达最大 y 的边
                }),
                active_edges.end());
            y++;
            while (! edges.empty() && edges.back().miny == y) {
                active_edges.push_back(edges.back());
                edges.pop_back();
            }
        }
    }

    /******************* 6. Image Supersampling *****************/
    void Supersample(
        ImageRGB &       output,
        ImageRGB const & input,
        int              rate) {
        // your code here:
        std::size_t iwidth  = input.GetSizeX();
        std::size_t iheight = input.GetSizeY();
        std::size_t owidth  = output.GetSizeX();
        std::size_t oheight = output.GetSizeY();
        float       stepX   = static_cast<float>(iwidth) / owidth;
        float       stepY   = static_cast<float>(iheight) / oheight;

        for (int x = 0; x < owidth; x++) {
            for (int y = 0; y < oheight; y++) {
                glm::vec3 sum(0.0f, 0.0f, 0.0f);

                for (int i = 0; i < rate; i++) {
                    for (int j = 0; j < rate; j++) {
                        float ix = x * stepX + (i + 0.5f) * (stepX / rate);
                        float iy = y * stepY + (j + 0.5f) * (stepY / rate);
                        int   x0 = static_cast<int>(std::floor(ix));
                        int   y0 = static_cast<int>(std::floor(iy));
                        int   x1 = x0 + 1;
                        int   y1 = y0 + 1;

                        // 边界处理
                        x0 = std::clamp(x0, 0, static_cast<int>(iwidth) - 1);
                        x1 = std::clamp(x1, 0, static_cast<int>(iwidth) - 1);
                        y0 = std::clamp(y0, 0, static_cast<int>(iheight)- 1);
                        y1 = std::clamp(y1, 0, static_cast<int>(iheight)- 1);

                        float dx = ix - x0;
                        float dy = iy - y0;
                        float omdx = 1.0f - dx;
                        float omdy = 1.0f - dy;

                        // 获取四个角点的颜色
                        const glm::vec3 & c00 = input.At(x0,y0);
                        const glm::vec3 & c10 = input.At(x1,y0);
                        const glm::vec3 & c01 = input.At(x0,y1);
                        const glm::vec3 & c11 = input.At(x1,y1);
                        
                        sum+=(c00 * omdx + c10 * dx) * omdy + (c01 * omdx + c11 * dx) * dy;
                    }
                }
                sum/=static_cast<float>(rate*rate);
                output.At(x,y)=sum;
            }
        }
    }

    /******************* 7. Bezier Curve *****************/
    // Note: Please finish the function [DrawLine] before trying this part.
    glm::vec2 CalculateBezierPoint(
        std::span<glm::vec2> points,
        float const          t) {
        // your code here:
        std::vector<glm::vec2> Points(points.begin(), points.end());
        size_t                 n = Points.size() - 1;
        for (size_t r = 0; r < n; r++) {
            for (size_t i = 0; i < n - r; i++) {
                Points[i] = glm::mix(Points[i], Points[i + 1], t);
            }
        }

        return Points[0];
    }
} // namespace VCX::Labs::Drawing2D