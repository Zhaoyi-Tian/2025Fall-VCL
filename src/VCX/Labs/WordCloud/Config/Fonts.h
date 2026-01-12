#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <unordered_set>

namespace VCX::Labs::labf {
    // 词云字体配置目录
    inline constexpr std::string_view WordCloudFontDir = "assets/fonts_wordcloud";

    // 字体信息结构
    struct WordCloudFontInfo {
        std::string path;       // 字体文件路径
        std::string name;       // 显示名称
    };

    // 自动扫描字体目录，返回可用字体列表（按名称去重）
    inline std::vector<WordCloudFontInfo> ScanWordCloudFonts() {
        std::vector<WordCloudFontInfo> fonts;
        std::unordered_set<std::string> seenNames;  // 用于去重

        try {
            std::filesystem::path dir(WordCloudFontDir);
            if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
                return fonts;
            }

            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;

                std::string path = entry.path().string();
                std::string ext  = entry.path().extension().string();

                // 支持 .ttf, .otf, .ttc
                if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;

                // 从文件名提取名称
                std::string name = entry.path().stem().string();

                // 清理TTC索引后缀
                auto pos = name.find(".ttc#");
                if (pos != std::string_view::npos) {
                    name = name.substr(0, pos);
                }

                // 替换下划线为空格，使显示更美观
                for (char& c : name) {
                    if (c == '_') c = ' ';
                }

                // 按名称去重，只保留第一个
                if (seenNames.find(name) == seenNames.end()) {
                    seenNames.insert(name);
                    fonts.push_back({ path, name });
                }
            }
        } catch (const std::exception&) {
            // 扫描失败时返回空列表
        }

        return fonts;
    }

    // 全局字体列表（静态初始化时扫描）
    inline const std::vector<WordCloudFontInfo>& GetWordCloudFonts() {
        static const auto fonts = ScanWordCloudFonts();
        return fonts;
    }

    // 默认选中索引
    inline constexpr std::size_t DefaultWordCloudFontIndex = 0;
}
