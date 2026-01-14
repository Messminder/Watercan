#include "app.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <cstring>

namespace Watercan {

bool App::saveTypeColorsToDisk() {
    try {
        std::filesystem::path configDir;
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && std::strlen(xdg) > 0) configDir = xdg;
        else {
            const char* home = std::getenv("HOME");
            if (!home) return false;
            configDir = std::filesystem::path(home) / ".config";
        }
        configDir /= "watercan";
        std::filesystem::create_directories(configDir);
        std::filesystem::path file = configDir / "type_colors.json";

        nlohmann::json j;
        for (const auto &p : m_typeColors) {
            const auto &arr = p.second;
            j[p.first] = { arr[0], arr[1], arr[2], arr[3] };
        }

        std::ofstream ofs(file);
        if (!ofs.is_open()) return false;
        ofs << j.dump(4);
        ofs.close();
        return true;
    } catch (...) {
        return false;
    }
}

bool App::loadTypeColorsFromDisk() {
    try {
        std::filesystem::path configDir;
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && std::strlen(xdg) > 0) configDir = xdg;
        else {
            const char* home = std::getenv("HOME");
            if (!home) return false;
            configDir = std::filesystem::path(home) / ".config";
        }
        configDir /= "watercan";
        std::filesystem::path file = configDir / "type_colors.json";
        if (!std::filesystem::exists(file)) return false;

        std::ifstream ifs(file);
        if (!ifs.is_open()) return false;
        nlohmann::json j;
        ifs >> j;
        ifs.close();

        for (auto it = j.begin(); it != j.end(); ++it) {
            auto val = it.value();
            if (val.is_array() && val.size() >= 4) {
                std::array<float,4> arr;
                arr[0] = val[0].get<float>();
                arr[1] = val[1].get<float>();
                arr[2] = val[2].get<float>();
                arr[3] = val[3].get<float>();
                m_typeColors[it.key()] = arr;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace Watercan