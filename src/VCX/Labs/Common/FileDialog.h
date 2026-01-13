#pragma once

#include <string>
#include <optional>
#include <vector>

// 使用 Windows 原生文件对话框 (GetOpenFileNameW)
// 避免外部依赖
namespace VCX::Labs::Common {

    class FileDialog {
    public:
        // 打开文件选择对话框
        // extensions: 例如 { {"Markdown", "*.md"}, {"All Files", "*.*"} }
        static std::optional<std::string> SelectFile(
            char const* title = "Open File",
            std::vector<std::pair<char const*, char const*>> const& extensions = {}
        );

        // 多选文件对话框
        static std::optional<std::vector<std::string>> SelectFiles(
            char const* title = "Open Files",
            std::vector<std::pair<char const*, char const*>> const& extensions = {}
        );
    };
}
