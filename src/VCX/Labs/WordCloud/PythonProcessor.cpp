#include <filesystem>
#include <cstdio>
#include <iostream>
#include <windows.h>
#include <codecvt>
#include <nlohmann/json.hpp>
#include "Labs/WordCloud/PythonProcessor.h"

namespace VCX::Labs::labf {

    static std::filesystem::path GetExeDir() {
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
    }

    std::string PythonProcessor::GetPythonPath() {
        std::filesystem::path exeDir = GetExeDir();
#if defined(_WIN32)
        return (exeDir / "assets/scripts/.venv/Scripts/python.exe").string();
#else
        return (exeDir / "assets/scripts/.venv/bin/python").string();
#endif
    }

    std::string PythonProcessor::GetScriptPath() {
        std::filesystem::path exeDir = GetExeDir();
        return (exeDir / "assets/scripts/word_processor.py").string();
    }

    bool PythonProcessor::IsPythonAvailable() {
        std::filesystem::path pythonPath = GetPythonPath();
        return std::filesystem::exists(pythonPath) && std::filesystem::is_regular_file(pythonPath);
    }

    std::vector<WordResult> PythonProcessor::ProcessMarkdown(std::string const& filePath) {
        std::vector<WordResult> results;

        std::filesystem::path pythonPath = GetPythonPath();
        std::filesystem::path scriptPath = GetScriptPath();
        std::filesystem::path targetPath = std::filesystem::u8path(filePath);

        std::wstring wCommand = L"\"\"" + pythonPath.wstring() + L"\" \"" +
                                scriptPath.wstring() + L"\" \"" +
                                targetPath.wstring() + L"\"";

        FILE* pipe = _wpopen(wCommand.c_str(), L"r");
        if (!pipe) {
            return results;
        }

        std::string output;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        _pclose(pipe);

        try {
            auto json = nlohmann::json::parse(output);
            for (auto const& item : json) {
                WordResult result;
                result.text = item["text"].get<std::string>();
                result.weight = item["weight"].get<float>();
                results.push_back(result);
            }
        } catch (nlohmann::json::parse_error const& e) {
            (void)e;
        }

        return results;
    }

    std::vector<WordResult> PythonProcessor::ProcessMarkdownBatch(
        std::vector<std::string> const& filePaths,
        int topK
    ) {
        std::vector<WordResult> results;
        if (filePaths.empty()) {
            return results;
        }

        std::filesystem::path pythonPath = GetPythonPath();
        std::filesystem::path scriptPath = GetScriptPath();

        // 检查文件存在性
        if (!std::filesystem::exists(pythonPath) || !std::filesystem::exists(scriptPath)) {
            return results;
        }

        // 构造命令：python script.py --top_k N file1 file2 ...
        std::wstring wCommand = L"\"\"" + pythonPath.wstring() + L"\" \"" +
                                scriptPath.wstring() + L"\" --top_k " +
                                std::to_wstring(topK);

        for (auto const& filePath : filePaths) {
            std::filesystem::path targetPath = std::filesystem::u8path(filePath);
            wCommand += L" \"" + targetPath.wstring() + L"\"";
        }
        wCommand += L"\"";

        FILE* pipe = _wpopen(wCommand.c_str(), L"r");
        if (!pipe) {
            return results;
        }

        std::string output;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        _pclose(pipe);

        if (output.empty()) {
            return results;
        }

        try {
            auto json = nlohmann::json::parse(output);
            for (auto const& item : json) {
                WordResult result;
                result.text = item["text"].get<std::string>();
                result.weight = item["weight"].get<float>();
                results.push_back(result);
            }
        } catch (nlohmann::json::parse_error const& e) {
            (void)e;
        }

        return results;
    }
}
