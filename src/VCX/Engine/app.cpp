#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>
#include <cctype>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "Engine/app.h"

static GLFWwindow *                      g_glfwWindow;
static std::function<void()>             g_glfwWindowRefreshCallback;
static std::uint32_t                     g_ImGuiDefaultFontSize;
static std::span<std::string_view const> g_ImGuiDefaultFontFileNames;

GLFWwindow *                      glfwGetCurrentWindow() { return g_glfwWindow; }
std::uint32_t                     ImGuiGetDefaultFontSize() { return g_ImGuiDefaultFontSize; }
std::span<std::string_view const> ImGuiGetDefaultFontFileNames() { return g_ImGuiDefaultFontFileNames; }

namespace VCX::Engine {
    static std::pair<std::uint32_t, std::uint32_t> g_WindowSize;
    static std::pair<std::uint32_t, std::uint32_t> g_FrameSize;

    static decltype(glfwGetTime())                 g_LastTime        = 0;
    static float                                   g_DeltaTime       = 0;

    static std::uint32_t                           g_FramesCnt       = 0;
    static decltype(glfwGetTime())                 g_LastTotTime     = 0;
    static float                                   g_FramesPerSecond = 0;
    
    float                                   GetDeltaTime() { return g_DeltaTime; }
    float                                   GetFramesPerSecond() { return g_FramesPerSecond; }
    std::pair<std::uint32_t, std::uint32_t> GetCurrentWindowSize() { return g_WindowSize; }
    std::pair<std::uint32_t, std::uint32_t> GetCurrentFrameSize() { return g_FrameSize; }

    // Helper: parse TTC face index from path.
    // Accepts patterns like:
    //  - ".../font.ttc#7"
    //  - ".../font.ttc#7.ttf" (compat with existing path style)
    // Returns pair {cleanPath, faceIndex}
    static std::pair<std::string, int> ParseTtcFace(std::string_view path) {
        std::string p(path);
        int face = 0;
        auto pos = p.rfind(".ttc#");
        if (pos != std::string::npos) {
            auto tail = p.substr(pos + 5); // after ".ttc#"
            // strip optional trailing ".ttf"
            if (tail.size() >= 4 && tail.compare(tail.size()-4, 4, ".ttf") == 0) {
                tail.erase(tail.size()-4);
            }
            // parse integer
            face = std::atoi(tail.c_str());
            p = p.substr(0, pos + 4); // keep up to ".ttc"
        }
        return { std::move(p), face };
    }

    // Heuristics to decide if a font likely contains CJK glyphs.
    static bool IsLikelyCJKFont(std::string const & path) {
        auto lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        return (lower.find(".ttc") != std::string::npos) ||
               (lower.find("cjk") != std::string::npos) ||
               (lower.find("noto") != std::string::npos && lower.find("sans") != std::string::npos && (lower.find("cj") != std::string::npos));
    }

    // Heuristics to detect monospace fonts.
    static bool IsLikelyMonoFont(std::string const & path) {
        auto lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        return lower.find("mono") != std::string::npos;
    }

