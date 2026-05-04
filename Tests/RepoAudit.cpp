#include <filesystem>
#include <iostream>

int main()
{
    namespace fs = std::filesystem;
    const char* required[] = {
        "CMakeLists.txt",
        "README.md",
        "Source/PluginProcessor.cpp",
        "Source/PluginProcessor.h",
        "Source/PluginEditor.cpp",
        "Source/PluginEditor.h",
        "Source/DSP/FreeVox8DSP.h",
        "docs/PRODUCTION_AUDIT.md"
    };

    bool ok = true;
    for (auto* path : required)
    {
        if (!fs::exists(path))
        {
            std::cerr << "Missing: " << path << "\n";
            ok = false;
        }
    }
    if (!ok) return 1;
    std::cout << "FreeVox8 repo audit passed.\n";
    return 0;
}
