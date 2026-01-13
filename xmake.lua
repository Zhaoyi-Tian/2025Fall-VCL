set_project("VCX-Labs")
set_version("2.0.0")
set_xmakever("2.6.9")
set_languages("cxx20")

add_rules("mode.debug", "mode.release", "mode.profile")
if is_plat("windows") then
    add_cxxflags("/utf-8")
else
    add_cxxflags("-finput-charset=UTF-8")
    add_cxxflags("-fexec-charset=UTF-8") 
    add_cxxflags("-DGLFW_INCLUDE_NONE")
    add_cxxflags("-DIMGUI_ENABLE_FREETYPE")
end

add_requires("glad")
add_requires("glfw")
add_requires("glm 1.0.0")
add_requires("imgui 1.90.1")
add_requires("spdlog")
add_requires("stb")
add_requires("fmt")
add_requires("tinyobjloader")
add_requires("yaml-cpp")
add_requires("eigen")
add_requires("freetype")
add_requires("tinyxml2")
add_requires("nlohmann_json")

if is_plat("macosx") then
    add_defines("PLATFORM_MACOSX")
end

target("assets")
    set_kind("phony")
    set_default(true)
    after_build(function (target)
        os.mkdir(path.join(target:targetdir(), "assets"))
        os.cp("assets/*|shaders", path.join(target:targetdir(), "assets"))
        os.mkdir(path.join(target:targetdir(), "assets", "shaders"))
        os.cp("assets/shaders/*", path.join(target:targetdir(), "assets", "shaders"))
    end)
    after_install(function (target)
        os.mkdir(path.join(target:installdir(), "assets"))
        os.cp("assets/*|shaders", path.join(target:installdir(), "assets"))
        os.mkdir(path.join(target:installdir(), "assets", "shaders"))
        os.cp("assets/shaders/*", path.join(target:installdir(), "assets", "shaders"))
    end)
    after_clean(function (target)
        os.rm(path.join(target:targetdir(), "assets"))
    end)

-- msdfgen 库（用于 MSDF 文本渲染）
target("msdfgen")
    set_kind("static")
    add_packages("freetype", { public = true })
    add_packages("tinyxml2")
    -- 添加 include 路径：
    -- 1. msdf-atlas-gen 根目录（用于 #include <msdfgen/xxx.h> 形式）
    -- 2. 其他必要的子目录
    add_includedirs(
        "src/3rdparty/msdf-atlas-gen",
        "src/3rdparty/msdf-atlas-gen/msdfgen",
        "src/3rdparty/msdf-atlas-gen/msdf-atlas-gen",
        "src/3rdparty/msdf-atlas-gen/artery-font-format",
        { public = true }
    )
    add_files(
        "src/3rdparty/msdf-atlas-gen/msdfgen/core/*.cpp",
        "src/3rdparty/msdf-atlas-gen/msdfgen/ext/*.cpp",
        "src/3rdparty/msdf-atlas-gen/msdf-atlas-gen/*.cpp"
    )
    -- 排除 main.cpp（命令行工具入口）
    remove_files("src/3rdparty/msdf-atlas-gen/msdf-atlas-gen/main.cpp")
    set_languages("cxx20")

target("engine")
    set_kind("static")
    add_packages("glad"         , { public = true })
    add_packages("glfw"         , { public = true })
    add_packages("glm"          , { public = true })
    add_packages("imgui"        , { public = true })
    add_packages("spdlog"       , { public = true })
    add_packages("stb"          , { public = true })
    add_packages("fmt"          , { public = true })
    add_packages("tinyobjloader", { public = true })
    add_packages("yaml-cpp"     , { public = true })
    add_packages("nlohmann_json", { public = true })

    add_includedirs("src/3rdparty", { public = true })
    add_includedirs("src/VCX"     , { public = true })
    -- 排除 msdf-atlas-gen 目录（由 msdfgen target 单独管理）
    add_headerfiles("src/3rdparty/**.h|msdf-atlas-gen/**")
    add_headerfiles("src/3rdparty/**.hpp|msdf-atlas-gen/**")
    add_files      ("src/3rdparty/**.cpp|msdf-atlas-gen/**")
    add_headerfiles("src/VCX/Assets/**.h")
    add_headerfiles("src/VCX/Assets/**.hpp")
    add_headerfiles("src/VCX/Engine/**.h")
    add_headerfiles("src/VCX/Engine/**.hpp")
    add_files      ("src/VCX/Engine/**.cpp")

target("lab-common")
    set_kind("static")
    add_deps("engine")
    add_deps("assets")
    add_headerfiles("src/VCX/Labs/Common/*.h")
    add_files      ("src/VCX/Labs/Common/*.cpp")

target("labf")
    set_kind("binary")
    add_deps("lab-common")
    add_deps("msdfgen")
    add_headerfiles("src/VCX/Labs/WordCloud/*.h")
    add_headerfiles("src/VCX/Labs/WordCloud/*.hpp")
    add_files      ("src/VCX/Labs/WordCloud/*.cpp")
    if is_plat("windows") then
        add_syslinks("comdlg32")  -- GetOpenFileNameW
    end
    after_build(function (target)
        os.cp("src/VCX/Labs/WordCloud/shaders/*", path.join(target:targetdir(), "assets", "shaders"))
        -- 使用 . 复制整个 scripts 目录（包括 .venv 子目录）
        os.cp("src/VCX/Labs/WordCloud/scripts/.", path.join(target:targetdir(), "assets", "scripts"))
    end)
    after_install(function (target)
        os.cp("src/VCX/Labs/WordCloud/shaders/*", path.join(target:installdir(), "bin", "assets", "shaders"))
        os.cp("src/VCX/Labs/WordCloud/scripts/.", path.join(target:installdir(), "bin", "assets", "scripts"))
    end)