    void ImGuiBuildFonts(std::span<std::string_view const> fontPaths, float fontSize) {
        ImGuiIO &io = ImGui::GetIO();

        // Partition fonts: latin base, cjk list (merge), mono list (added separately)
        std::string                 baseLatinPath;
        int                         baseLatinFace = 0;
        std::vector<std::pair<std::string,int>> cjkFonts;
        std::vector<std::pair<std::string,int>> monoFonts;
        std::vector<std::pair<std::string,int>> otherFonts;

        for (auto const &sv : fontPaths) {
            std::string path(sv);
            auto [clean, face] = ParseTtcFace(path);
            if (IsLikelyMonoFont(clean)) {
                monoFonts.emplace_back(std::move(clean), face);
            } else if (IsLikelyCJKFont(clean)) {
                cjkFonts.emplace_back(std::move(clean), face);
            } else {
                if (baseLatinPath.empty()) { baseLatinPath = clean; baseLatinFace = face; }
                else                       { otherFonts.emplace_back(std::move(clean), face); }
            }
        }

        // If no explicit latin base found, fall back to first non-mono font as base
        if (baseLatinPath.empty()) {
            if (!otherFonts.empty()) {
                baseLatinPath = otherFonts.front().first;
                baseLatinFace = otherFonts.front().second;
                otherFonts.erase(otherFonts.begin());
            } else if (!cjkFonts.empty()) {
                baseLatinPath = cjkFonts.front().first;
                baseLatinFace = cjkFonts.front().second;
                cjkFonts.erase(cjkFonts.begin());
            } else if (!monoFonts.empty()) {
                baseLatinPath = monoFonts.front().first;
                baseLatinFace = monoFonts.front().second;
                monoFonts.erase(monoFonts.begin());
            }
        }

        // Build fonts: base latin
        if (!baseLatinPath.empty()) {
            ImFontConfig cfg{};
            cfg.MergeMode = false;
            cfg.FontNo    = baseLatinFace;
            io.Fonts->AddFontFromFileTTF(baseLatinPath.c_str(), fontSize, &cfg, io.Fonts->GetGlyphRangesDefault());
        }

        // Merge CJK ranges into base to support Chinese
        for (auto const &[p, face] : cjkFonts) {
            ImFontConfig cfg{};
            cfg.MergeMode = true;
            cfg.FontNo    = face;
            io.Fonts->AddFontFromFileTTF(p.c_str(), fontSize, &cfg, io.Fonts->GetGlyphRangesChineseFull());
        }

        // Add mono fonts as standalone (index becomes Fonts[1] if one exists)
        for (auto const &[p, face] : monoFonts) {
            ImFontConfig cfg{};
            cfg.MergeMode = false;
            cfg.FontNo    = face;
            io.Fonts->AddFontFromFileTTF(p.c_str(), fontSize, &cfg, io.Fonts->GetGlyphRangesDefault());
        }

        // Any remaining fonts: add as standalone with default ranges
        for (auto const &[p, face] : otherFonts) {
            ImFontConfig cfg{};
            cfg.MergeMode = false;
            cfg.FontNo    = face;
            io.Fonts->AddFontFromFileTTF(p.c_str(), fontSize, &cfg, io.Fonts->GetGlyphRangesDefault());
        }
    }
}

namespace VCX::Engine::Internal {
    static void glfwErrorCallback(int const error, char const * const description) {
        spdlog::error("GLFW Error {}: {}", error, description);
        std::exit(EXIT_FAILURE);
    }

    static void glfwWindowRefreshCallback(GLFWwindow * const _) {
        g_glfwWindowRefreshCallback();
    }

    static void glfwWindowSizeCallback(GLFWwindow * const _, int const width, int const height) {
        assert(width >= 0 && height >= 0);
        g_WindowSize = { width, height };
    }

    static void glfwFramebufferSizeCallback(GLFWwindow * const _, int const width, int const height) {
        assert(width >= 0 && height >= 0);
        g_FrameSize = { width, height };
    }

    static void RunApp_InitGLFW(AppContextOptions const &);
    static void RunApp_InitGLFWWindowIcons(AppContextOptions const &);
    static void RunApp_InitGLFWWindowCallbacks(IApp &);
    static void RunApp_InitGLAD();
    static void RunApp_InitImGui(AppContextOptions const &);
    static void RunApp_Frame(IApp &);

    void RunApp_Init(AppContextOptions const & options) {
        RunApp_InitGLFW(options);
        #ifndef PLATFORM_MACOSX
            RunApp_InitGLFWWindowIcons(options);
        #endif
        RunApp_InitGLAD();
        RunApp_InitImGui(options);
    }

    void RunApp_Main(IApp && app) {
        RunApp_InitGLFWWindowCallbacks(app);
        glfwShowWindow(g_glfwWindow);
        while (! glfwWindowShouldClose(g_glfwWindow)) {
            RunApp_Frame(app);
            glfwPollEvents(); 
        }
    }

