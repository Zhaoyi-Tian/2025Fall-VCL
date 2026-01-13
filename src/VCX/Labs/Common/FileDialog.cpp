#include "Labs/Common/FileDialog.h"
#include <windows.h>
#include <commdlg.h>
#include <cstring>

namespace VCX::Labs::Common {

    static std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring wstr(size - 1, 0);  // 减1去掉末尾的null
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
        return wstr;
    }

    static std::string WStringToString(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string str(size - 1, 0);  // 减1去掉末尾的null
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
        return str;
    }

    std::optional<std::string> FileDialog::SelectFile(
        char const* title,
        std::vector<std::pair<char const*, char const*>> const& extensions
    ) {
        (void)title; // Windows 对话框标题有长度限制，暂不使用

        OPENFILENAMEW ofn = {};
        wchar_t filePath[MAX_PATH] = L"";

        // 构建过滤器字符串
        wchar_t filter[MAX_PATH * 8] = L"";
        size_t filterPos = 0;

        for (size_t i = 0; i < extensions.size() && i < 6; ++i) {
            std::wstring name = StringToWString(extensions[i].first);
            std::wstring spec = StringToWString(extensions[i].second);

            // 复制名称
            wcscpy_s(filter + filterPos, MAX_PATH, name.c_str());
            filterPos += name.size() + 1;

            // 复制扩展名
            wcscpy_s(filter + filterPos, MAX_PATH, spec.c_str());
            filterPos += spec.size() + 1;
        }

        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = NULL;  // 可以关联到 ImGui 窗口
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameW(&ofn)) {
            return WStringToString(filePath);
        }
        return std::nullopt;
    }

    std::optional<std::vector<std::string>> FileDialog::SelectFiles(
        char const* title,
        std::vector<std::pair<char const*, char const*>> const& extensions
    ) {
        (void)title;

        OPENFILENAMEW ofn = {};
        // 多选需要更大的缓冲区
        std::vector<wchar_t> filePath(65535, 0);

        // 构建过滤器字符串
        wchar_t filter[MAX_PATH * 8] = L"";
        size_t filterPos = 0;

        for (size_t i = 0; i < extensions.size() && i < 6; ++i) {
            std::wstring name = StringToWString(extensions[i].first);
            std::wstring spec = StringToWString(extensions[i].second);

            wcscpy_s(filter + filterPos, MAX_PATH, name.c_str());
            filterPos += name.size() + 1;

            wcscpy_s(filter + filterPos, MAX_PATH, spec.c_str());
            filterPos += spec.size() + 1;
        }

        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = NULL;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = filePath.data();
        ofn.nMaxFile = static_cast<DWORD>(filePath.size());
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR
                  | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

        if (GetOpenFileNameW(&ofn)) {
            std::vector<std::string> results;
            const wchar_t* p = filePath.data();
            std::wstring directory = p;
            p += directory.size() + 1;

            if (*p == L'\0') {
                // 只选了一个文件，directory 就是完整路径
                results.push_back(WStringToString(directory));
            } else {
                // 多个文件：目录\0文件1\0文件2\0\0
                while (*p != L'\0') {
                    std::wstring fileName = p;
                    std::wstring fullPath = directory + L"\\" + fileName;
                    results.push_back(WStringToString(fullPath));
                    p += fileName.size() + 1;
                }
            }
            return results;
        }
        return std::nullopt;
    }
}
