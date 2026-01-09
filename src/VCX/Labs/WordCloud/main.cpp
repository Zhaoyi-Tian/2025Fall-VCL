#include "Assets/bundled.h"
#include "Labs/WordCloud/App.h"

// 强制使用 NVIDIA 独立显卡（针对 Optimus 双显卡笔记本）
#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main() {

    using namespace VCX;
    return Engine::RunApp<Labs::labf::App>(Engine::AppContextOptions {
        .Title      = "VCX Labs: Word Cloud",
        .WindowSize = { 1600, 960 },
        .FontSize   = 17,

        .IconFileNames = Assets::DefaultIcons,
        .FontFileNames = Assets::DefaultFonts,
    });
}