    void RunApp_Shutdown() {
        ImGui_ImplGlfw_Shutdown();
        ImGui_ImplOpenGL3_Shutdown();
        glfwTerminate();
    }

    static void RunApp_InitGLFW(AppContextOptions const & options) {
        glfwSetErrorCallback(glfwErrorCallback);
        if (glfwInit()) {
            spdlog::trace("GLFW: glfwInit()");
        } else {
            spdlog::error("GLFW: glfwInit() failed.");
            exit(EXIT_FAILURE);
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // for macos
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, 4);  // 4x MSAA for anti-aliasing

        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        assert(*(options.Title.cend()) == '\0');
        g_glfwWindow = glfwCreateWindow(
            options.WindowSize.first,
            options.WindowSize.second,
            options.Title.data(),
            nullptr,
            nullptr);
        if (g_glfwWindow) {
            spdlog::trace("GLFW: glfwCreateWindow(..)");
        } else {
            spdlog::error("GLFW: glfwCreateWindow(..) failed.");
            glfwTerminate();
            exit(EXIT_FAILURE);
        }

        int width;
        int height;
        glfwGetWindowSize(g_glfwWindow, &width, &height);
        g_WindowSize = { width, height };
        glfwGetFramebufferSize(g_glfwWindow, &width, &height);
        g_FrameSize = { width, height };

        glfwSetWindowRefreshCallback(g_glfwWindow, glfwWindowRefreshCallback);
        glfwSetWindowSizeCallback(g_glfwWindow, glfwWindowSizeCallback);
        glfwSetFramebufferSizeCallback(g_glfwWindow, glfwFramebufferSizeCallback);

        glfwMakeContextCurrent(g_glfwWindow);
        glfwSwapInterval(1);
    }

    static void RunApp_InitGLFWWindowIcons(AppContextOptions const & options) {
        auto const             iconsCount { options.IconFileNames.size() };
        std::vector<GLFWimage> icons;
        icons.resize(iconsCount);
        for (std::size_t i = 0; i < iconsCount; ++i) {
            auto const & iconFileName { options.IconFileNames[i] };
            auto &       icon { icons[i] };
            assert(*(iconFileName.cend()) == '\0');
            icon.pixels = stbi_load(iconFileName.data(), &icon.width, &icon.height, nullptr, 4);
        }
        glfwSetWindowIcon(
            g_glfwWindow,
            icons.size(),
            icons.data());
        for (auto const & icon : icons) {
            stbi_image_free(icon.pixels);
        }
    }

    static void RunApp_InitGLAD() {
        if (gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
            spdlog::trace("GLAD: gladLoadGLLoader(..)");
        } else {
            spdlog::error("GLAD: gladLoadGLLoader(..) failed.");
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
    }

    static void RunApp_InitImGui(AppContextOptions const & options) {
        ImGui::CreateContext();
        ImGuiBuildFonts(options.FontFileNames, options.FontSize);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        ImGui::GetIO().MouseDragThreshold = 1.0f;  // 降低拖动阈值（默认是 6.0f）

        ImGui_ImplOpenGL3_Init();
        ImGui_ImplGlfw_InitForOpenGL(g_glfwWindow, true);

        g_ImGuiDefaultFontSize      = options.FontSize;
        g_ImGuiDefaultFontFileNames = options.FontFileNames;
    }

    static void RunApp_InitGLFWWindowCallbacks(IApp & app) {
        g_glfwWindowRefreshCallback = [&app]() { RunApp_Frame(app); };
    }

    static void RunApp_Frame(IApp & app) {
        auto const currentTime = glfwGetTime();
        g_DeltaTime = currentTime - g_LastTime;
        g_LastTime  = currentTime;
        if (g_FramesCnt++; currentTime - g_LastTotTime >= 1.) {
            g_FramesPerSecond = float(g_FramesCnt / (currentTime - g_LastTotTime));
            g_FramesCnt = 0;
            g_LastTotTime = currentTime;
        }

        glViewport(0, 0, g_FrameSize.first, g_FrameSize.second);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.OnFrame();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(g_glfwWindow);
    }
} // namespace VCX::Engine::Internal
