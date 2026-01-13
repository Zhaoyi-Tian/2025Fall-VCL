#pragma once

#include <string>
#include <vector>

namespace VCX::Labs::labf {

    struct WordResult {
        std::string text;
        float weight;
    };

    class PythonProcessor {
    public:
        // 异步处理 md 文件，返回词频列表
        static std::vector<WordResult> ProcessMarkdown(std::string const& filePath);

        // 批量处理多个 md 文件（合并后统计词频）
        static std::vector<WordResult> ProcessMarkdownBatch(
            std::vector<std::string> const& filePaths,
            int topK = 100
        );

        // 检查 Python 是否可用
        static bool IsPythonAvailable();

        // 获取虚拟环境 Python 路径
        static std::string GetPythonPath();

        // 获取脚本路径
        static std::string GetScriptPath();
    };
}
