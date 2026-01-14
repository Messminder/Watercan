// Minimal WinMain wrapper that forwards to standard main(argc, argv)
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>

// forward declaration of real main
int main(int argc, char** argv);

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvW) return 0;

    std::vector<std::string> utf8_storage;
    utf8_storage.reserve(argc);
    std::vector<char*> argv;
    argv.reserve(argc + 1);

    for (int i = 0; i < argc; ++i) {
        // Determine required buffer size (including null)
        int size = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) {
            utf8_storage.emplace_back("");
            argv.push_back(const_cast<char*>(utf8_storage.back().c_str()));
            continue;
        }
        utf8_storage.emplace_back();
        utf8_storage.back().resize(size);
        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, &utf8_storage.back()[0], size, nullptr, nullptr);
        argv.push_back(&utf8_storage.back()[0]);
    }
    argv.push_back(nullptr);

    int result = main(argc, argv.data());

    LocalFree(argvW);
    return result;
}
#endif
