#pragma once

#include <vector>
#include <optional>
#include <cmath>

#include "Labs/WordCloud/WordEntity.h"

namespace VCX::Labs::labf {

    /**
     * 管理词云中的多个 WordEntity，提供增删改查与批量更新接口。
     * 暂不实现加速结构（如四叉树/网格），但预留扩展空间。
     */
    class WordManager {
    public:
        // 添加一个词并返回引用
        WordEntity & add(std::string const & text, float fontSize);

        // 删除指定索引的词
        bool remove(std::size_t idx);

        // 清空
        void clear();

        // 访问所有词
        std::vector<WordEntity>       & items()       { return _words; }
        std::vector<WordEntity> const & items() const { return _words; }

        // 简易命中测试（使用未旋转 AABB 近似）
        std::optional<std::size_t> hitTest(glm::vec2 const & pt) const;

        // 获取两个词之间的语义相似度（对称矩阵）
        float getSimilarity(std::size_t i, std::size_t j) const;

        // 重新计算相似度矩阵（当词向量更新时调用）
        void recomputeSimilarityMatrix();

    private:
        std::vector<WordEntity> _words;

        // 相似度矩阵（上三角存储，n*(n-1)/2 个元素）
        // 索引 (i,j) 其中 i < j 映射到 index = i*n - i*(i+1)/2 + (j-i-1)
        std::vector<float> _similarityMatrix;
        std::size_t _matrixSize = 0;  // 记录矩阵对应的词数量

        // 计算两个向量的余弦相似度
        static float cosineSimilarity(std::vector<float> const& a, std::vector<float> const& b);

        // 获取上三角矩阵索引
        std::size_t getMatrixIndex(std::size_t i, std::size_t j) const;
    };

} // namespace VCX::Labs::labf
