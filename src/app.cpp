#include "app.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstring>
#include <vector>
#include <cmath>
#include "embedded_resources.h"
#include <deque>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <limits>
#include <sstream>
#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

namespace Watercan {

// Version information
constexpr const char* WATERCAN_VERSION = "1.6";

// Format seconds as "M:SS"
static std::string secsToStr(double s) {
    int si = (int)std::floor(s + 0.5);
    int min = si / 60;
    int sec = si % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d:%02d", min, sec);
    return std::string(buf);
}

// Error callback for GLFW
static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

App::App() = default;

App::~App() {
    shutdown();
}

bool App::init() {
    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        return false;
    }
    
    // GL version hints
#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    const char* glsl_version = "#version 150";
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    const char* glsl_version = "#version 130";
#endif
    
    // Create window
    m_window = glfwCreateWindow(1400, 900, "Watercan", nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        return false;
    }

    // Store pointer for callbacks and set up key shortcut handling
    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, [](GLFWwindow* w, int key, int scancode, int action, int mods){
        if (action == GLFW_PRESS && key == GLFW_KEY_O && (mods & GLFW_MOD_CONTROL)) {
            auto app = static_cast<App*>(glfwGetWindowUserPointer(w));
            if (app) {
                app->openFileDialog();
            }
        }
    });

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // Enable vsync
    
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    // Setup style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    
    // Custom colors for a cleaner look
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.35f, 0.55f, 0.80f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.45f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.50f, 0.80f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.20f, 0.25f, 0.90f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.40f, 0.60f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.22f, 0.35f, 0.50f, 0.80f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.45f, 0.65f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.50f, 0.75f, 1.00f);
    
    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load a secondary font with Cyrillic support (for Russian lyrics only)
    // The default ImGui font is kept for all other UI elements.
    {
        io.Fonts->AddFontDefault(); // slot 0: default UI font
        const char* fontPaths[] = {
            // Linux
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            // Windows
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\tahoma.ttf",
            // macOS
            "/System/Library/Fonts/Helvetica.ttc",
            "/Library/Fonts/Arial.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf",
        };
        for (const char* fp : fontPaths) {
            try {
                if (std::filesystem::exists(fp)) {
                    ImFontConfig cfg;
                    cfg.MergeMode = false;
                    cfg.GlyphRanges = io.Fonts->GetGlyphRangesCyrillic();
                    m_cyrillicFont = io.Fonts->AddFontFromFileTTF(fp, 15.0f, &cfg);
                    if (m_cyrillicFont) break;
                }
            } catch (...) {}
        }
        io.Fonts->Build();
    }
    
    // Initialize SDL audio subsystem (used by MusicPlayer) if available
#ifdef HAVE_SDL2
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[app] SDL_Init(SDL_INIT_AUDIO) failed: %s\n", SDL_GetError());
    }
#endif

    // Load About image (initial attempt)
    loadAboutImage();


// Note: The JSON data is not embedded in the executable by design; the app will load
// `seasonal_spiritshop.json` from the working directory or via File > Open.


    // Attempt to load saved user type colors from disk (non-fatal)
    loadTypeColorsFromDisk();
    // Initialize saved feedback timer to past time
    m_typeColorsSavedUntil = std::chrono::steady_clock::time_point::min();

    m_running = true;
    return true;
}

void App::run() {
    double lastTime = glfwGetTime();
    
    while (m_running && !glfwWindowShouldClose(m_window)) {
        // Calculate delta time
        double currentTime = glfwGetTime();
        float deltaTime = (float)(currentTime - lastTime);
        lastTime = currentTime;

        // Clamp delta time to avoid physics explosions after pause
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        // Poll events so the UI remains responsive even while dialogs are open
        glfwPollEvents();


        // Update physics for elastic node dragging and collision
        const SpiritTree* currentTree = m_selectedSpirit.empty() ? nullptr : m_treeManager.getTree(m_selectedSpirit);
        m_treeRenderer.updatePhysics(deltaTime, currentTree);
        
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // Render UI
        renderUI();

        // Render internal file-open dialog if requested
        if (m_showInternalOpenDialog) {
            // We'll render this as a modal
            ImGui::OpenPopup("Open JSON file");
            if (ImGui::BeginPopupModal("Open JSON file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                // Ensure the popup has focus so it appears in front of other windows
                ImGui::SetWindowFocus("Open JSON file");

                // Path input with Up button anchored to the right: InputText takes remaining space
                {
                    ImGuiStyle &style = ImGui::GetStyle();
                    const float btnW = 48.0f;
                    float avail = ImGui::GetContentRegionAvail().x;
                    float inputW = std::max(32.0f, avail - btnW - style.ItemSpacing.x);

                    ImGui::PushItemWidth(inputW);
                    char pathBuf[1024];
                    strncpy(pathBuf, m_internalDialogPath.c_str(), sizeof(pathBuf));
                    pathBuf[sizeof(pathBuf)-1] = '\0';
                    if (ImGui::InputText("##open_path", pathBuf, sizeof(pathBuf))) {
                        m_internalDialogPath = std::string(pathBuf);
                    }
                    ImGui::PopItemWidth();

                    // Place the Up button at the right edge
                    ImGui::SameLine();
                    float rightX = ImGui::GetWindowContentRegionMax().x - btnW;
                    ImGui::SetCursorPosX(rightX);
                    // Up textual button anchored to right
                    if (ImGui::Button("Back", ImVec2(btnW, 0))) {
                        try {
                            std::filesystem::path p(m_internalDialogPath);
                            std::filesystem::path abs = p.is_absolute() ? p : std::filesystem::absolute(p);
                            auto parent = abs.parent_path();
                            if (parent.empty()) parent = abs; // stay at root if no parent
                            m_internalDialogPath = parent.string();
                        } catch (...) {
                            auto pos = m_internalDialogPath.find_last_of('/');
                            if (pos != std::string::npos) m_internalDialogPath = m_internalDialogPath.substr(0, pos);
                            if (m_internalDialogPath.empty()) m_internalDialogPath = "/";
                        }
                    }
                }

                ImGui::Separator();

                // Breadcrumbs (clickable path segments)
                {
                    std::vector<std::string> parts;
                    std::string tmp = m_internalDialogPath;
                    if (tmp.empty()) tmp = "/";
                    // Normalize: remove trailing slashes except for root
                    while (tmp.size() > 1 && tmp.back() == '/') tmp.pop_back();
                    size_t start = 0;
                    while (start < tmp.size()) {
                        size_t pos = tmp.find('/', start);
                        if (pos == std::string::npos) pos = tmp.size();
                        std::string part = tmp.substr(start, pos - start);
                        if (!part.empty()) parts.push_back(part);
                        start = pos + 1;
                    }

                    // Render clickable breadcrumbs
                    std::string accum = (m_internalDialogPath.size() && m_internalDialogPath[0] == '/') ? "/" : "";
                    if (parts.empty()) {
                        ImGui::Text("/");
                    } else {
                        for (size_t i = 0; i < parts.size(); ++i) {
                            if (i != 0) ImGui::SameLine();
                            std::string label = parts[i];
                            // Taller breadcrumb buttons for easier clicking
                            if (ImGui::Button(label.c_str(), ImVec2(0, 20))) {
                                // Rebuild path up to this segment
                                accum = "/";
                                for (size_t j = 0; j <= i; ++j) {
                                    if (j != 0) accum += '/';
                                    accum += parts[j];
                                }
                                m_internalDialogPath = accum;
                            }
                        }
                    }
                }

                // Prepare sorted directory and file lists
                std::vector<std::string> dirs;
                std::vector<std::string> files;

                ImGui::BeginChild("file_list", ImVec2(600, 300), true);
                try {
                    // Collect directories and files and sort them for a stable, user-friendly order
                    for (const auto& entry : std::filesystem::directory_iterator(m_internalDialogPath)) {
                        try {
                            if (entry.is_directory()) {
                                dirs.push_back(entry.path().filename().string());
                            } else if (entry.path().extension() == ".json") {
                                files.push_back(entry.path().filename().string());
                            }
                        } catch (...) {
                            // ignore entries we can't stat
                        }
                    }
                    std::sort(dirs.begin(), dirs.end(), [](const std::string&a,const std::string&b){return strcasecmp(a.c_str(), b.c_str())<0;});
                    std::sort(files.begin(), files.end(), [](const std::string&a,const std::string&b){return strcasecmp(a.c_str(), b.c_str())<0;});

                    // Ensure icons exist
                    if (!m_iconFolderTexture || !m_iconFileTexture) createIconTextures();

                    // Directories first (icon + selectable)
                    for (const auto& name : dirs) {
                        bool selected = (std::string(m_internalSelectedFilename) == name);
                        ImGui::PushID(name.c_str());
                        ImGui::Image((void*)(intptr_t)m_iconFolderTexture, ImVec2(16,16));
                        // Robust double-click on icon area
                        {
                            ImVec2 min = ImGui::GetItemRectMin();
                            ImVec2 max = ImGui::GetItemRectMax();
                            ImVec2 mp = ImGui::GetIO().MousePos;
                            bool inRect = (mp.x >= min.x && mp.y >= min.y && mp.x <= max.x && mp.y <= max.y);
                            if (inRect && ImGui::IsMouseDoubleClicked(0)) {
                                if (m_internalDialogPath.back() != '/') m_internalDialogPath += '/';
                                m_internalDialogPath += name;
                                m_internalSelectedFilename[0] = '\0';
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Selectable(name.c_str(), selected)) {
                            // Select directory (single click)
                            std::strncpy(m_internalSelectedFilename, name.c_str(), sizeof(m_internalSelectedFilename)-1);
                            m_internalSelectedFilename[sizeof(m_internalSelectedFilename)-1] = '\0';
                            // Double-click enters directory
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                if (m_internalDialogPath.back() != '/') m_internalDialogPath += '/';
                                m_internalDialogPath += name;
                                m_internalSelectedFilename[0] = '\0';
                            }
                        }
                        // Show last-modified timestamp aligned to the right
                        try {
                            std::filesystem::path p = std::filesystem::path(m_internalDialogPath) / name;
                            std::time_t cftime = 0;
                            auto it = m_forcedTimestamps.find(p.string());
                            if (it != m_forcedTimestamps.end()) {
                                cftime = it->second;
                            } else {
                                auto ftime = std::filesystem::last_write_time(p);
                                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                    ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
                                cftime = std::chrono::system_clock::to_time_t(sctp);
                            }
                            char timestr[64];
                            std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M", std::localtime(&cftime));
                            float dateX = ImGui::GetWindowContentRegionMax().x - 140.0f;
                            ImGui::SameLine(dateX);
                            ImGui::TextUnformatted(timestr);
                        } catch (...) {}
                        ImGui::PopID();
                    }

                    // Then files (file icon + selectable)
                    for (const auto& name : files) {
                        bool selected = (std::string(m_internalSelectedFilename) == name);
                        ImGui::PushID(name.c_str());
                        ImGui::Image((void*)(intptr_t)m_iconFileTexture, ImVec2(16,16));
                        // Robust double-click on icon area
                        {
                            ImVec2 min = ImGui::GetItemRectMin();
                            ImVec2 max = ImGui::GetItemRectMax();
                            ImVec2 mp = ImGui::GetIO().MousePos;
                            bool inRect = (mp.x >= min.x && mp.y >= min.y && mp.x <= max.x && mp.y <= max.y);
                            if (inRect && ImGui::IsMouseDoubleClicked(0)) {
                                std::string fullPath = m_internalDialogPath;
                                if (fullPath.back() != '/') fullPath += '/';
                                fullPath += name;
                                fprintf(stderr, "[Watercan] internal open dialog: double-click open '%s'\n", fullPath.c_str());
                                loadFile(fullPath);
                                m_showInternalOpenDialog = false;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Selectable(name.c_str(), selected)) {
                            std::strncpy(m_internalSelectedFilename, name.c_str(), sizeof(m_internalSelectedFilename)-1);
                            m_internalSelectedFilename[sizeof(m_internalSelectedFilename)-1] = '\0';
                            // Double-click opens file
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                std::string fullPath = m_internalDialogPath;
                                if (fullPath.back() != '/') fullPath += '/';
                                fullPath += name;
                                fprintf(stderr, "[Watercan] internal open dialog: double-click open '%s'\n", fullPath.c_str());
                                loadFile(fullPath);
                                m_showInternalOpenDialog = false;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        // Show last-modified timestamp aligned to the right
                        try {
                            std::filesystem::path p = std::filesystem::path(m_internalDialogPath) / name;
                            auto ftime = std::filesystem::last_write_time(p);
                            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
                            std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
                            char timestr[64];
                            std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M", std::localtime(&cftime));
                            float dateX = ImGui::GetWindowContentRegionMax().x - 140.0f;
                            ImGui::SameLine(dateX);
                            ImGui::TextUnformatted(timestr);
                        } catch (...) {}
                        ImGui::PopID();
                    }

                    // Keyboard: Enter opens selected file or enters selected directory
                    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        if (m_internalSelectedFilename[0] != '\0') {
                            std::string sel(m_internalSelectedFilename);
                            // If selection matches a directory, enter it
                            if (std::find(dirs.begin(), dirs.end(), sel) != dirs.end()) {
                                if (m_internalDialogPath.back() != '/') m_internalDialogPath += '/';
                                m_internalDialogPath += sel;
                                m_internalSelectedFilename[0] = '\0';
                            } else {
                                // Otherwise treat as file and open
                                std::string fullPath = m_internalDialogPath;
                                if (fullPath.back() != '/') fullPath += '/';
                                fullPath += sel;
                                fprintf(stderr, "[Watercan] internal open dialog: enter open '%s'\n", fullPath.c_str());
                                loadFile(fullPath);
                                m_showInternalOpenDialog = false;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }

                } catch (...) {
                    ImGui::TextColored(ImVec4(1,0.6f,0.3f,1), "Failed to list directory");
                }
                ImGui::EndChild();

                ImGui::Separator();
                ImGui::Text("Selected: %s", m_internalSelectedFilename);
                if (ImGui::Button("Open", ImVec2(120,0))) {
                    if (m_internalSelectedFilename[0] != '\0') {
                        std::string sel(m_internalSelectedFilename);
                        // If selection is a directory, enter it
                        if (std::find(dirs.begin(), dirs.end(), sel) != dirs.end()) {
                            if (m_internalDialogPath.back() != '/') m_internalDialogPath += '/';
                            m_internalDialogPath += sel;
                            m_internalSelectedFilename[0] = '\0';
                        } else {
                            std::string fullPath = m_internalDialogPath;
                            if (fullPath.back() != '/') fullPath += '/';
                            fullPath += m_internalSelectedFilename;
                            fprintf(stderr, "[Watercan] internal open dialog: selected '%s'\n", fullPath.c_str());
                            loadFile(fullPath);
                            m_showInternalOpenDialog = false;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120,0))) {
                    m_showInternalOpenDialog = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        // Render internal save dialog if requested
        if (m_showInternalSaveDialog) {
            ImGui::OpenPopup("Save Spirit Shop JSON");
            if (ImGui::BeginPopupModal("Save Spirit Shop JSON", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                // Ensure the popup has focus
                ImGui::SetWindowFocus("Save Spirit Shop JSON");

                // Path input with Up button anchored to the right: InputText takes remaining space
                {
                    ImGuiStyle &style = ImGui::GetStyle();
                    const float btnW = 48.0f;
                    float avail = ImGui::GetContentRegionAvail().x;
                    float inputW = std::max(32.0f, avail - btnW - style.ItemSpacing.x);

                    ImGui::PushItemWidth(inputW);
                    char pathBuf[1024];
                    strncpy(pathBuf, m_internalSavePath.c_str(), sizeof(pathBuf));
                    pathBuf[sizeof(pathBuf)-1] = '\0';
                    if (ImGui::InputText("##save_path", pathBuf, sizeof(pathBuf))) {
                        m_internalSavePath = std::string(pathBuf);
                    }
                    ImGui::PopItemWidth();

                    // Place the Up button at the right edge
                    ImGui::SameLine();
                    float rightX = ImGui::GetWindowContentRegionMax().x - btnW;
                    ImGui::SetCursorPosX(rightX);
                    // Up textual button anchored to right
                    if (ImGui::Button("Up", ImVec2(btnW, 0))) {
                        try {
                            std::filesystem::path p(m_internalSavePath);
                            std::filesystem::path abs = p.is_absolute() ? p : std::filesystem::absolute(p);
                            auto parent = abs.parent_path();
                            if (parent.empty()) parent = abs;
                            m_internalSavePath = parent.string();
                        } catch (...) {
                            auto pos = m_internalSavePath.find_last_of('/');
                            if (pos != std::string::npos) m_internalSavePath = m_internalSavePath.substr(0, pos);
                            if (m_internalSavePath.empty()) m_internalSavePath = "/";
                        }
                    }
                }

                ImGui::Separator();

                // Breadcrumbs
                {
                    std::vector<std::string> parts;
                    std::string tmp = m_internalSavePath;
                    if (tmp.empty()) tmp = "/";
                    while (tmp.size() > 1 && tmp.back() == '/') tmp.pop_back();
                    size_t start = 0;
                    while (start < tmp.size()) {
                        size_t pos = tmp.find('/', start);
                        if (pos == std::string::npos) pos = tmp.size();
                        std::string part = tmp.substr(start, pos - start);
                        if (!part.empty()) parts.push_back(part);
                        start = pos + 1;
                    }

                    std::string accum = (m_internalSavePath.size() && m_internalSavePath[0] == '/') ? "/" : "";
                    if (parts.empty()) {
                        ImGui::Text("/");
                    } else {
                        for (size_t i = 0; i < parts.size(); ++i) {
                            if (i != 0) ImGui::SameLine();
                            std::string label = parts[i];
                            // Taller breadcrumb buttons for easier clicking
                            if (ImGui::Button(label.c_str(), ImVec2(0, 24))) {
                                accum = "/";
                                for (size_t j = 0; j <= i; ++j) {
                                    if (j != 0) accum += '/';
                                    accum += parts[j];
                                }
                                m_internalSavePath = accum;
                            }
                        }
                    }
                }

                // Directory and file listing (select a filename to save)
                std::vector<std::string> dirs;
                std::vector<std::string> files;

                ImGui::BeginChild("save_file_list", ImVec2(600, 300), true);
                try {
                    for (const auto& entry : std::filesystem::directory_iterator(m_internalSavePath)) {
                        try {
                            if (entry.is_directory()) {
                                dirs.push_back(entry.path().filename().string());
                            } else if (entry.path().extension() == ".json") {
                                files.push_back(entry.path().filename().string());
                            }
                        } catch (...) {}
                    }
                    std::sort(dirs.begin(), dirs.end(), [](const std::string&a,const std::string&b){return strcasecmp(a.c_str(), b.c_str())<0;});
                    std::sort(files.begin(), files.end(), [](const std::string&a,const std::string&b){return strcasecmp(a.c_str(), b.c_str())<0;});

                    // Ensure icons exist
                    if (!m_iconFolderTexture || !m_iconFileTexture) createIconTextures();

                    for (const auto& name : dirs) {
                        bool selected = (std::string(m_internalSaveSelectedFilename) == name);
                        ImGui::PushID(name.c_str());
                        ImGui::Image((void*)(intptr_t)m_iconFolderTexture, ImVec2(16,16));
                        // Robust double-click on icon area
                        {
                            ImVec2 min = ImGui::GetItemRectMin();
                            ImVec2 max = ImGui::GetItemRectMax();
                            ImVec2 mp = ImGui::GetIO().MousePos;
                            bool inRect = (mp.x >= min.x && mp.y >= min.y && mp.x <= max.x && mp.y <= max.y);
                            if (inRect && ImGui::IsMouseDoubleClicked(0)) {
                                if (m_internalSavePath.back() != '/') m_internalSavePath += '/';
                                m_internalSavePath += name;
                                m_internalSaveSelectedFilename[0] = '\0';
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Selectable(name.c_str(), selected)) {
                            std::strncpy(m_internalSaveSelectedFilename, name.c_str(), sizeof(m_internalSaveSelectedFilename)-1);
                            m_internalSaveSelectedFilename[sizeof(m_internalSaveSelectedFilename)-1] = '\0';
                            // Double-click enters directory
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                if (m_internalSavePath.back() != '/') m_internalSavePath += '/';
                                m_internalSavePath += name;
                                m_internalSaveSelectedFilename[0] = '\0';
                            }
                        }
                        // Show last-modified timestamp aligned to the right
                        try {
                            std::filesystem::path p = std::filesystem::path(m_internalSavePath) / name;
                            std::time_t cftime = 0;
                            auto it = m_forcedTimestamps.find(p.string());
                            if (it != m_forcedTimestamps.end()) {
                                cftime = it->second;
                            } else {
                                auto ftime = std::filesystem::last_write_time(p);
                                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                    ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
                                cftime = std::chrono::system_clock::to_time_t(sctp);
                            }
                            char timestr[64];
                            std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M", std::localtime(&cftime));
                            float dateX = ImGui::GetWindowContentRegionMax().x - 140.0f;
                            ImGui::SameLine(dateX);
                            ImGui::TextUnformatted(timestr);
                        } catch (...) {}
                        ImGui::PopID();
                    }

                    for (const auto& name : files) {
                        bool selected = (std::string(m_internalSaveSelectedFilename) == name);
                        ImGui::PushID(name.c_str());
                        ImGui::Image((void*)(intptr_t)m_iconFileTexture, ImVec2(16,16));
                        // Allow double-click on icon to save/open as well
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                            std::string fullPath = m_internalSavePath;
                            if (fullPath.back() != '/') fullPath += '/';
                            fullPath += name;
                            if (std::filesystem::exists(fullPath)) {
                                m_overwriteTargetPath = fullPath;
                                m_showOverwriteConfirm = true;
                            } else {
                                if (m_internalSaveSingle) {
                                    saveSingleSpiritToPath(fullPath, m_internalSaveSingleName);
                                    m_internalSaveSingle = false;
                                } else {
                                    saveFile(fullPath);
                                }
                                // Keep the save dialog open and show feedback
                                std::strncpy(m_internalSaveSelectedFilename, name.c_str(), sizeof(m_internalSaveSelectedFilename)-1);
                                m_internalSaveSelectedFilename[sizeof(m_internalSaveSelectedFilename)-1] = '\0';
                                m_internalSaveNew = false;
                                m_saveFeedbackUntil = glfwGetTime() + 1.0;
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Selectable(name.c_str(), selected)) {
                            // selecting a real file clears the '(New file)' mock selection
                            m_internalSaveNew = false;
                            std::strncpy(m_internalSaveSelectedFilename, name.c_str(), sizeof(m_internalSaveSelectedFilename)-1);
                            m_internalSaveSelectedFilename[sizeof(m_internalSaveSelectedFilename)-1] = '\0';
                            // Single click selects the filename
                            std::strncpy(m_internalSaveSelectedFilename, name.c_str(), sizeof(m_internalSaveSelectedFilename)-1);
                            m_internalSaveSelectedFilename[sizeof(m_internalSaveSelectedFilename)-1] = '\0';
                            // Double-click triggers overwrite confirmation + save
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                std::string fullPath = m_internalSavePath;
                                if (fullPath.back() != '/') fullPath += '/';
                                fullPath += name;
                                if (std::filesystem::exists(fullPath)) {
                                    m_overwriteTargetPath = fullPath;
                                    m_showOverwriteConfirm = true;
                                } else {
                                    if (m_internalSaveSingle) {
                                        saveSingleSpiritToPath(fullPath, m_internalSaveSingleName);
                                        m_internalSaveSingle = false;
                                    } else {
                                        saveFile(fullPath);
                                    }
                                    std::strncpy(m_internalSaveSelectedFilename, name.c_str(), sizeof(m_internalSaveSelectedFilename)-1);
                                    m_internalSaveSelectedFilename[sizeof(m_internalSaveSelectedFilename)-1] = '\0';
                                    m_internalSaveNew = false;
                                    m_saveFeedbackUntil = glfwGetTime() + 1.0;
                                }
                            }
                        }
                        // Show last-modified timestamp aligned to the right
                        try {
                            std::filesystem::path p = std::filesystem::path(m_internalSavePath) / name;
                            auto ftime = std::filesystem::last_write_time(p);
                            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
                            std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
                            char timestr[64];
                            std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M", std::localtime(&cftime));
                            float dateX = ImGui::GetWindowContentRegionMax().x - 140.0f;
                            ImGui::SameLine(dateX);
                            ImGui::TextUnformatted(timestr);
                        } catch (...) {}
                        ImGui::PopID();
                    }

                    // Mock '(New file)' entry at the end of the listing — no icon, pulsating text
                    {
                        const char* newLabel = "   (New file)";
                        bool newSelected = m_internalSaveNew;
                        ImGui::PushID("__new_file");
                        // Pulsate text from darker grey to white
                        double t = glfwGetTime();
                        float pulse = 0.5f * (1.0f + std::sin((float)t * 2.0f)); // 0..1
                        const float baseGrey = 0.55f;
                        float brightness = baseGrey + (1.0f - baseGrey) * pulse; // baseGrey..1.0
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(brightness, brightness, brightness, 1.0f));
                        if (ImGui::Selectable(newLabel, newSelected)) {
                            m_internalSaveNew = true;
                            m_internalSaveSelectedFilename[0] = '\0';
                        }
                        ImGui::PopStyleColor();
                        // Show blank/new timestamp (use current time if set)
                        try {
                            std::filesystem::path p = std::filesystem::path(m_internalSavePath) / std::string("__new__");
                            std::time_t cftime = 0;
                            auto it = m_forcedTimestamps.find((m_internalSavePath + "/" + std::string("__new__")));
                            if (it != m_forcedTimestamps.end()) cftime = it->second;
                            if (cftime != 0) {
                                char timestr[64];
                                std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M", std::localtime(&cftime));
                                float dateX = ImGui::GetWindowContentRegionMax().x - 140.0f;
                                ImGui::SameLine(dateX);
                                ImGui::TextUnformatted(timestr);
                            }
                        } catch (...) {}
                        ImGui::PopID();
                    }

                    // Keyboard: Enter saves selected file or enters directory
                    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        if (m_internalSaveSelectedFilename[0] != '\0') {
                            std::string sel(m_internalSaveSelectedFilename);
                            if (std::find(dirs.begin(), dirs.end(), sel) != dirs.end()) {
                                if (m_internalSavePath.back() != '/') m_internalSavePath += '/';
                                m_internalSavePath += sel;
                                m_internalSaveSelectedFilename[0] = '\0';
                            } else {
                                // File selected: copy into filename buffer and attempt save
                                std::string fullPath = m_internalSavePath;
                                if (fullPath.back() != '/') fullPath += '/';
                                fullPath += sel;
                                if (std::filesystem::exists(fullPath)) {
                                    m_overwriteTargetPath = fullPath;
                                    m_showOverwriteConfirm = true;
                                } else {
                                    if (m_internalSaveSingle) {
                                        saveSingleSpiritToPath(fullPath, m_internalSaveSingleName);
                                        m_internalSaveSingle = false;
                                    } else {
                                        saveFile(fullPath);
                                    }
                                    // keep dialog open, show feedback and select file
                                    std::string base = sel;
                                    std::strncpy(m_internalSaveSelectedFilename, base.c_str(), sizeof(m_internalSaveSelectedFilename)-1);
                                    m_internalSaveSelectedFilename[sizeof(m_internalSaveSelectedFilename)-1] = '\0';
                                    m_internalSaveNew = false;
                                    m_saveFeedbackUntil = glfwGetTime() + 1.0;
                                }
                            }
                        }
                    }

                } catch (...) {
                    ImGui::TextColored(ImVec4(1,0.6f,0.3f,1), "Failed to list directory");
                }
                ImGui::EndChild();

                ImGui::Separator();

                // Determine selected full path and whether it matches the currently-loaded file
                bool canSave = (m_internalSaveSelectedFilename[0] != '\0') || m_internalSaveNew;
                std::string selectedFullPath;
                if (m_internalSaveNew) {
                    std::string name = std::string(m_internalSaveNewName);
                    if (name.empty()) name = "Watered_Spirit_tree";
                    selectedFullPath = m_internalSavePath;
                    if (selectedFullPath.back() != '/') selectedFullPath += '/';
                    selectedFullPath += name;
                    // Ensure .json extension for new files
                    try {
                        std::filesystem::path sp(selectedFullPath);
                        if (sp.extension() != ".json") selectedFullPath += ".json";
                    } catch (...) {}
                } else if (m_internalSaveSelectedFilename[0] != '\0') {
                    selectedFullPath = m_internalSavePath;
                    if (selectedFullPath.back() != '/') selectedFullPath += '/';
                    selectedFullPath += m_internalSaveSelectedFilename;
                    // Ensure .json extension (safety)
                    try {
                        std::filesystem::path sp(selectedFullPath);
                        if (sp.extension() != ".json") selectedFullPath += ".json";
                    } catch (...) {}
                }
                bool selectedExists = false;
                bool isSelectedCurrent = false;
                if (canSave) {
                    try {
                        selectedExists = std::filesystem::exists(selectedFullPath);
                    } catch (...) {
                        selectedExists = false;
                    }
                    try {
                        auto a = std::filesystem::absolute(std::filesystem::path(selectedFullPath));
                        auto b = std::filesystem::absolute(std::filesystem::path(m_currentFilePath));
                        isSelectedCurrent = (a == b);
                    } catch (...) {
                        // Fallback to string compare if absolute() fails for any reason
                        isSelectedCurrent = (selectedFullPath == m_currentFilePath);
                    }
                }

                // Show selected filename and mark if it's the currently loaded file
                ImGui::Text("Selected: %s", m_internalSaveSelectedFilename[0] ? m_internalSaveSelectedFilename : "(none)");
                if (isSelectedCurrent) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.18f, 0.75f, 0.18f, 1.0f), "[Loaded file]");
                }

                // Name input and action buttons on a single row
                ImGui::BeginGroup();
                // Show name: either the selected filename or an input when '(New file)' is selected
                if (m_internalSaveNew) {
                    ImGui::Text("Name:");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(180.0f);
                    if (ImGui::InputText("##new_name", m_internalSaveNewName, sizeof(m_internalSaveNewName))) {
                        // nothing else to do here
                    }
                    ImGui::PopItemWidth();
                } else {
                    ImGui::Text("Name: %s", m_internalSaveSelectedFilename[0] ? m_internalSaveSelectedFilename : "(none)");
                }

                // Buttons on the same line, anchored to the right
                ImGui::SameLine();
                ImGuiStyle &style2 = ImGui::GetStyle();
                float pad2 = style2.WindowPadding.x;
                float saveW = 120.0f, cancelW = 120.0f;
                float spacing = style2.ItemSpacing.x;
                float right = ImGui::GetWindowContentRegionMax().x - pad2;
                float buttonsTotal = saveW + spacing + cancelW;
                ImGui::SetCursorPosX(right - buttonsTotal);

                

                double now = glfwGetTime();
                bool inSaveFeedback = (m_saveFeedbackUntil > now);

                // Detect if the selected entry is a directory so the Save button can act as "Open?"
                bool selectedIsDirectory = false;
                if (canSave && m_internalSaveSelectedFilename[0] != '\0') {
                    try {
                        std::filesystem::path cand = std::filesystem::path(m_internalSavePath) / m_internalSaveSelectedFilename;
                        selectedIsDirectory = std::filesystem::is_directory(cand);
                    } catch (...) { selectedIsDirectory = false; }
                }

                const char* saveLabel = "Save";
                if (inSaveFeedback) saveLabel = "Saved!";
                else if (selectedIsDirectory) saveLabel = "Open?";
                else if (selectedExists) saveLabel = "Overwrite?";

                // Button highlight colors
                bool pushedSaveColors = false;
                if (inSaveFeedback) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.60f, 0.18f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.35f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                    pushedSaveColors = true;
                } else if (selectedExists) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.55f, 0.18f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.35f, 0.05f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    pushedSaveColors = true;
                }

                ImGui::BeginDisabled(!canSave);
                if (ImGui::Button(saveLabel, ImVec2(saveW,0))) {
                    if (inSaveFeedback) {
                        // ignore while showing feedback
                    } else if (selectedIsDirectory) {
                        // Enter/display the selected directory
                        try {
                            std::filesystem::path sp = std::filesystem::path(m_internalSavePath) / m_internalSaveSelectedFilename;
                            std::string newPath = sp.string();
                            if (!newPath.empty() && newPath.back() != '/') newPath += '/';
                            m_internalSavePath = newPath;
                            m_internalSaveSelectedFilename[0] = '\0';
                            m_internalSaveNew = false;
                        } catch (...) {}
                    } else if (isSelectedCurrent) {
                        // Overwrite current file immediately and show feedback
                        if (m_internalSaveSingle) {
                            saveSingleSpiritToPath(selectedFullPath, m_internalSaveSingleName);
                            m_internalSaveSingle = false;
                        } else {
                            saveFile(selectedFullPath);
                        }
                        m_saveFeedbackUntil = now + 1.0; // 1 second
                    } else if (canSave) {
                        // Save file (ensure .json extension)
                        try {
                            std::filesystem::path sp(selectedFullPath);
                            if (sp.extension() != std::string(".json")) selectedFullPath += ".json";
                        } catch (...) {}
                        if (m_internalSaveSingle) {
                            saveSingleSpiritToPath(selectedFullPath, m_internalSaveSingleName);
                            m_internalSaveSingle = false;
                        } else {
                            saveFile(selectedFullPath);
                        }
                        // Keep the save dialog open and show feedback, select saved file
                        try {
                            std::filesystem::path sp(selectedFullPath);
                            std::string name = sp.filename().string();
                            std::strncpy(m_internalSaveSelectedFilename, name.c_str(), sizeof(m_internalSaveSelectedFilename)-1);
                            m_internalSaveSelectedFilename[sizeof(m_internalSaveSelectedFilename)-1] = '\0';
                        } catch (...) {}
                        m_internalSaveNew = false;
                        m_saveFeedbackUntil = now + 1.0;
                    }
                }
                ImGui::EndDisabled();

                if (pushedSaveColors) {
                    ImGui::PopStyleColor(4);
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(cancelW,0))) {
                    m_showInternalSaveDialog = false;
                    m_internalSaveSingle = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndGroup();





                // If an overwrite was requested, perform it immediately (no confirmation modal)
                if (m_showOverwriteConfirm && !m_overwriteTargetPath.empty()) {
                    if (m_internalSaveSingle) {
                        saveSingleSpiritToPath(m_overwriteTargetPath, m_internalSaveSingleName);
                        m_internalSaveSingle = false;
                    } else {
                        saveFile(m_overwriteTargetPath);
                    }
                    // Keep dialog open and select the saved file, show feedback
                    try {
                        std::filesystem::path sp(m_overwriteTargetPath);
                        std::string name = sp.filename().string();
                        std::strncpy(m_internalSaveSelectedFilename, name.c_str(), sizeof(m_internalSaveSelectedFilename)-1);
                        m_internalSaveSelectedFilename[sizeof(m_internalSaveSelectedFilename)-1] = '\0';
                    } catch (...) {}
                    m_internalSaveNew = false;
                    m_saveFeedbackUntil = glfwGetTime() + 1.0;
                    m_showOverwriteConfirm = false;
                    m_overwriteTargetPath.clear();
                    m_pendingSavePath.clear();
                }

                ImGui::EndPopup();
            }
        }
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(m_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glfwSwapBuffers(m_window);
    }
}

void App::shutdown() {
    // Signal any background threads to stop or finish
    // No background file dialog threads are used.

    // Cleanup About image texture
    if (m_aboutImageTexture) {
        glDeleteTextures(1, &m_aboutImageTexture);
        m_aboutImageTexture = 0;
    }
    // Cleanup icon textures
    if (m_iconFolderTexture) {
        glDeleteTextures(1, &m_iconFolderTexture);
        m_iconFolderTexture = 0;
    }
    if (m_iconFileTexture) {
        glDeleteTextures(1, &m_iconFileTexture);
        m_iconFileTexture = 0;
    }
    
    // Ensure audio is stopped and resources are freed
    m_musicPlayer.stop();
    m_musicPlayer.unload();
#ifdef HAVE_SDL2
    SDL_Quit();
#endif

    if (m_window) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        
        glfwDestroyWindow(m_window);
        glfwTerminate();
        m_window = nullptr;
    }
}

void App::loadAboutImage(const std::string& imageName) {
    // If already loaded with the same image, nothing to do
    if (m_aboutImageTexture && m_currentAboutImageName == imageName) return;
    
    // If switching images, delete existing texture first
    if (m_aboutImageTexture) {
        glDeleteTextures(1, &m_aboutImageTexture);
        m_aboutImageTexture = 0;
    }

#if defined(BUILD_SINGLE_EXE) || defined(BUILD_WINDOWS_SINGLE_EXE)
    // For single-exe builds, use the generated embedded resource accessor instead of direct symbols
    size_t embeddedLen = 0;
    const unsigned char* embeddedData = embedded_resource_data(imageName.c_str(), &embeddedLen);
    if (embeddedData && embeddedLen > 0) {
        int width = 0, height = 0, channels = 0;
        unsigned char* data = stbi_load_from_memory(const_cast<unsigned char*>(embeddedData), (int)embeddedLen, &width, &height, &channels, 4);
        if (data) {
            fprintf(stderr, "[loadAboutImage] loaded embedded image %dx%d\n", width, height);
            glGenTextures(1, &m_aboutImageTexture);
            glBindTexture(GL_TEXTURE_2D, m_aboutImageTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            m_aboutImageWidth = width;
            m_aboutImageHeight = height;
            stbi_image_free(data);
            m_currentAboutImageName = imageName;
            return;
        } else {
            fprintf(stderr, "[loadAboutImage] failed to decode embedded image\n");
        }
    }
#endif

    // Fall back to loading from disk (useful for development builds)
    std::string candidates[] = {
        "../res/" + imageName,
        "res/" + imageName,
        "./res/" + imageName
    };

    for (const std::string& path : candidates) {
        int width = 0, height = 0, channels = 0;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        fprintf(stderr, "[loadAboutImage] trying disk path '%s'... %s\n", path.c_str(), data ? "found" : "not found");
        if (!data) continue;

        fprintf(stderr, "[loadAboutImage] loaded disk image %dx%d from '%s'\n", width, height, path.c_str());
        glGenTextures(1, &m_aboutImageTexture);
        glBindTexture(GL_TEXTURE_2D, m_aboutImageTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        m_aboutImageWidth = width;
        m_aboutImageHeight = height;
        stbi_image_free(data);
        m_currentAboutImageName = imageName;
        break;
    }

    if (!m_aboutImageTexture) {
        fprintf(stderr, "[loadAboutImage] no about image available\n");
    }
}

void App::openUrl(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open '" + url + "' &";
    std::system(cmd.c_str());
#else
    // Try xdg-open on Linux and ignore failures
    std::string cmd = "xdg-open '" + url + "' 2>/dev/null &";
    std::system(cmd.c_str());
#endif
}

void App::createIconTextures() {
    if (m_iconFolderTexture && m_iconFileTexture) return;

    auto make_texture = [](int w, int h, const std::vector<unsigned char>& pixels, unsigned int &outTex){
        glGenTextures(1, &outTex);
        glBindTexture(GL_TEXTURE_2D, outTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    };

    const int W = 16, H = 16;

    // Folder icon: yellow folder with tab
    std::vector<unsigned char> folder(W*H*4, 0);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        unsigned char* p = &folder[(y*W + x)*4];
        p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0;
        if (y >= 5 && y <= 12 && x >= 1 && x <= 14) {
            p[0] = 220; p[1] = 180; p[2] = 60; p[3] = 255; // yellow
        }
        if (y >= 2 && y <= 5 && x >= 2 && x <= 7) {
            p[0] = 200; p[1] = 150; p[2] = 40; p[3] = 255; // darker
        }
        if ((y == 4 || y == 12) && x >= 1 && x <= 14) { p[0]=160; p[1]=120; p[2]=30; p[3]=255; }
        if ((x == 1 || x == 14) && y >= 5 && y <= 12) { p[0]=160; p[1]=120; p[2]=30; p[3]=255; }
    }
    make_texture(W, H, folder, m_iconFolderTexture);

    // File icon: white page with folded corner
    std::vector<unsigned char> file(W*H*4, 0);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        unsigned char* p = &file[(y*W + x)*4];
        p[0]=0; p[1]=0; p[2]=0; p[3]=0;
        if (x >= 2 && x <= 13 && y >= 2 && y <= 13) {
            p[0]=240; p[1]=240; p[2]=240; p[3]=255;
        }
        if (x >= 9 && y <= 5) {
            p[0]=200; p[1]=200; p[2]=200; p[3]=255;
        }
        if ((x==2 || x==13) && y>=2 && y<=13) { p[0]=180; p[1]=180; p[2]=180; p[3]=255; }
        if ((y==2 || y==13) && x>=2 && x<=13) { p[0]=180; p[1]=180; p[2]=180; p[3]=255; }
    }
    make_texture(W, H, file, m_iconFileTexture);


}

void App::renderUI() {
    // Full window docking space
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags window_flags = 
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("MainWindow", nullptr, window_flags);
    ImGui::PopStyleVar(3);
    
    renderMenuBar();
    
    // Main content area
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    contentSize.y -= 25.0f;  // Reserve space for status bar
    
    // Calculate widths - clamp details panel to fit within available space
    float splitterWidth = 4.0f;
    float availableWidth = contentSize.x;
    
    // Ensure panels fit within available width
    float minCenterWidth = 100.0f;
    float maxDetailsWidth = availableWidth - m_sidebarWidth - minCenterWidth - (splitterWidth * 2);
    if (maxDetailsWidth < 200.0f) maxDetailsWidth = 200.0f;
    m_detailsWidth = std::min(m_detailsWidth, maxDetailsWidth);
    
    float centerWidth = availableWidth - m_sidebarWidth - m_detailsWidth - (splitterWidth * 2);
    if (centerWidth < minCenterWidth) centerWidth = minCenterWidth;
    
    // Left panel - Spirit list
    ImGui::BeginChild("SpiritListPanel", ImVec2(m_sidebarWidth, contentSize.y), true);
    renderSpiritList();
    ImGui::EndChild();
    
    // Splitter (left)
    ImGui::SameLine();
    ImGui::Button("##vsplitter_left", ImVec2(splitterWidth, contentSize.y));
    if (ImGui::IsItemActive()) {
        m_sidebarWidth += ImGui::GetIO().MouseDelta.x;
        m_sidebarWidth = std::clamp(m_sidebarWidth, 150.0f, 400.0f);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    
    // Center panel - Tree viewport (fills remaining space, use -1 to auto-calculate)
    ImGui::SameLine();
    float rightPanelTotalWidth = m_detailsWidth + splitterWidth;
    float actualCenterWidth = ImGui::GetContentRegionAvail().x - rightPanelTotalWidth;
    if (actualCenterWidth < 100.0f) actualCenterWidth = 100.0f;
    ImGui::BeginChild("TreeViewport", ImVec2(actualCenterWidth, contentSize.y), true);
    renderTreeViewport();
    ImGui::EndChild();
    
    // Splitter (right)
    ImGui::SameLine();
    ImGui::Button("##vsplitter_right", ImVec2(splitterWidth, contentSize.y));
    if (ImGui::IsItemActive()) {
        m_detailsWidth -= ImGui::GetIO().MouseDelta.x;
        m_detailsWidth = std::clamp(m_detailsWidth, 200.0f, 450.0f);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    
    // Right panel container - use remaining width to anchor to right edge
    ImGui::SameLine();
    ImGui::BeginChild("RightPanelContainer", ImVec2(0, contentSize.y), false);
    
    // Calculate heights for node details and JSON editor
    // Node details gets a fixed smaller height, JSON editor takes the rest
    float splitterHeight = 4.0f;
    float jsonEditorHeight = contentSize.y - m_nodeDetailsHeight - splitterHeight;
    if (jsonEditorHeight < 100.0f) {
        jsonEditorHeight = 100.0f;
        m_nodeDetailsHeight = contentSize.y - jsonEditorHeight - splitterHeight;
        if (m_nodeDetailsHeight < 100.0f) m_nodeDetailsHeight = 100.0f;
    }
    
    // Top: Node details panel
    ImGui::BeginChild("NodeDetailsPanel", ImVec2(0, m_nodeDetailsHeight), true);
    renderNodeDetails();
    ImGui::EndChild();
    
    // Horizontal splitter
    ImGui::Button("##hsplitter", ImVec2(-1, splitterHeight));
    if (ImGui::IsItemActive()) {
        m_nodeDetailsHeight += ImGui::GetIO().MouseDelta.y;
        m_nodeDetailsHeight = std::clamp(m_nodeDetailsHeight, 100.0f, contentSize.y - 100.0f - splitterHeight);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    
    // Bottom: JSON editor panel - use 0 height to fill remaining space
    ImGui::BeginChild("JsonEditorPanel", ImVec2(0, 0), true);
    renderNodeJsonEditor();
    ImGui::EndChild();
    
    ImGui::EndChild();  // End RightPanelContainer
    
    // Status bar (new line, full width)
    renderStatusBar();
    
    ImGui::End();
    
    // About dialog
    if (m_showAbout) {
        // Ensure the image is loaded before opening the popup
        loadAboutImage();

        // Attempt to load the embedded About music (res/inneruniverse.ogg) only if the secret has been unlocked
        if (m_aboutMusicUnlocked && !m_aboutMusicLoaded) {
            bool loadedFromDisk = false;
            std::vector<std::string> candidates = {"../res/inneruniverse.ogg", "res/inneruniverse.ogg", "./res/inneruniverse.ogg"};
            for (const auto &c : candidates) {
                try {
                    if (std::filesystem::exists(c)) {
                        if (m_musicPlayer.load(c)) {
                            m_aboutMusicLoaded = true;
                            loadedFromDisk = true;
                            break;
                        }
                    }
                } catch (...) {}
            }
            // If not found on disk, try embedded resource
            if (!loadedFromDisk && !m_aboutMusicLoaded) {
                size_t elen = 0;
                const unsigned char* em = embedded_resource_data("inneruniverse.ogg", &elen);
                if (em && elen > 0) {
                    if (m_musicPlayer.loadFromMemory(em, elen)) {
                        m_aboutMusicLoaded = true;
                    }
                }
            }
        }

        ImGui::OpenPopup("About Watercan");
        m_showAbout = false;
    }

    // Tools: FNV1a32 Generator dialog
    if (m_showFNVDialog) {
        ImGui::OpenPopup("FNV1a32 Generator");
        m_showFNVDialog = false;
    }
    // Tools: Color codes dialog
    if (m_showColorCodes) {
        ImGui::OpenPopup("Color Code editor");
        m_showColorCodes = false;
    }


    if (ImGui::BeginPopupModal("FNV1a32 Generator", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Allow Escape key to close the modal as well
        if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) { ImGui::CloseCurrentPopup(); }

        ImGui::Text("Item IDs in Sky are generated using FNV-1a32 hashing of the item name.");
        ImGui::Separator();
        ImGui::InputText("Name (nm)", m_fnvNameBuf, sizeof(m_fnvNameBuf));
        ImGui::Spacing();
        // Convert button
        if (ImGui::Button("Convert")) {
            std::string name(m_fnvNameBuf);
            m_fnvResult = fnv1a32(name);
        }
        ImGui::Spacing();
        ImGui::Separator();
        // Result
        ImGui::Text("Result:");
        ImGui::Text("Decimal: %u", m_fnvResult);
        ImGui::Text("Hex: 0x%08X", m_fnvResult);
        ImGui::Spacing();
        if (ImGui::Button("Copy Decimal")) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%u", m_fnvResult);
            ImGui::SetClipboardText(buf);
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy Hex")) {
            char buf[64];
            snprintf(buf, sizeof(buf), "0x%08X", m_fnvResult);
            ImGui::SetClipboardText(buf);
        }

        // Exit button anchored to bottom-right
        ImGui::Spacing();
        ImGuiStyle &style2 = ImGui::GetStyle();
        float exitW = ImGui::CalcTextSize("Exit").x + style2.FramePadding.x*2.0f + 12.0f;
        float exitH = ImGui::GetFrameHeight();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 exitScreenPos = ImVec2(winPos.x + ImGui::GetWindowWidth() - exitW - style2.WindowPadding.x,
                                      winPos.y + ImGui::GetWindowHeight() - exitH - style2.WindowPadding.y);
        ImGui::SetCursorScreenPos(exitScreenPos);
        if (ImGui::Button("Exit", ImVec2(exitW, exitH))) { ImGui::CloseCurrentPopup(); }

        ImGui::EndPopup();
    }

    // Color Codes dialog: fixed size 900x600 and non-resizable
    ImGui::SetNextWindowSize(ImVec2(770, 600), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Color Code editor", nullptr, ImGuiWindowFlags_NoResize)) {

        // Collect all types found in the currently loaded file
        std::vector<std::string> types;
        for (const auto& s : m_treeManager.getSpiritNames()) {
            const SpiritTree* t = m_treeManager.getTree(s);
            if (!t) continue;
            for (const auto& n : t->nodes) if (!n.type.empty()) types.push_back(n.type);
        }
        for (const auto& s : m_treeManager.getGuideNames()) {
            const SpiritTree* t = m_treeManager.getTree(s);
            if (!t) continue;
            for (const auto& n : t->nodes) if (!n.type.empty()) types.push_back(n.type);
        }
        std::sort(types.begin(), types.end());
        types.erase(std::unique(types.begin(), types.end()), types.end());

        // Helper: generate a pleasant default color from a string hash
        auto defaultColorFor = [](const std::string &k) {
            size_t h = std::hash<std::string>{}(k);
            // map hash to hue [0,1)
            float hue = (float)(h % 360) / 360.0f;
            float s = 0.5f + (float)((h >> 8) % 50) / 100.0f; // 0.5-1.0
            float v = 0.65f + (float)((h >> 16) % 35) / 100.0f; // 0.65-1.0
            // HSV -> RGB
            float r,g,b;
            int i = (int)(hue * 6.0f);
            float f = hue * 6.0f - i;
            float p = v * (1.0f - s);
            float q = v * (1.0f - f * s);
            float t = v * (1.0f - (1.0f - f) * s);
            switch (i % 6) {
                case 0: r=v; g=t; b=p; break;
                case 1: r=q; g=v; b=p; break;
                case 2: r=p; g=v; b=t; break;
                case 3: r=p; g=q; b=v; break;
                case 4: r=t; g=p; b=v; break;
                default: r=v; g=p; b=q; break;
            }
            return std::array<float,4>{r,g,b,1.0f};
        };

        // Ensure m_typeColors has entries for every found typ so preview uses picker color immediately
        for (const auto &typ : types) {
            if (m_typeColors.find(typ) == m_typeColors.end()) {
                m_typeColors[typ] = defaultColorFor(typ);
            }
        }

        // Layout sizes for preview and left column (compute to fit the dialog)
        ImGuiStyle &style = ImGui::GetStyle();
        float winW = ImGui::GetWindowWidth();
        float winH = ImGui::GetWindowHeight();
        float leftW = 220.0f;
        // Leave room for header and bottom buttons; compute preview height accordingly
        float previewH = std::max(200.0f, winH - (style.WindowPadding.y * 2.0f + 80.0f));
        float previewW = std::max(240.0f, winW - leftW - style.WindowPadding.x * 2.0f - 24.0f);
        // Left column: type color pickers (match preview height)
        ImGui::BeginChild("TypeList", ImVec2(leftW, previewH), true);
        for (const auto &typ : types) {
            ImGui::PushID(typ.c_str());
            ImGui::TextUnformatted(typ.c_str());
            ImGui::SameLine();
            std::array<float,4> col = defaultColorFor(typ);
            auto it = m_typeColors.find(typ);
            if (it != m_typeColors.end()) col = it->second;
            float fcol[4] = { col[0], col[1], col[2], col[3] };
            if (ImGui::ColorEdit4("##col_preview", fcol, ImGuiColorEditFlags_NoInputs)) {
                m_typeColors[typ] = { fcol[0], fcol[1], fcol[2], fcol[3] };
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        // Position preview at the right side of the dialog and match heights
        ImVec2 winPos = ImGui::GetWindowPos();
        float desiredX = winPos.x + winW - style.WindowPadding.x - previewW;
        // Align preview top with the TypeList child's top so columns line up
        ImVec2 typeListPos = ImGui::GetItemRectMin(); // top-left of the last item (the TypeList child)
        ImGui::SetCursorScreenPos(ImVec2(desiredX, typeListPos.y));
        // Show preview embedded in this modal (mock-up view showing all found types)
        ImGui::BeginChild("PreviewCanvas", ImVec2(previewW, previewH), true);

        // Preview area (full width): create a circular layout showing one node per found typ
        ImGui::BeginChild("PreviewView", ImVec2(0,0), false);
        // Compute view canvas position and size for possible overlay drawing
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        // Build a mock preview tree that contains exactly one node per found type, arranged in a circle
        SpiritTree preview;
        preview.spiritName = "preview_types";
        size_t N = types.size();
        if (N == 0) {
            ImGui::TextColored(ImVec4(1,0.6f,0.3f,1), "No types found in the loaded file.");
        } else {
            float radius = std::min(canvasSize.x, canvasSize.y) * 0.35f;
            uint64_t baseId = 0xE0000000; // high base to avoid accidental collisions
            // Layout rules:
            // - First 9 types placed evenly around circle
            // - 10th type (index 9) placed in the center
            // - Up to 3 extra nodes at each corner (NW, NE, SW, SE) = 12 extra
            // Total capacity = 9 + 1 + 12 = 22
            if (N > 22) {
                ImGui::TextColored(ImVec4(1,0.6f,0.3f,1), "Sorry! You've reached the type limit for the preview.");
            } else {
                // Place circle nodes (up to 9)
                size_t circleCount = std::min((size_t)9, N);
                for (size_t i = 0; i < circleCount; ++i) {
                    SpiritNode n;
                    n.id = baseId + (uint64_t)i + 1;
                    n.name.clear();
                    n.type = types[i];
                    float angle = (float)i / (float)circleCount * 2.0f * 3.14159265f;
                    n.x = cosf(angle) * radius;
                    n.y = sinf(angle) * radius;
                    preview.nodes.push_back(n);
                }

                // Center node if present
                size_t idx = circleCount;
                if (N >= 10) {
                    SpiritNode centerNode;
                    centerNode.id = baseId + (uint64_t)idx + 1;
                    centerNode.name.clear();
                    centerNode.type = types[idx];
                    centerNode.x = 0.0f;
                    centerNode.y = 0.0f;
                    preview.nodes.push_back(centerNode);
                    idx++;
                }

                // Corner placements (NW, NE, SW, SE), up to 3 each
                struct Corner { float dx, dy; } corners[4] = { { -1.0f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } };
                size_t cornerCapacity = 3;
                for (size_t c = 0; c < 4 && idx < N; ++c) {
                    Corner corner = corners[c];
                    // base position near corner
                    float baseX = corner.dx * radius * 0.85f;
                    float baseY = corner.dy * radius * 0.85f;
                    // perpendicular for stacking
                    float perpX = -corner.dy;
                    float perpY = corner.dx;
                    for (size_t slot = 0; slot < cornerCapacity && idx < N; ++slot, ++idx) {
                        SpiritNode n;
                        n.id = baseId + (uint64_t)idx + 1;
                        n.name.clear();
                        n.type = types[idx];
                        // center the 3 slots around base, spacing by 18 pixels (screen units)
                        float offset = (float)((int)slot - 1) * 18.0f;
                        n.x = baseX + perpX * offset;
                        n.y = baseY + perpY * offset;
                        preview.nodes.push_back(n);
                    }
                }

                TreeRenderer previewRenderer;
                previewRenderer.resetView();
                float zoom = 0.75f;
                previewRenderer.setZoom(zoom);
                // Center the preview on the nodes' bounding box
                float minX = std::numeric_limits<float>::max();
                float maxX = -std::numeric_limits<float>::max();
                float minY = std::numeric_limits<float>::max();
                float maxY = -std::numeric_limits<float>::max();
                for (const auto &n : preview.nodes) {
                    minX = std::min(minX, n.x);
                    maxX = std::max(maxX, n.x);
                    minY = std::min(minY, n.y);
                    maxY = std::max(maxY, n.y);
                }
                float centerX = 0.0f, centerY = 0.0f;
                if (!preview.nodes.empty()) {
                    centerX = (minX + maxX) * 0.5f;
                    centerY = (minY + maxY) * 0.5f;
                }
                float panX = -centerX;
                float panY = centerY - (canvasSize.y * 0.25f / zoom);
                previewRenderer.setPan(ImVec2(panX, panY));
                // Render the mock preview — colors are still driven by m_typeColors if present
                previewRenderer.render(&preview, false, nullptr, false, nullptr, nullptr, false, true, &m_typeColors, nullptr, nullptr, nullptr, nullptr);
            }
        }

        ImGui::EndChild();
        ImGui::EndChild();

        // Anchor Save and Close buttons to the bottom of the dialog
        float saveW = 160.0f;
        float closeW = 120.0f;
        float btnH = ImGui::GetFrameHeight();
        // Reuse existing winPos variable (declared earlier) to avoid redeclaration
        winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        float paddingX = style.WindowPadding.x;
        float paddingY = style.WindowPadding.y;

        // Save button at bottom-left (disabled when no file loaded)
        bool savedClicked = false;
        ImGui::SetCursorScreenPos(ImVec2(winPos.x + paddingX, winPos.y + winSize.y - paddingY - btnH));
        ImGui::BeginDisabled(!m_treeManager.isLoaded());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.9f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.7f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        if (std::chrono::steady_clock::now() < m_typeColorsSavedUntil) {
            ImGui::Button("Saved!", ImVec2(saveW, 0));
        } else {
            if (ImGui::Button("Save user preferences", ImVec2(saveW, 0))) {
                savedClicked = true;
            }
        }
        ImGui::PopStyleColor(4);
        ImGui::EndDisabled();

        if (savedClicked) {
            if (saveTypeColorsToDisk()) {
                m_typeColorsSavedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            } else {
                m_typeColorsSavedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
        }

        // Close button at bottom-right
        ImGui::SetCursorScreenPos(ImVec2(winPos.x + winSize.x - paddingX - closeW, winPos.y + winSize.y - paddingY - btnH));
        if (ImGui::Button("Close", ImVec2(closeW, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Make the About dialog size responsive to display size so it looks correct on HiDPI and varied desktop setups
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    ImFont* font = ImGui::GetFont();
    float fontH = ImGui::GetFontSize();
    float pad = style.WindowPadding.x;

    // Measure key text widths so the dialog is wide enough to show all text without wrapping
    std::string title = std::string("Watercan ") + WATERCAN_VERSION + " - Vibecoded by Dusk//Night with Copilot wheelchair assistance";
    float titleW = ImGui::CalcTextSize(title.c_str()).x;
    float desc1W = ImGui::CalcTextSize("JSON-based dependency tree viewer and editor specialized for Sky: Children of the Light").x;
    float desc2W = ImGui::CalcTextSize("For use with private servers and their communities.").x;
    float shellW = ImGui::CalcTextSize("This release of Watercan has been given the symbolic name of 'Shell'!").x;
    float textMaxW = std::max(std::max(titleW, desc1W), std::max(desc2W, shellW));

    // Account for the image width (scaled for HiDPI) if present
    float imgW = 0.0f;
    if (m_aboutImageTexture) {
        ImVec2 fbScale = io.DisplayFramebufferScale;
        imgW = (float)m_aboutImageWidth / (fbScale.x > 0.0f ? fbScale.x : 1.0f);
    }

    // Desired width: enough for image + text + padding, but not exceeding 95% of display
    float desiredW = std::min(io.DisplaySize.x * 0.95f, std::max(textMaxW + imgW + pad * 6.0f, 540.0f));
    float winW = std::clamp(desiredW, 480.0f, io.DisplaySize.x * 0.95f);

    // Ensure height is at least enough for several lines of text plus the button row so Close/License are always visible
    float minHeight = fontH * 8.0f + ImGui::GetFrameHeight() + pad * 4.0f;
    float desiredH = std::clamp(io.DisplaySize.y * 0.5f, minHeight, io.DisplaySize.y * 0.9f);
    float winH = desiredH;

    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("About Watercan", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Watercan %s - Vibecoded by Dusk//Night with Copilot wheelchair assistance", WATERCAN_VERSION);
        ImGui::Separator();
        ImGui::Text("JSON-based dependency tree viewer and editor specialized for Sky: Children of the Light");
        
        // Display the About image with text beside it
        if (m_aboutImageTexture) {
            // Account for framebuffer scaling (HiDPI) so image appears at intended physical size
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 fbScale = io.DisplayFramebufferScale;
            float displayW = (float)m_aboutImageWidth / (fbScale.x > 0.0f ? fbScale.x : 1.0f);
            float displayH = (float)m_aboutImageHeight / (fbScale.y > 0.0f ? fbScale.y : 1.0f);
            // Constrain image to a reasonable portion of the dialog width
            float availW = ImGui::GetContentRegionAvail().x;
            float maxW = std::max(120.0f, availW * 0.45f);
            if (displayW > maxW) {
                float r = maxW / displayW;
                displayW *= r; displayH *= r;
            }

            ImGui::Image((ImTextureID)(intptr_t)m_aboutImageTexture, ImVec2(displayW, displayH));

            ImGui::SameLine();

            // Text beside image and music player under the text
            ImGui::BeginGroup();
            ImGui::Text("For use with private servers and their communities.");

            // Render the sentence with "Shell" as inline clickable letters
            ImGui::Text("This release of Watercan has been given the symbolic name of '");
            const char *shellLetters[5] = {"S","h","e","l","l"};
            const ImVec4 rainbow[5] = {
                ImVec4(1.0f, 0.2f, 0.2f, 1.0f),  // red
                ImVec4(1.0f, 0.5f, 0.0f, 1.0f),  // orange
                ImVec4(1.0f, 0.85f, 0.0f, 1.0f), // yellow
                ImVec4(0.2f, 0.8f, 0.2f, 1.0f),  // green
                ImVec4(0.2f, 0.6f, 1.0f, 1.0f)   // blue
            };
            for (int i = 0; i < 5; ++i) {
                ImGui::SameLine(0, 0);
                ImGui::PushID(i);
                ImVec2 sz = ImGui::CalcTextSize(shellLetters[i]);
                ImGui::InvisibleButton("##sl", sz);
                ImVec2 pos = ImGui::GetItemRectMin();
                // Use rainbow color if this letter has been activated, otherwise default text color
                ImVec4 col = (i < m_shellColoredLetters) ? m_shellLetterColors[i] : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, ImGui::ColorConvertFloat4ToU32(col), shellLetters[i]);
                if (ImGui::IsItemClicked()) {
                    if (m_shellColoredLetters < 5) {
                        m_shellLetterColors[m_shellColoredLetters] = rainbow[m_shellColoredLetters];
                        m_shellColoredLetters++;
                    } else {
                        std::array<ImVec4,5> tmp = m_shellLetterColors;
                        for (int j = 0; j < 5; ++j) tmp[j] = m_shellLetterColors[(j+1)%5];
                        m_shellLetterColors = tmp;
                    }
                }
                ImGui::PopID();
            }
            ImGui::SameLine(0, 0);
            ImGui::Text("'!");

            // If fully colored, allow holding CTRL+ALT+S here to unlock the music player
            if (m_shellColoredLetters >= 5) {
                bool ctrlDownLocal = m_window && ((glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) || (glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS));
                bool altDownLocal = m_window && ((glfwGetKey(m_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) || (glfwGetKey(m_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS));
                bool sDownLocal = m_window && (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS);

                double secretProgress = 0.0;
                if (ctrlDownLocal && altDownLocal && sDownLocal) {
                    if (!m_ctrlAltSHoldActive) {
                        m_ctrlAltSHoldActive = true;
                        m_ctrlAltSHoldStart = std::chrono::steady_clock::now();
                    } else {
                        auto now = std::chrono::steady_clock::now();
                        secretProgress = std::chrono::duration<double>(now - m_ctrlAltSHoldStart).count() / 5.0;
                        if (secretProgress >= 1.0 && !m_aboutMusicUnlocked) {
                            m_aboutMusicUnlocked = true;
                            // attempt to load music immediately
                            std::vector<std::string> candidates = {"../res/inneruniverse.ogg", "res/inneruniverse.ogg", "./res/inneruniverse.ogg"};
                            for (const auto &c : candidates) {
                                try {
                                    if (std::filesystem::exists(c)) {
                                        if (m_musicPlayer.load(c)) { m_aboutMusicLoaded = true; break; }
                                    }
                                } catch (...) {}
                            }
                            // If not found on disk, try embedded music
                            if (!m_aboutMusicLoaded) {
                                size_t elen = 0;
                                const unsigned char* em = embedded_resource_data("inneruniverse.ogg", &elen);
                                if (em && elen > 0) {
                                    if (m_musicPlayer.loadFromMemory(em, elen)) m_aboutMusicLoaded = true;
                                }
                            }
                            // Load LRC lyrics file alongside the music
                            if (!m_lrcLoaded) {
                                std::vector<std::string> lrcCandidates = {"../res/inneruniverse.lrc", "res/inneruniverse.lrc", "./res/inneruniverse.lrc"};
                                for (const auto &lp : lrcCandidates) {
                                    try {
                                        if (!std::filesystem::exists(lp)) continue;
                                        std::ifstream lf(lp);
                                        if (!lf.is_open()) continue;
                                        m_lrcLines.clear();
                                        std::string line;
                                        while (std::getline(lf, line)) {
                                            // Parse [mm:ss.xx] text
                                            if (line.size() < 10 || line[0] != '[') continue;
                                            size_t cb = line.find(']');
                                            if (cb == std::string::npos || cb < 9) continue;
                                            std::string ts = line.substr(1, cb - 1);
                                            std::string txt = (cb + 1 < line.size()) ? line.substr(cb + 1) : "";
                                            // Trim leading space from text
                                            if (!txt.empty() && txt[0] == ' ') txt = txt.substr(1);
                                            // Parse mm:ss.xx
                                            size_t colon = ts.find(':');
                                            size_t dot = ts.find('.');
                                            if (colon == std::string::npos) continue;
                                            int mins = std::stoi(ts.substr(0, colon));
                                            int secs = std::stoi(ts.substr(colon + 1, (dot != std::string::npos ? dot : ts.size()) - colon - 1));
                                            int hundredths = 0;
                                            if (dot != std::string::npos) hundredths = std::stoi(ts.substr(dot + 1));
                                            double t = mins * 60.0 + secs + hundredths / 100.0;
                                            m_lrcLines.push_back({t, txt});
                                        }
                                        m_lrcLoaded = true;
                                        break;
                                    } catch (...) {}
                                }
                            }
                        }
                    }
                } else {
                    m_ctrlAltSHoldActive = false;
                }
            }
            ImGui::Text("Under the highly permissive MIT license, see LICENSE for details.");

            // Music player UI for About dialog (placed to the right of image, under the text)
            if (m_musicPlayer.hasAudio()) {
                ImGui::Separator();
                // Playback controls
                if (!m_musicPlayer.isPlaying()) {
                    if (ImGui::Button("Play")) {
                        m_musicPlayer.play();
                    }
                } else {
                    if (ImGui::Button("Pause")) {
                        m_musicPlayer.pause();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop")) {
                    m_musicPlayer.stop();
                }

                // Display current lyric line to the right of the buttons
                if (!m_lrcLines.empty()) {
                    double pos_now = m_musicPlayer.getPositionSeconds();
                    // Find the last lyric line whose timestamp <= current position
                    std::string currentLyric;
                    for (int li = (int)m_lrcLines.size() - 1; li >= 0; --li) {
                        if (m_lrcLines[li].time <= pos_now) {
                            currentLyric = m_lrcLines[li].text;
                            break;
                        }
                    }
                    if (!currentLyric.empty()) {
                        // Reserve space for a small vector music-note icon to the left of the lyric (font-independent)
                        float iconH = ImGui::GetTextLineHeight();
                        float iconW = iconH * 0.7f;
                        ImGui::SameLine();
                        ImGui::Dummy(ImVec2(iconW, iconH));
                        ImVec2 iconPos = ImGui::GetItemRectMin();
                        ImDrawList* idl = ImGui::GetWindowDrawList();
                        ImU32 icoCol = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Text));
                        // head
                        ImVec2 headCenter = ImVec2(iconPos.x + iconW * 0.28f, iconPos.y + iconH * 0.58f);
                        idl->AddCircleFilled(headCenter, iconH * 0.18f, icoCol);
                        // stem
                        idl->AddRectFilled(ImVec2(iconPos.x + iconW * 0.48f, iconPos.y + iconH * 0.08f), ImVec2(iconPos.x + iconW * 0.52f, iconPos.y + iconH * 0.58f), icoCol);
                        // flag (triangle)
                        idl->AddTriangleFilled(ImVec2(iconPos.x + iconW * 0.52f, iconPos.y + iconH * 0.08f), ImVec2(iconPos.x + iconW * 0.86f, iconPos.y + iconH * 0.18f), ImVec2(iconPos.x + iconW * 0.52f, iconPos.y + iconH * 0.28f), icoCol);
                        ImGui::SameLine(0, 6.0f);
                        if (m_cyrillicFont) ImGui::PushFont(m_cyrillicFont);
                        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", currentLyric.c_str());
                        if (m_cyrillicFont) ImGui::PopFont();
                    }
                }

                // Timeline
                double dur = m_musicPlayer.getDurationSeconds();
                double pos = m_musicPlayer.getPositionSeconds();
                // If playback naturally reached the end, reset timeline to start
                if (dur > 0.0 && pos >= dur - 0.05) {
                    m_musicPlayer.stop();
                    pos = 0.0;
                }
                float frac = (dur > 0.0) ? (float)(pos / dur) : 0.0f;

                ImGui::Spacing();
                float availW = ImGui::GetContentRegionAvail().x;
                ImGui::PushID("about_timeline");
                ImGui::InvisibleButton("##timeline", ImVec2(availW, 14));
                ImVec2 tlMin = ImGui::GetItemRectMin();
                ImVec2 tlMax = ImGui::GetItemRectMax();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(tlMin, tlMax, ImColor(0.12f, 0.12f, 0.12f, 1.0f));
                dl->AddRectFilled(ImVec2(tlMin.x, tlMin.y), ImVec2(tlMin.x + frac * (tlMax.x - tlMin.x), tlMax.y), ImColor(0.2f, 0.5f, 0.8f, 1.0f));
                float cursorX = tlMin.x + frac * (tlMax.x - tlMin.x);
                dl->AddLine(ImVec2(cursorX, tlMin.y - 2), ImVec2(cursorX, tlMax.y + 2), ImColor(1.0f, 1.0f, 1.0f), 2.0f);

                // Click or drag to seek
                if (ImGui::IsItemActive() || ImGui::IsItemClicked()) {
                    if (ImGui::IsMouseDown(0)) {
                        float mx = ImGui::GetIO().MousePos.x;
                        float f = (mx - tlMin.x) / (tlMax.x - tlMin.x);
                        if (f < 0.0f) f = 0.0f;
                        if (f > 1.0f) f = 1.0f;
                        m_musicPlayer.seekSeconds(f * dur);
                    }
                }
                ImGui::PopID();

                // Time display (right aligned)
                std::string timeStr = secsToStr(pos) + " / " + secsToStr(dur);
                // If the song finished, stop and reset to the beginning
                if (m_musicPlayer.isPlaying() && dur > 0.0 && pos + 0.05 >= dur) {
                    m_musicPlayer.stop();
                    m_musicPlayer.seekSeconds(0.0);
                }
                float tw = ImGui::CalcTextSize(timeStr.c_str()).x;
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - tw);
                ImGui::TextUnformatted(timeStr.c_str());

                // Oscilloscope (smoothed and decimated for a higher effective FPS)
                ImGui::Spacing();
                ImGui::Text("Inner Universe - Origa - Ghost in the Shell: Standalone Complex OST");
                float scopeH = 80.0f;
                // Non-interactive scope area (display-only)
                ImGui::Dummy(ImVec2(availW, scopeH));
                ImVec2 scMin = ImGui::GetItemRectMin();
                ImVec2 scMax = ImGui::GetItemRectMax();
                dl->AddRectFilled(scMin, scMax, ImColor(0.02f, 0.02f, 0.02f, 1.0f));
                const auto& samps = m_musicPlayer.samples();
                if (!samps.empty()) {
                    size_t sr = (size_t)m_musicPlayer.sampleRate();
                    size_t n = samps.size();
                    size_t center = (size_t)std::lround(pos * sr);
                    if (center >= n) center = n ? (n - 1) : 0;
                    size_t window = std::min<size_t>((size_t)m_scopeWindowSamples, n);
                    size_t start = 0;
                    if (center >= window/2) {
                        start = center - window/2;
                        if (start + window > n) start = (n > window) ? (n - window) : 0;
                    }

                    float w = scMax.x - scMin.x;
                    float h = scMax.y - scMin.y;
                    float midY = scMin.y + h * 0.5f;

                    // Determine number of visual points (limit to available pixel width and a reasonable cap)
                    int pixelCount = (int)std::clamp((int)std::round(w), 32, 1024);
                    if (pixelCount > (int)window) pixelCount = (int)window;

                    // Resize previous buffer if needed
                    if ((int)m_scopePrev.size() != pixelCount) m_scopePrev.assign(pixelCount, 0.0f);

                    // Prepare polyline points
                    std::vector<ImVec2> pts;
                    pts.reserve(pixelCount);

                    // For each pixel, average samples in its source range and blend with previous frame
                    for (int px = 0; px < pixelCount; ++px) {
                        size_t src0 = start + (size_t)((uint64_t)px * window / pixelCount);
                        size_t src1 = start + (size_t)((uint64_t)(px + 1) * window / pixelCount);
                        if (src0 >= n) src0 = n ? (n - 1) : 0;
                        if (src1 > n) src1 = n;
                        // compute average (avoid extremely small loops)
                        float sum = 0.0f;
                        size_t count = 0;
                        for (size_t si = src0; si < src1; ++si) { sum += samps[si]; ++count; }
                        float avg = (count > 0) ? (sum / (float)count) : samps[src0];

                        // apply gain and clamp to avoid extreme scaling
                        avg *= m_scopeGain;
                        if (avg > 1.0f) avg = 1.0f;
                        if (avg < -1.0f) avg = -1.0f;

                        // apply temporal smoothing (exponential blend)
                        float sm = (m_scopePrev[px] * m_scopeSmoothAlpha) + (avg * (1.0f - m_scopeSmoothAlpha));
                        m_scopePrev[px] = sm;

                        float x = scMin.x + (float)px / (float)std::max(1, pixelCount - 1) * w;
                        // slightly amplify vertical scaling for more visual impact
                        float y = midY - sm * (h * 0.5f);
                        pts.emplace_back(x, y);
                    }

                    // Draw waveform with a faint glow and a brighter core for better visibility
                    if (!pts.empty()) {
                        // glow layer (soft, low alpha, thick)
                        dl->AddPolyline(pts.data(), (int)pts.size(), ImColor(0.3f, 0.8f, 0.3f, 0.12f), false, 6.0f);
                        // main waveform (brighter and slightly thicker)
                        dl->AddPolyline(pts.data(), (int)pts.size(), ImColor(0.46f, 0.98f, 0.46f, 1.0f), false, 2.5f);
                    }
                }
            }

            // Scrolling credits box (under the music player, right of the image)
            if (m_musicPlayer.hasAudio()) {
                ImGui::Spacing();
                ImGui::Separator();
                float creditsBoxH = 100.0f;
                float creditsW = ImGui::GetContentRegionAvail().x;
                ImGui::BeginChild("##credits_scroll", ImVec2(creditsW, creditsBoxH), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                const char* creditsLines[] = {
                    "",
                    "--- Credits ---",
                    "",
                    "- Duskar//Night -",
                    "Vibecoded Watercan",
                    "",
                    "Github Copilot (Raptor mini and Claude Opus models)",
                    "Give wheelchair assistance to people who can't code",
                    "to save their lives.",
                    "",
                    "--- Special Thanks ---",
                    "",
                    "-- Canvascord and legacies --",
                    "The order of appearance is not important.",
                    "",
                    "- TheSR -",
                    "For an extreme amount of patience, teachings and belief.",
                    "",
                    "- Manuel -",
                    "A small act of kindness set off a chain reaction",
                    "that changed history.",
                    "",
                    "- MrGatto -",
                    "For the mods and support during the historical Battle for LA.",
                    "",
                    "- Lukas -",
                    "Paved the way for what is to come.",
                    "",
                    "- Shinova -",
                    "The cute maid who drew the manga edition of The Broken Mind.",
                    "",
                    "- Zasha -",
                    "Original artist of The Broken Mind.",
                    "",
                    "ThatModdingCommunity - For having made history!",
                    "The first public-facing private server for Sky on the internet!",
                    "Much works still needs to be done.",
                    "But the end goal is visible on the horizon!",
                    "",
                    "",
                    "",
                    "Watercan is licensed under the permissive MIT License.",
                    "See LICENSE file in the distribution for details.",
                    "",
                    "",
                    "Thank you for using Watercan!",
                };
                int lineCount = sizeof(creditsLines) / sizeof(creditsLines[0]);
                float lineH = ImGui::GetTextLineHeightWithSpacing();
                // Reset point: when the last line has fully scrolled above the box
                float resetPoint = creditsBoxH + lineCount * lineH;

                m_creditsScrollY += ImGui::GetIO().DeltaTime * 25.0f;
                if (m_creditsScrollY >= resetPoint) m_creditsScrollY = 0.0f;

                float startY = creditsBoxH - m_creditsScrollY;
                ImVec2 boxMin = ImGui::GetCursorScreenPos();
                ImDrawList* cdl = ImGui::GetWindowDrawList();
                ImVec2 clipMin = boxMin;
                ImVec2 clipMax = ImVec2(boxMin.x + creditsW, boxMin.y + creditsBoxH);
                cdl->PushClipRect(clipMin, clipMax, true);
                for (int li = 0; li < lineCount; ++li) {
                    float y = boxMin.y + startY + li * lineH;
                    if (y + lineH < clipMin.y || y > clipMax.y) continue;
                    float textW = ImGui::CalcTextSize(creditsLines[li]).x;
                    float x = boxMin.x + (clipMax.x - clipMin.x - textW) * 0.5f;
                    ImVec4 col(0.8f, 0.85f, 0.95f, 1.0f);
                    if (std::string(creditsLines[li]).find("---") != std::string::npos)
                        col = ImVec4(1.0f, 0.85f, 0.4f, 1.0f);
                    cdl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(col), creditsLines[li]);
                }
                cdl->PopClipRect();

                ImGui::EndChild();
            }

            ImGui::EndGroup();
        } else {
            // If the image failed to load, show helpful message and path hints
            ImGui::TextColored(ImVec4(1.0f,0.6f,0.3f,1.0f), "About image not found.");
            ImGui::Text("Expected one of: ../res/TheBrokenClip.png, res/TheBrokenClip.png, ./res/TheBrokenClip.png");

            // Fallback: show the music player below when image is not present
            if (m_musicPlayer.hasAudio()) {
                ImGui::Separator();
                if (!m_musicPlayer.isPlaying()) {
                    if (ImGui::Button("Play")) m_musicPlayer.play();
                } else {
                    if (ImGui::Button("Pause")) m_musicPlayer.pause();
                }
                ImGui::SameLine(); if (ImGui::Button("Stop")) m_musicPlayer.stop();

                double dur = m_musicPlayer.getDurationSeconds();
                double pos = m_musicPlayer.getPositionSeconds();
                float frac = (dur > 0.0) ? (float)(pos / dur) : 0.0f;
                ImGui::Spacing();
                float availW = ImGui::GetContentRegionAvail().x;
                ImGui::PushID("about_timeline_fallback");
                ImGui::InvisibleButton("##timeline_fallback", ImVec2(availW, 14));
                ImVec2 tlMin = ImGui::GetItemRectMin();
                ImVec2 tlMax = ImGui::GetItemRectMax();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(tlMin, tlMax, ImColor(0.12f, 0.12f, 0.12f, 1.0f));
                dl->AddRectFilled(ImVec2(tlMin.x, tlMin.y), ImVec2(tlMin.x + frac * (tlMax.x - tlMin.x), tlMax.y), ImColor(0.2f, 0.5f, 0.8f, 1.0f));
                float cursorX = tlMin.x + frac * (tlMax.x - tlMin.x);
                dl->AddLine(ImVec2(cursorX, tlMin.y - 2), ImVec2(cursorX, tlMax.y + 2), ImColor(1.0f, 1.0f, 1.0f), 2.0f);
                if (ImGui::IsItemActive() || ImGui::IsItemClicked()) {
                    if (ImGui::IsMouseDown(0)) {
                        float mx = ImGui::GetIO().MousePos.x;
                        float f = (mx - tlMin.x) / (tlMax.x - tlMin.x);
                        if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
                        m_musicPlayer.seekSeconds(f * dur);
                    }
                }
                ImGui::PopID();

                std::string timeStr = secsToStr(pos) + " / " + secsToStr(dur);
                // If the song finished, stop and reset to the beginning
                if (m_musicPlayer.isPlaying() && dur > 0.0 && pos + 0.05 >= dur) {
                    m_musicPlayer.stop();
                    m_musicPlayer.seekSeconds(0.0);
                }
                float tw = ImGui::CalcTextSize(timeStr.c_str()).x;
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - tw);
                ImGui::TextUnformatted(timeStr.c_str());

                // Simple oscilloscope fallback (smaller, single line)
                ImGui::Spacing();
                ImGui::Text("Inner Universe - Origa - Ghost in the Shell: Standalone Complex OST");
                // Non-interactive fallback scope area (display-only)
                ImGui::Dummy(ImVec2(availW, 60));
                ImVec2 scMin = ImGui::GetItemRectMin(); ImVec2 scMax = ImGui::GetItemRectMax();
                dl->AddRectFilled(scMin, scMax, ImColor(0.02f, 0.02f, 0.02f, 1.0f));
                const auto& samps = m_musicPlayer.samples();
                if (!samps.empty()) {
                    size_t sr = (size_t)m_musicPlayer.sampleRate(); size_t n = samps.size();
                    size_t center = (size_t)std::lround(pos * sr); if (center >= n) center = n ? (n-1) : 0;
                    size_t window = std::min<size_t>((size_t)m_scopeWindowSamples, n);
                    size_t start = (center >= window/2) ? (center - window/2) : 0;
                    if (start + window > n) start = (n > window) ? (n - window) : 0;
                    float w = scMax.x - scMin.x; float h = scMax.y - scMin.y; float midY = scMin.y + h*0.5f;
                    int pixelCount = (int)std::clamp((int)std::round(w), 16, 512); if (pixelCount > (int)window) pixelCount = (int)window;
                    std::vector<ImVec2> pts; pts.reserve(pixelCount);
                    for (int px=0; px<pixelCount; ++px) {
                        size_t s0 = start + (size_t)((uint64_t)px*window/pixelCount);
                        size_t s1 = start + (size_t)((uint64_t)(px+1)*window/pixelCount);
                        if (s0>=n) s0 = n? (n-1):0; if (s1>n) s1 = n;
                        float sum = 0; size_t cnt=0; for (size_t si=s0; si<s1; ++si){sum+=samps[si];++cnt;}
                        float avg = cnt? (sum/(float)cnt) : samps[s0]; avg *= m_scopeGain; if (avg>1) avg=1;if(avg<-1)avg=-1;
                        float x = scMin.x + (float)px/(float)std::max(1,pixelCount-1)*w;
                        float y = midY - avg*(h*0.45f);
                        pts.emplace_back(x,y);
                    }
                    if (!pts.empty()) dl->AddPolyline(pts.data(), (int)pts.size(), ImColor(0.3f,0.8f,0.3f), false, 1.5f);
                }
            }
        }
        

        if (ImGui::Button("Close", ImVec2(120, 0))) {
            m_musicPlayer.stop();
            m_musicPlayer.unload();
            m_aboutMusicLoaded = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("License", ImVec2(120, 0))) {
            m_musicPlayer.stop();
            m_musicPlayer.unload();
            m_aboutMusicLoaded = false;
            m_showLicense = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        // Popup was closed (Escape key or clicked outside)
        m_musicPlayer.stop();
        m_musicPlayer.unload();
        m_aboutMusicLoaded = false;
    }
    
    // License window
    if (m_showLicense) {
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("MIT License", &m_showLicense)) {
            ImGui::TextWrapped(
                "MIT License\n\n"
                "Copyright (c) 2026 Dusk//Night, Copilot, the Sky:COTL modding community, and Canvascord's legacies.\n\n"
                "Permission is hereby granted, free of charge, to any person obtaining a copy "
                "of this software and associated documentation files (the \"Software\"), to deal "
                "in the Software without restriction, including without limitation the rights "
                "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
                "copies of the Software, and to permit persons to whom the Software is "
                "furnished to do so, subject to the following conditions:\n\n"
                "The above copyright notice and this permission notice shall be included in all "
                "copies or substantial portions of the Software.\n\n"
                "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
                "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
                "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
                "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
                "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
                "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
                "SOFTWARE."
            );
        }
        ImGui::End();
    }
}

void App::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                openFileDialog();
            }
            if (ImGui::MenuItem("Reload", "Ctrl+R", false, !m_currentFilePath.empty())) {
                loadFile(m_currentFilePath);
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, m_treeManager.isLoaded())) {
                saveFileDialog();
            }
            if (ImGui::MenuItem("Save single spirit...", nullptr, false, (m_treeManager.isLoaded() && !m_selectedSpirit.empty()))) {
                // Prepare single-spirit save and open save dialog
                m_internalSaveSingle = true;
                m_internalSaveSingleName = m_selectedSpirit;
                saveFileDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                m_running = false;
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("ID Finder")) {
                m_showFNVDialog = true;
                // initialize input buffer
                m_fnvNameBuf[0] = '\0';
            }
            if (ImGui::MenuItem("Color codes")) {
                // open the color codes modal
                m_showColorCodes = true;
                // Immediately load preview rally.json into preview tree so user sees it in the dialog
                std::vector<std::string> candidates = {"../res/rally.json", "res/rally.json", "./res/rally.json"};
                std::string found;
                for (const auto &c : candidates) {
                    try { if (std::filesystem::exists(c)) { found = c; break; } } catch(...){}
                }
                m_previewLoaded = false;
                if (!found.empty()) {
                    SpiritTreeManager tmp;
                    if (tmp.loadFromFile(found)) {
                        const auto &names = tmp.getSpiritNames();
                        if (!names.empty()) {
                            const SpiritTree* t = tmp.getTree(names[0]);
                            if (t) { m_previewTree = *t; m_previewLoaded = true; }
                        }
                    }
                }
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Sky planner (Open Web browser)")) {
                openUrl("https://sky-planner.com/");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About")) {
                m_showAbout = true;
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMenuBar();
    }
}

void App::renderSpiritList() {
    // Tabs for Spirits and Guides
    if (ImGui::BeginTabBar("SpiritListTabs")) {
        if (ImGui::BeginTabItem("Spirits")) {
            m_spiritListTab = 0;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Guides")) {
            m_spiritListTab = 1;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    
    // New/Delete spirit buttons
    ImGui::BeginGroup();
    ImGui::PushID("SpiritBtns");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
    static bool openNewSpirit = false;
    if (ImGui::Button("+ New")) {
        m_newSpiritName[0] = '\0';
        openNewSpirit = true;
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
    bool canDeleteSpirit = !m_selectedSpirit.empty();
    static bool openDeleteSpirit = false;
    if (!canDeleteSpirit) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    if (ImGui::Button("- Delete")) {
        openDeleteSpirit = true;
    }
    if (!canDeleteSpirit) ImGui::PopItemFlag();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    ImGui::EndGroup();

    // Open popups in the same frame as button click
    if (openNewSpirit) {
        ImGui::OpenPopup("NewSpiritPopup");
        openNewSpirit = false;
    }
    if (openDeleteSpirit) {
        ImGui::OpenPopup("DeleteSpiritPopup");
        openDeleteSpirit = false;
    }

    ImGui::SameLine();
    // Search filter
    ImGui::PushItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search...", m_searchFilter, sizeof(m_searchFilter));
    ImGui::PopItemWidth();
    
    ImGui::Spacing();

    // New spirit modal
    if (ImGui::BeginPopupModal("NewSpiritPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Create new spirit");
        ImGui::InputText("Name", m_newSpiritName, sizeof(m_newSpiritName));
        ImGui::Separator();
        if (ImGui::Button("Create")) {
            std::string newName(m_newSpiritName);
            if (!newName.empty()) {
                bool ok = m_treeManager.addSpirit(newName, m_selectedSpirit);
                if (ok) {
                    // Automatically add a root node for the new spirit
                    uint64_t rootId = m_treeManager.createNode(newName, 0.0f, 0.0f);
                    // Optionally set the node's name to the spirit name or "Root"
                    if (rootId != 0) {
                        nlohmann::json data;
                        data["nm"] = newName;
                        data["id"] = fnv1a32(newName);
                        data["dep"] = 0;
                        m_treeManager.updateNodeFromJson(newName, rootId, data.dump());
                    }
                    m_selectedSpirit = newName;
                    m_treeRenderer.resetView();
                    ImGui::CloseCurrentPopup();
                } else {
                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1.0f), "Failed: name exists or invalid");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Delete spirit modal (opened by OpenPopup("DeleteSpiritPopup"))
    if (ImGui::BeginPopupModal("DeleteSpiritPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Krill '%s''s spirit tree?", m_selectedSpirit.c_str());
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("Yes. Krill it.")) {
            if (!m_selectedSpirit.empty()) {
                m_treeManager.deleteSpirit(m_selectedSpirit);
                m_selectedSpirit.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        if (ImGui::Button("Spare it.")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    if (!m_treeManager.isLoaded()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No JSON file loaded.");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Use File > Open");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "To load a list of spirit trees.");
        return;
    }
    
    // Get the appropriate list based on tab
    const auto& items = (m_spiritListTab == 0) 
        ? m_treeManager.getSpiritNames() 
        : m_treeManager.getGuideNames();
    
    // Spirit/Guide list
    ImGui::BeginChild("SpiritListScroll", ImVec2(0, 0), false);
    
    std::string filterLower(m_searchFilter);
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
    
    for (const auto& spirit : items) {
        // Apply filter
        if (!filterLower.empty()) {
            std::string spiritLower = spirit;
            std::transform(spiritLower.begin(), spiritLower.end(), spiritLower.begin(), ::tolower);
            if (spiritLower.find(filterLower) == std::string::npos) {
                continue;
            }
        }
        
        size_t nodeCount = m_treeManager.getNodeCount(spirit);
        char label[256];
        snprintf(label, sizeof(label), "%s (%zu)", spirit.c_str(), nodeCount);
        
        // Travelling indicator to the left
        bool isTrav = m_treeManager.isTravellingSpirit(spirit);
        if (isTrav) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
            ImGui::Text("[TS]");
            if (ImGui::IsItemClicked()) {
                m_selectedSpirit = spirit;
                m_treeRenderer.resetView();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        bool isSelected = (m_selectedSpirit == spirit);
        if (ImGui::Selectable(label, isSelected)) {
            m_selectedSpirit = spirit;
            m_treeRenderer.resetView();
        }

    }
    
    ImGui::EndChild();
}

void App::renderTreeViewport() {
    // Header with spirit name on left, controls on right
    float windowWidth = ImGui::GetWindowWidth();
    float controlsWidth = 380.0f;  // Approximate width needed for controls
    


    // Right-aligned controls group
    ImGuiStyle &style = ImGui::GetStyle();
    float controlsStartX = windowWidth - controlsWidth - style.WindowPadding.x;
    // Ensure controls don't overlap the spirit label
    float minControlsX = ImGui::GetCursorPosX() + 20.0f;
    if (controlsStartX < minControlsX) controlsStartX = minControlsX;

    // Left-anchored spirit label (leave room for the controls at controlsStartX)
    {
        std::string spiritLabel;
        bool hasTravelling = false;
        if (!m_selectedSpirit.empty()) {
            const SpiritTree* tree = m_treeManager.getTree(m_selectedSpirit);
            if (tree) {
                char buf[256];
                snprintf(buf, sizeof(buf), "Spirit: %s  |  Nodes: %zu", m_selectedSpirit.c_str(), tree->nodes.size());
                spiritLabel = buf;
                hasTravelling = m_treeManager.isTravellingSpirit(m_selectedSpirit);
            } else {
                spiritLabel = "Tree Viewer";
            }
        } else {
            spiritLabel = "Tree Viewer";
        }

        float startX = ImGui::GetCursorPosX();
        float startY = ImGui::GetCursorPosY();
        float maxW = controlsStartX - startX - 8.0f; // leave small padding from controls
        if (maxW < 0.0f) maxW = 0.0f;
        float labelW = ImGui::CalcTextSize(spiritLabel.c_str()).x;

        // Ensure the spirit label stays on a single line (truncate with ellipsis if needed)
        if (labelW > maxW && maxW > 10.0f) {
            std::string truncated = spiritLabel;
            // Reserve 3 chars for ellipsis
            while (!truncated.empty() && ImGui::CalcTextSize((truncated + "...").c_str()).x > maxW) {
                truncated.pop_back();
            }
            truncated += "...";
            ImGui::TextUnformatted(truncated.c_str());
        } else {
            ImGui::TextUnformatted(spiritLabel.c_str());
        }

        if (hasTravelling) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[Travelling Spirit]");
        }

        // Top-of-viewer transient message (e.g., link failures) — place it on the same baseline as the spirit stats
        if (!m_treeMessage.empty() && std::chrono::steady_clock::now() < m_treeMessageUntil) {
            char msgBuf[512];
            const char* prefix = (m_treeMessageType == TreeMessageType::Warning) ? "WARNING: " : "ERROR: ";
            snprintf(msgBuf, sizeof(msgBuf), "%s%s", prefix, m_treeMessage.c_str());
            float msgW = ImGui::CalcTextSize(msgBuf).x;
            float desiredX = controlsStartX - msgW - 8.0f; // leave small padding from controls

            // Start on the same line as the spirit label
            ImGui::SameLine();
            // If we have enough room to push it to the right, set X; otherwise leave it after the label
            float curX = ImGui::GetCursorPosX();
            if (desiredX > curX + 4.0f) {
                ImGui::SetCursorPosX(desiredX);
            }
            // Make sure the message is aligned to the same baseline as the spirit label
            ImGui::SetCursorPosY(startY);
            if (m_treeMessageType == TreeMessageType::Warning) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "%s", msgBuf);
            } else {
                ImGui::TextColored(ImVec4(0.85f, 0.2f, 0.2f, 1.0f), "%s", msgBuf);
            }
        } else {
            // Clear expired message
            m_treeMessage.clear();
            m_treeMessageType = TreeMessageType::None;
        }
    }



    // Create mode button / Link mode / Delete confirm mode
    // Only position controls if we have a selected spirit
    if (!m_selectedSpirit.empty()) {
        ImGui::SameLine(controlsStartX);
        if (m_deleteConfirmMode) {
            // Delete confirmation buttons
            // Anchor delete confirmation buttons to the right edge
            ImVec2 winPosLocal = ImGui::GetWindowPos();
            ImVec2 winSizeLocal = ImGui::GetWindowSize();
            float btnPadding = style.WindowPadding.x;

            ImVec2 yesText = ImGui::CalcTextSize("Yes, Krill it.");
            float yesW = yesText.x + style.FramePadding.x * 2.0f + 12.0f;
            ImVec2 cancelText = ImGui::CalcTextSize("Spare it.");
            float cancelW = cancelText.x + style.FramePadding.x * 2.0f + 12.0f;
            float gap = 8.0f;
            float totalW = yesW + gap + cancelW;
            float startX = winPosLocal.x + winSizeLocal.x - btnPadding - totalW;

            ImGui::SetCursorScreenPos(ImVec2(startX, ImGui::GetCursorScreenPos().y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("Yes, Krill it.", ImVec2(yesW, 0))) {
                // Prepare for delete: capture parent and children so we can reparent and relayout
                uint64_t oldParent = 0;
                std::vector<uint64_t> oldChildren;
                if (!m_selectedSpirit.empty()) {
                    SpiritNode* delNode = m_treeManager.getNode(m_selectedSpirit, m_deleteNodeId);
                    if (delNode) {
                        oldParent = delNode->dep;
                        oldChildren = delNode->children;
                    }
                }

                // Start delete animation (capture node geometry/color before removing it)
                if (!m_selectedSpirit.empty()) {
                    SpiritNode* delNode = m_treeManager.getNode(m_selectedSpirit, m_deleteNodeId);
                    if (delNode) {
                        ImU32 color = m_treeRenderer.getNodeFillColorForNode(*delNode);
                        m_treeRenderer.startDeleteAnimation(m_deleteNodeId, delNode->x, delNode->y, color);
                        // Quick transient message to confirm animation trigger

                    }
                }

                // Sever children from the deleted node — they become free-floating roots
                // (a new independent subtree). Do NOT reparent to grandparent.
                if (!oldChildren.empty()) {
                    for (uint64_t cid : oldChildren) {
                        SpiritNode* ch = m_treeManager.getNode(m_selectedSpirit, cid);
                        if (ch) {
                            ch->dep = 0;
                            m_treeRenderer.setFreeFloating(cid);
                        }
                    }
                }

                // Actually delete the node
                m_treeManager.deleteNode(m_selectedSpirit, m_deleteNodeId);
                m_treeRenderer.clearFreeFloating(m_deleteNodeId);
                // Remove from selection set if present
                if (m_treeRenderer.isNodeSelected(m_deleteNodeId)) {
                    m_treeRenderer.removeNodeFromSelection(m_deleteNodeId);
                }

                // Rebuild relationships only (no layout recomputation)
                m_treeManager.rebuildTree(m_selectedSpirit);

                // Deletion may have reduced the child count of the old parent; update offending status
                updateOffendingStatusForParent(oldParent);

                m_deleteConfirmMode = false;
                m_deleteNodeId = 0;
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            if (ImGui::Button("Spare it.", ImVec2(cancelW, 0))) {
                m_deleteConfirmMode = false;
                m_deleteNodeId = 0;
            }
        } else if (m_createMode) {
            // Anchor red "Cancel" button to the right edge when in create mode
            ImVec2 winPosLocal = ImGui::GetWindowPos();
            ImVec2 winSizeLocal = ImGui::GetWindowSize();
            float btnPadding = style.WindowPadding.x;
            ImVec2 textSize = ImGui::CalcTextSize("Cancel");
            float btnW = textSize.x + style.FramePadding.x * 2.0f + 12.0f;
            float x = winPosLocal.x + winSizeLocal.x - btnPadding - btnW;
            ImGui::SetCursorScreenPos(ImVec2(x, ImGui::GetCursorScreenPos().y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button("Cancel", ImVec2(btnW, 0))) {
                m_createMode = false;
            }
            ImGui::PopStyleColor(4);
        } else if (m_reorderMode) {
            // Anchor "Cancel" to the right edge to match other modes
            ImVec2 winPosLocal = ImGui::GetWindowPos();
            ImVec2 winSizeLocal = ImGui::GetWindowSize();
            float btnPadding = style.WindowPadding.x;
            ImVec2 textSize = ImGui::CalcTextSize("Cancel");
            float btnW_local = textSize.x + style.FramePadding.x * 2.0f + 12.0f;
            float x_local = winPosLocal.x + winSizeLocal.x - btnPadding - btnW_local;
            ImGui::SetCursorScreenPos(ImVec2(x_local, ImGui::GetCursorScreenPos().y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.7f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.6f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("Cancel", ImVec2(btnW_local, 0))) {
                m_reorderMode = false;
                m_reorderNodeId = TreeRenderer::NO_NODE_ID;
                m_reorderSelectedLeafId = TreeRenderer::NO_NODE_ID;
                m_treeRenderer.clearHighlightedNodes();
                m_treeRenderer.clearSelectableNodes();
            }
            ImGui::PopStyleColor(4);

            // If a leaf has been selected, show NW/N/NE buttons inside an overlay window anchored to the viewport's top-right
            // Reorder insert buttons removed — keep Cancel visible only
            if (m_reorderSelectedLeafId != TreeRenderer::NO_NODE_ID) {
                // Buttons NW/N/NE intentionally removed per user request.
                // Reorder operations are performed by selecting and swapping highlighted leaves.
            }
        } else if (m_linkMode) {
            // Anchor "Cancel Link" to the right edge when in link mode
            ImVec2 winPosLocal = ImGui::GetWindowPos();
            ImVec2 winSizeLocal = ImGui::GetWindowSize();
            float btnPadding = style.WindowPadding.x;
            ImVec2 textSize = ImGui::CalcTextSize("Cancel Link");
            float btnW = textSize.x + style.FramePadding.x * 2.0f + 12.0f;
            float x = winPosLocal.x + winSizeLocal.x - btnPadding - btnW;
            ImGui::SetCursorScreenPos(ImVec2(x, ImGui::GetCursorScreenPos().y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.7f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.3f, 0.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button("Cancel Link", ImVec2(btnW, 0))) {
                m_linkMode = false;
                m_linkSourceNodeId = 0;
            }
            ImGui::PopStyleColor(4);
        } else {
            // Anchor "+ Add Node" to the right edge of the control area and add a "* Reshape" button to its left
            ImVec2 winPosLocal = ImGui::GetWindowPos();
            ImVec2 winSizeLocal = ImGui::GetWindowSize();
            float btnPadding = style.WindowPadding.x;
            ImVec2 addTextSize = ImGui::CalcTextSize("+ Add Node");
            float addBtnW = addTextSize.x + style.FramePadding.x * 2.0f + 12.0f;
            ImVec2 reshapeTextSize = ImGui::CalcTextSize("* Reshape");
            float reshapeBtnW = reshapeTextSize.x + style.FramePadding.x * 2.0f + 12.0f;
            float gap = 8.0f;

            // Add Node button at right edge and an "Arrows" toggle between Reshape and Add Node
            float addX = winPosLocal.x + winSizeLocal.x - btnPadding - addBtnW;
            float btnY = winPosLocal.y + btnPadding; // anchor to same top padding line

            // Determine arrows label and width
            bool arrowsShown = m_treeRenderer.showArrows();
            const char* arrowsLabel = arrowsShown ? "- Arrows" : "+ Arrows";
            ImVec2 arrowsTextSize = ImGui::CalcTextSize(arrowsLabel);
            float arrowsBtnW = arrowsTextSize.x + style.FramePadding.x * 2.0f + 12.0f;

            // Compute positions: addX, arrowsX, reshapeX
            float arrowsX = addX - gap - arrowsBtnW;
            float reshapeX = arrowsX - gap - reshapeBtnW;

            // Reshape button (left of arrows)
            ImGui::SetCursorScreenPos(ImVec2(reshapeX, btnY));
            bool canReshape = false;
            bool hasSnaps = false;
            bool needsRestore = false;
            bool hasOffending = false;
            if (!m_selectedSpirit.empty()) {
                canReshape = m_treeManager.needsReshape(m_selectedSpirit);
                hasSnaps = m_treeManager.hasSnaps(m_selectedSpirit);
                // If any parent has 4+ children, present Restore instead of Reshape
                hasOffending = !m_offendingParents.empty();
                needsRestore = hasSnaps || m_treeManager.needsRestore(m_selectedSpirit) || hasOffending;
                canReshape = canReshape || needsRestore;
            }
            ImGui::BeginDisabled(!canReshape);
            // When a restore is needed (snaps, missing originals, or custom nodes),
            // require a confirm step before performing a full reload.
            if (needsRestore) {
                if (!m_restoreConfirmPending) {
                    if (ImGui::Button("* Restore", ImVec2(reshapeBtnW, 0))) {
                        // Hide any current tree message immediately when entering Restore confirmation
                        clearTreeMessage();
                        m_restoreConfirmPending = true;
                    }
                } else {
                    // Confirmation UI: position the confirmation controls immediately left of the Arrows button
                    ImGuiStyle &styleLocal = ImGui::GetStyle();
                    float smallW = reshapeBtnW * 0.5f; // each button is half the reshape button
                    float textW = ImGui::CalcTextSize("Are you sure?").x;
                    float innerGap = 6.0f; // small spacing between text and buttons
                    // total width: text + gap + two buttons + a bit of padding
                    float totalW = textW + innerGap + (smallW * 2.0f) + (styleLocal.FramePadding.x * 2.0f);
                    // Place the controls so their right edge sits just left of the Arrows button
                    ImGui::SetCursorScreenPos(ImVec2(arrowsX - totalW - gap * 0.5f, btnY));
                    ImGui::Text("Are you sure?"); ImGui::SameLine();
                    if (ImGui::Button("Yes", ImVec2(smallW, 0))) {
                        // Collect old node positions and colors for animations
                        std::unordered_map<uint64_t, std::pair<float,float>> oldPos;
                        std::unordered_map<uint64_t, ImU32> oldColors;
                        {
                            const SpiritTree* tptr = m_treeManager.getTree(m_selectedSpirit);
                            if (tptr) {
                                for (const auto& n : tptr->nodes) {
                                    oldPos[n.id] = std::make_pair(n.x, n.y);
                                    oldColors[n.id] = m_treeRenderer.getNodeFillColorForNode(n);
                                }
                            }
                        }

                        // Force-reload the spirit from the originally loaded file.
                        // This discards ALL changes: custom nodes, reorders, snaps, renames.
                        bool reloaded = m_treeManager.reloadSpirit(m_selectedSpirit);

                        if (reloaded) {
                            // Clear all free-floating markers and traverse new tree
                            const SpiritTree* tptr = m_treeManager.getTree(m_selectedSpirit);
                            if (tptr) {
                                for (const auto& n : tptr->nodes) {
                                    m_treeRenderer.clearFreeFloating(n.id);

                                    // Compute animated shift from old position -> new position
                                    auto it = oldPos.find(n.id);
                                    if (it != oldPos.end()) {
                                        float dx = it->second.first  - n.x;
                                        float dy = it->second.second - n.y;
                                        float mag = sqrtf(dx*dx + dy*dy);
                                        if (mag > 0.5f) {
                                            m_treeRenderer.applyBaseShift(n.id, dx, dy);
                                            m_treeRenderer.triggerRestoreEffect(tptr, n.id);
                                        }
                                    }
                                    m_treeRenderer.thawNode(n.id);
                                }
                            }
                            // Trigger delete animation for nodes that were removed by the reload
                            std::unordered_set<uint64_t> newIds;
                            if (tptr) {
                                for (const auto& n : tptr->nodes) newIds.insert(n.id);
                            }
                            for (const auto& kv : oldPos) {
                                if (newIds.find(kv.first) == newIds.end()) {
                                    // Node was deleted — play delete animation at its old position
                                    ImU32 col = IM_COL32(180, 180, 180, 255);
                                    auto cit = oldColors.find(kv.first);
                                    if (cit != oldColors.end()) col = cit->second;
                                    m_treeRenderer.startDeleteAnimation(kv.first, kv.second.first, kv.second.second, col);
                                }
                                m_treeRenderer.clearFreeFloating(kv.first);
                            }
                            m_treeRenderer.suppressCollisions(2.0f);

                            // Clear any offending markers since reload restores original structure
                            for (auto &kv : m_parentOffendingChild) m_treeRenderer.clearOffendingNode(kv.second);
                            m_parentOffendingChild.clear();
                            m_offendingParents.clear();
                            // Clear any top-of-viewer message when user confirmed Restore
                            clearTreeMessage();
                        }

                        // Clear confirmation state
                        m_restoreConfirmPending = false;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("No", ImVec2(smallW, 0))) {
                        // User declined: re-show the persistent warning message
                        setTreeMessage("Tree structure will look off ingame", TreeMessageType::Warning, std::chrono::seconds(0));
                        m_restoreConfirmPending = false;
                    }
                }
            } else {
                // Normal Reshape behavior (no snaps)
                const char* reshapeLabel = "* Reshape";
                if (ImGui::Button(reshapeLabel, ImVec2(reshapeBtnW, 0))) {
                    std::unordered_map<uint64_t, std::pair<float,float>> shifts;
                    if (m_treeManager.reshapeTreeAndCollectShifts(m_selectedSpirit, &shifts)) {
                        const SpiritTree* tptr = m_treeManager.getTree(m_selectedSpirit);
                        for (const auto& kv : shifts) {
                            m_treeRenderer.applyBaseShift(kv.first, kv.second.first, kv.second.second);
                            float dx = kv.second.first; float dy = kv.second.second;
                            float mag = sqrtf(dx*dx + dy*dy);
                            if (mag > 1.5f && tptr) m_treeRenderer.triggerRestoreEffect(tptr, kv.first);
                        }
                        m_treeRenderer.suppressCollisions(2.0f);
                    }

                    if (!m_selectedSpirit.empty()) {
                        const SpiritTree* tptr = m_treeManager.getTree(m_selectedSpirit);
                        if (tptr) {
                            for (const auto& n : tptr->nodes) {
                                m_treeRenderer.thawNode(n.id);
                            }
                        }
                    }
                }
            }
            ImGui::EndDisabled();

            // Arrows toggle (default colors)
            ImGui::SetCursorScreenPos(ImVec2(arrowsX, btnY));
            if (ImGui::Button(arrowsLabel, ImVec2(arrowsBtnW, 0))) {
                m_treeRenderer.toggleShowArrows();
            }

            // Add Node button at right edge
            ImGui::SetCursorScreenPos(ImVec2(addX, btnY));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.9f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.7f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("+ Add Node", ImVec2(addBtnW, 0))) {
                m_createMode = true;
            }
            ImGui::PopStyleColor(4);
        }
    }
    
    // Reset View button removed — the canvas supports mouse-wheel zoom and panning
    ImGui::Separator();
    
    // Render tree
    const SpiritTree* tree = nullptr;
    if (!m_selectedSpirit.empty()) {
        tree = m_treeManager.getTree(m_selectedSpirit);
    }

    // Reorder mode: highlight all direct children of the reorder node and draw yellow border
    if (m_reorderMode && !m_selectedSpirit.empty()) {
        std::unordered_set<uint64_t> leaves;
        if (tree) {
            std::unordered_map<uint64_t, const SpiritNode*> idToNode;
            for (const auto& n : tree->nodes) idToNode[n.id] = &n;
            auto pit = idToNode.find(m_reorderNodeId);
            if (pit != idToNode.end()) {
                for (uint64_t cid : pit->second->children)
                    leaves.insert(cid);
            }
        }
        m_treeRenderer.setHighlightedNodes(leaves);
        m_treeRenderer.setSelectableNodes(leaves);

        // Renderer will draw the canvas-level yellow border; avoid drawing a full-window border here.
    }
    ImVec2 clickPos;
    clickPos.x = std::numeric_limits<float>::quiet_NaN(); // sentinel: set by renderer when canvas click occurs
    uint64_t linkTargetId = TreeRenderer::NO_NODE_ID;
    uint64_t rightClickedNodeId = TreeRenderer::NO_NODE_ID;
    // Optional drag release outputs (single-node) and continuous tree-drag outputs
    uint64_t dragReleasedId = TreeRenderer::NO_NODE_ID;
    ImVec2 dragFinalOffset = ImVec2(0.0f, 0.0f);
    uint64_t draggingTreeId = TreeRenderer::NO_NODE_ID;
    ImVec2 dragTreeDelta = ImVec2(0.0f, 0.0f);
    // Pass the user's type colors to the main renderer so changes apply immediately
    bool clicked = m_treeRenderer.render(tree, m_createMode, &clickPos, 
                                          m_linkMode, &linkTargetId, 
                                          &rightClickedNodeId, m_deleteConfirmMode, false, &m_typeColors,
                                          &dragReleasedId, &dragFinalOffset, &draggingTreeId, &dragTreeDelta, m_reorderMode);

    // If the user left-clicked on the canvas (clickPos set) in normal mode and the click
    // was on empty space (no node under the cursor), then deselect nodes.
    if (!m_createMode && !m_linkMode && !m_reorderMode && !std::isnan(clickPos.x) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mouseScreen = io.MousePos;
        uint64_t hit = m_treeRenderer.getNodeAtScreenPosition(tree, mouseScreen);
        if (hit == TreeRenderer::NO_NODE_ID) {
            m_treeRenderer.clearSelection();
        }
    }

    // Handle any pending snap events from the renderer (connection stretch -> snap)
    auto snaps = m_treeRenderer.popPendingSnaps();
    for (auto &s : snaps) {
        // Only apply snaps to the currently viewed spirit
        if (tree) {
            // Detach child from its parent (becomes free-floating)
            SpiritNode* childNode = m_treeManager.getNode(m_selectedSpirit, s.childId);
            if (childNode) {
                // Record snap mapping BEFORE we change the tree so we capture original index
                m_treeManager.recordSnap(m_selectedSpirit, s.childId, s.parentId);
                childNode->dep = 0;
                m_treeRenderer.setFreeFloating(s.childId);
                // Rebuild relationships so the parent no longer lists this child
                m_treeManager.rebuildTree(m_selectedSpirit);

                // Recompute layout only for the subtree rooted at the old parent (where the child was detached)
                std::unordered_map<uint64_t, std::pair<float,float>> shifts;
                if (s.parentId != 0) {
                    if (m_treeManager.layoutSubtreeAndCollectShifts(m_selectedSpirit, s.parentId, &shifts)) {
                        for (const auto& kv : shifts) {
                            m_treeRenderer.applyBaseShift(kv.first, kv.second.first, kv.second.second);
                            m_treeRenderer.thawNode(kv.first);
                        }
                    }
                    // After detaching, update offending status for the old parent
                    updateOffendingStatusForParent(s.parentId);
                } else {
                    // Fallback to full reshape if the parent was root
                    if (m_treeManager.reshapeTreeAndCollectShifts(m_selectedSpirit, &shifts)) {
                        for (const auto& kv : shifts) {
                            m_treeRenderer.applyBaseShift(kv.first, kv.second.first, kv.second.second);
                            m_treeRenderer.thawNode(kv.first);
                        }
                    }
                }

                m_treeRenderer.suppressCollisions(1.0f);
            }
        }
    }

    // Persistent duplicate-name detection: ensure offending nodes pulse until fixed and message stays
    if (!m_selectedSpirit.empty()) {
        auto dupIds = m_treeManager.getDuplicateNodeIds(m_selectedSpirit);
        if (!dupIds.empty()) {
            // Set persistent message (error)
            setTreeMessage("Node with same name found.", TreeMessageType::Error, std::chrono::seconds(0));
            // Mark nodes as red-pulsing
            for (uint64_t id : dupIds) m_treeRenderer.setNodeRedState(id, true);
            // Also ensure any nodes no longer duplicates are cleared
            const SpiritTree* tptr = m_treeManager.getTree(m_selectedSpirit);
            if (tptr) {
                for (const auto& n : tptr->nodes) {
                    if (dupIds.count(n.id) == 0) {
                    // Do not clear the red pulse if this node is currently flagged as offending
                    bool isOffendingChild = false;
                    for (const auto &kv : m_parentOffendingChild) { if (kv.second == n.id) { isOffendingChild = true; break; } }
                    if (!isOffendingChild) m_treeRenderer.setNodeRedState(n.id, false);
                }
                }
            }
        } else {
            // No duplicates: clear message if it matches our duplicate message and clear pulses
            if (m_treeMessage == "Node with same name found.") clearTreeMessageIfMatches("Node with same name found.");
            const SpiritTree* tptr = m_treeManager.getTree(m_selectedSpirit);
            if (tptr) {
                for (const auto& n : tptr->nodes) {
                    bool isOffendingChild = false;
                    for (const auto &kv : m_parentOffendingChild) { if (kv.second == n.id) { isOffendingChild = true; break; } }
                    if (!isOffendingChild) m_treeRenderer.setNodeRedState(n.id, false);
                }
            }
        }
    }

    // When in reorder mode, user clicks should select highlighted leaves only
    if (m_reorderMode && !std::isnan(clickPos.x) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mouseScreen = io.MousePos;
        uint64_t hit = m_treeRenderer.getNodeAtScreenPosition(tree, mouseScreen);
        if (hit != TreeRenderer::NO_NODE_ID) {
            // Recompute the reorder node's direct children
            std::unordered_set<uint64_t> leaves;
            if (tree) {
                std::unordered_map<uint64_t, const SpiritNode*> idToNode;
                for (const auto& n : tree->nodes) idToNode[n.id] = &n;
                auto pit = idToNode.find(m_reorderNodeId);
                if (pit != idToNode.end()) {
                    for (uint64_t cid : pit->second->children)
                        leaves.insert(cid);
                }
            }
            if (leaves.count(hit) > 0) {
                // If we do not yet have a selected leaf, select this one (enter second-step)
                if (m_reorderSelectedLeafId == TreeRenderer::NO_NODE_ID) {
                    m_reorderSelectedLeafId = hit;
                    // Reflect selection visibly in the tree
                    m_treeRenderer.setSelectedNodeId(hit);
                } else if (hit == m_reorderSelectedLeafId) {
                    // Clicking the already-selected leaf is a no-op (keeps it selected)
                    m_treeRenderer.setSelectedNodeId(hit);
                } else {
                    // We have a selected leaf and the user clicked a different highlighted leaf.
                    // Interpret this as a "swap positions" operation: exchange the two children
                    // inside the parent so the selected leaf takes the clicked node's slot.
                    SpiritNode* parent = m_treeManager.getNode(m_selectedSpirit, m_reorderNodeId);
                    if (parent) {
                        auto &children = parent->children;
                        auto itA = std::find(children.begin(), children.end(), m_reorderSelectedLeafId);
                        auto itB = std::find(children.begin(), children.end(), hit);
                        if (itA != children.end() && itB != children.end()) {
                            std::iter_swap(itA, itB);

                            // Reposition direct children and rigidly shift their subtrees
                            repositionChildrenOfNode(m_reorderNodeId);

                            // Briefly suppress collisions so the nodes settle
                            m_treeRenderer.suppressCollisions(2.0f);
                        }
                    }

                    // Exit reorder mode after swap
                    m_reorderMode = false;
                    m_reorderNodeId = TreeRenderer::NO_NODE_ID;
                    m_reorderSelectedLeafId = TreeRenderer::NO_NODE_ID;
                    m_treeRenderer.clearHighlightedNodes();
                    m_treeRenderer.clearSelectableNodes();
                }
            } else {
                setTreeMessage("Select a highlighted leaf node to reorder", TreeMessageType::Warning, std::chrono::seconds(3));
            }
        }
    }

    // If a drag just finished, persist the node's base offset so it remains at the dropped location
    if (dragReleasedId != TreeRenderer::NO_NODE_ID && !m_selectedSpirit.empty()) {
        // Apply the same drop delta to all selected nodes so group drags are committed together
        const auto& selected = m_treeRenderer.getSelectedNodeIds();
        if (!selected.empty()) {
            // Commit moved base positions for the whole selection
            std::unordered_set<uint64_t> affectedParents;
            for (uint64_t sid : selected) {
                SpiritNode* n = m_treeManager.getNode(m_selectedSpirit, sid);
                if (n) affectedParents.insert(n->dep);
                m_treeManager.moveNodeBase(m_selectedSpirit, sid, dragFinalOffset.x, dragFinalOffset.y);
                m_treeRenderer.clearNodeOffset(sid);
            }

            // After committing base positions, relayout each affected parent subtree so
            // attached nodes are repositioned via animated base shifts (no instantaneous snap)
            for (uint64_t pid : affectedParents) {
                if (pid == 0) continue;
                std::unordered_map<uint64_t, std::pair<float,float>> shifts;
                if (m_treeManager.layoutSubtreeAndCollectShifts(m_selectedSpirit, pid, &shifts)) {
                    for (const auto& kv : shifts) m_treeRenderer.applyBaseShift(kv.first, kv.second.first, kv.second.second);
                }
            }

            // End the group-drag so physics can resume for those nodes
            m_treeRenderer.endGroupDrag();
        } else {
            // Fallback: commit only the dragged node
            if (m_treeManager.moveNodeBase(m_selectedSpirit, dragReleasedId, dragFinalOffset.x, dragFinalOffset.y)) {
                m_treeRenderer.clearNodeOffset(dragReleasedId);
                // Re-layout subtree rooted at the released node and animate attached nodes smoothly
                std::unordered_map<uint64_t, std::pair<float,float>> shifts;
                if (m_treeManager.layoutSubtreeAndCollectShifts(m_selectedSpirit, dragReleasedId, &shifts)) {
                    for (const auto& kv : shifts) m_treeRenderer.applyBaseShift(kv.first, kv.second.first, kv.second.second);
                }
            }
        }
    }

    // Continuous tree-drag: move only the dragged node's subtree so each subtree
    // behaves as an independent unit. Descendants follow with distance-based falloff;
    // ancestor / sibling nodes stay fixed.
    if (draggingTreeId != TreeRenderer::NO_NODE_ID && !m_selectedSpirit.empty()) {
        std::unordered_set<uint64_t> movedIds;
        if (m_treeManager.moveSubtreeBase(m_selectedSpirit, draggingTreeId, dragTreeDelta.x, dragTreeDelta.y, &movedIds)) {
            const SpiritTree* treePtr = m_treeManager.getTree(m_selectedSpirit);
            if (treePtr) {
                // BFS *downward only* from dragged node to compute graph distances within the subtree
                std::unordered_map<uint64_t, const SpiritNode*> idToNode;
                for (const auto &n : treePtr->nodes) idToNode[n.id] = &n;

                std::unordered_map<uint64_t, int> dist;
                std::deque<uint64_t> q;
                dist[draggingTreeId] = 0;
                q.push_back(draggingTreeId);
                int maxDepth = 0;
                while (!q.empty()) {
                    uint64_t cur = q.front(); q.pop_front();
                    auto it = idToNode.find(cur);
                    if (it == idToNode.end()) continue;
                    // Only traverse children (descendants), not parent
                    for (uint64_t c : it->second->children) {
                        if (dist.find(c) == dist.end()) {
                            dist[c] = dist[cur] + 1;
                            maxDepth = std::max(maxDepth, dist[c]);
                            q.push_back(c);
                        }
                    }
                }

                // Apply distance-based falloff within the moved subtree
                 for (uint64_t id : movedIds) {
                     int d = 0;
                     auto itd = dist.find(id);
                     if (itd != dist.end()) d = itd->second;
                     // Normalize by the max depth so all nodes get a fraction < 1.0 and participate
                     float denom = (float)std::max(1, maxDepth + 1);
                     float factor = (float)d / denom; // 0 => full move, <1 => partial cancel (animated)
                     float sx = -factor * dragTreeDelta.x;
                     float sy = -factor * dragTreeDelta.y;
                     m_treeRenderer.applyBaseShift(id, sx, sy);
                 }
            }
        }
    }

    // If the canvas was clicked (clickPos set) and we have a node in clipboard, open canvas paste popup
    if (!std::isnan(clickPos.x) && m_hasClipboardNode && rightClickedNodeId == TreeRenderer::NO_NODE_ID) {
        m_canvasPasteX = clickPos.x;
        m_canvasPasteY = clickPos.y;
        ImGui::OpenPopup("CanvasPastePopup");
    }
    

    // Handle right-click context menu
    if (rightClickedNodeId != TreeRenderer::NO_NODE_ID) {
        m_contextMenuNodeId = rightClickedNodeId;
        m_showNodeContextMenu = true;
        ImGui::OpenPopup("NodeContextMenu");
    } else if (!std::isnan(clickPos.x) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        // Right-click on empty canvas should also open the context menu; represent canvas with NO_NODE_ID
        m_contextMenuNodeId = TreeRenderer::NO_NODE_ID;
        m_showNodeContextMenu = true;
        ImGui::OpenPopup("NodeContextMenu");
    }
    
    // Render context menu
    if (ImGui::BeginPopup("NodeContextMenu")) {
        if (ImGui::MenuItem("Copy Node")) {
            // Get the node and serialize it
            if (!m_selectedSpirit.empty()) {
                SpiritNode* node = m_treeManager.getNode(m_selectedSpirit, m_contextMenuNodeId);
                if (node) {
                    m_clipboardNodeJson = SpiritTreeManager::nodeToJson(*node);
                    m_hasClipboardNode = true;
                }
            }
        }
        
        if (ImGui::MenuItem("Paste Node", nullptr, false, m_hasClipboardNode)) {
            // Parse and create a new node from clipboard
            if (!m_selectedSpirit.empty() && m_hasClipboardNode) {
                try {
                    nlohmann::json data = nlohmann::json::parse(m_clipboardNodeJson);
                    // Generate new unique name
                    std::string baseName = data.value("nm", "pasted_node");
                    std::string nodeName = baseName + "_copy";
                    data["nm"] = nodeName;
                    data["id"] = fnv1a32(nodeName);
                    data["dep"] = 0;  // Free-floating, no parent
                    
                    // Determine paste position: if context menu was on a node, offset to the right; otherwise use canvas paste coords
                    SpiritNode* sourceNode = m_treeManager.getNode(m_selectedSpirit, m_contextMenuNodeId);
                    float x = 0, y = 0;
                    if (sourceNode) {
                        x = sourceNode->x + 80;  // Offset to the right
                        y = sourceNode->y;
                    } else if (!std::isnan(m_canvasPasteX)) {
                        x = m_canvasPasteX;
                        y = m_canvasPasteY;
                        // Reset canvas paste position
                        m_canvasPasteX = std::numeric_limits<float>::quiet_NaN();
                        m_canvasPasteY = std::numeric_limits<float>::quiet_NaN();
                    }
                    
                    uint64_t newNodeId = m_treeManager.createNode(m_selectedSpirit, x, y);
                    if (newNodeId != 0) {
                        // Update the new node with clipboard data (except position)
                        data.erase("id");  // Let updateNodeFromJson handle it
                        m_treeManager.updateNodeFromJson(m_selectedSpirit, newNodeId, data.dump());
                        m_treeRenderer.setSelectedNodeId(newNodeId);
                        m_treeRenderer.setFreeFloating(newNodeId);
                    }
                } catch (...) {
                    // Ignore parse errors
                }
            }
        }
        
        ImGui::Separator();
        
        // Show Link Node for any existing node (allow re-parenting)
        bool canLink = false;
        if (!m_selectedSpirit.empty()) {
            SpiritNode* node = m_treeManager.getNode(m_selectedSpirit, m_contextMenuNodeId);
            if (node) {
                canLink = true;
            }
        }
        
        // Link Node option
        if (ImGui::MenuItem("Link Node...", nullptr, false, canLink)) {
            m_linkMode = true;
            m_linkSourceNodeId = m_contextMenuNodeId;
        }

        if (ImGui::BeginPopupModal("CanvasPastePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            // Position the popup at mouse position when opened
            ImVec2 mp = ImGui::GetMousePos();
            ImGui::SetWindowPos(mp);
            if (m_hasClipboardNode) {
                if (ImGui::MenuItem("Paste Node Here")) {
                    // Paste at m_canvasPasteX/Y
                    try {
                        nlohmann::json data = nlohmann::json::parse(m_clipboardNodeJson);
                        std::string baseName = data.value("nm", "pasted_node");
                        std::string nodeName = baseName + "_copy";
                        data["nm"] = nodeName;
                        data["id"] = fnv1a32(nodeName);
                        data["dep"] = 0; // free-floating

                        uint64_t newNodeId = m_treeManager.createNode(m_selectedSpirit, m_canvasPasteX, m_canvasPasteY);
                        if (newNodeId != 0) {
                            data.erase("id");
                            m_treeManager.updateNodeFromJson(m_selectedSpirit, newNodeId, data.dump());
                            m_treeRenderer.setSelectedNodeId(newNodeId);
                            m_treeRenderer.setFreeFloating(newNodeId);
                        }
                    } catch (...) {}
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        
        // Clear Links - only show for nodes that have a parent (dep != 0)
        bool canClearLinks = false;
        if (!m_selectedSpirit.empty()) {
            SpiritNode* node = m_treeManager.getNode(m_selectedSpirit, m_contextMenuNodeId);
            if (node && node->dep != 0) {
                canClearLinks = true;
            }
        }

        // Clear Links option
        if (ImGui::MenuItem("Clear Links", nullptr, false, canClearLinks)) {
            SpiritNode* node = m_treeManager.getNode(m_selectedSpirit, m_contextMenuNodeId);
            if (node) {
                // Record this detach as a snap so Restore can reattach it later
                uint64_t oldParent = node->dep;

                // Record snap mapping BEFORE changing the tree so we capture the original index
                m_treeManager.recordSnap(m_selectedSpirit, m_contextMenuNodeId, oldParent);

                // Remove parent dependency and mark as free-floating (like a snap)
                node->dep = 0;
                m_treeRenderer.setFreeFloating(m_contextMenuNodeId);

                // Rebuild relationships so the parent no longer lists this node as a child
                m_treeManager.rebuildTree(m_selectedSpirit);

                // Recompute layout just for the subtree rooted at the old parent (less disruptive than full reshape)
                std::unordered_map<uint64_t, std::pair<float,float>> shifts;
                if (oldParent != 0) {
                    if (m_treeManager.layoutSubtreeAndCollectShifts(m_selectedSpirit, oldParent, &shifts)) {
                        for (const auto& kv : shifts) {
                            m_treeRenderer.applyBaseShift(kv.first, kv.second.first, kv.second.second);
                            // Thaw moved nodes so they participate in physics
                            m_treeRenderer.thawNode(kv.first);
                        }
                    }
                    // After clearing a link, the old parent may no longer be offending
                    updateOffendingStatusForParent(oldParent);
                } else {
                    // If oldParent was root (0) fallback to global reshape
                    if (m_treeManager.reshapeTreeAndCollectShifts(m_selectedSpirit, &shifts)) {
                        for (const auto& kv : shifts) {
                            m_treeRenderer.applyBaseShift(kv.first, kv.second.first, kv.second.second);
                            m_treeRenderer.thawNode(kv.first);
                        }
                    }
                }

                // Give a short window to suppress collisions so nodes can settle
                m_treeRenderer.suppressCollisions(1.0f);
            }
        }

        // Reorder option - enter reorder mode for this node to reparent leaves
        bool canReorder = false;
        if (canLink) {
            const auto& sel = m_treeRenderer.getSelectedNodeIds();
            if (sel.size() == 1 && m_treeRenderer.getSelectedNodeId() == m_contextMenuNodeId) canReorder = true;
        }
        // If the context node has no children, disable reorder
        if (canReorder && !m_selectedSpirit.empty()) {
            const SpiritNode* ctx = m_treeManager.getNode(m_selectedSpirit, m_contextMenuNodeId);
            if (!ctx || ctx->children.empty()) canReorder = false;
        }
        if (ImGui::MenuItem("Reorder", nullptr, false, canReorder)) {
            // Enter reorder mode for the context node
            m_reorderMode = true;
            m_reorderNodeId = m_contextMenuNodeId;
            m_reorderSelectedLeafId = TreeRenderer::NO_NODE_ID;
            // Ensure other modes are off
            m_linkMode = false; m_createMode = false; m_deleteConfirmMode = false;

            // Compute the reorder node's direct children only
            std::unordered_set<uint64_t> leaves;
            const SpiritTree* tree = m_treeManager.getTree(m_selectedSpirit);
            if (tree) {
                std::unordered_map<uint64_t, const SpiritNode*> idToNode;
                for (const auto& n : tree->nodes) idToNode[n.id] = &n;
                auto pit = idToNode.find(m_reorderNodeId);
                if (pit != idToNode.end()) {
                    for (uint64_t cid : pit->second->children)
                        leaves.insert(cid);
                }
            }
            m_treeRenderer.setHighlightedNodes(leaves);
            m_treeRenderer.setSelectableNodes(leaves);
        }
        
        ImGui::Separator();
        
        // Delete node - danger option with red color
        bool canDelete = (m_treeRenderer.getSelectedNodeId() != TreeRenderer::NO_NODE_ID);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        if (ImGui::MenuItem("Delete Node", nullptr, false, canDelete)) {
            // Enter delete confirmation mode
            m_deleteConfirmMode = true;
            m_deleteNodeId = m_contextMenuNodeId;
        }
        ImGui::PopStyleColor();
        
        ImGui::EndPopup();
    }
    
    // Handle node creation
    if (m_createMode && clicked && !m_selectedSpirit.empty()) {
        uint64_t newNodeId = m_treeManager.createNode(m_selectedSpirit, clickPos.x, clickPos.y);
        if (newNodeId != 0) {
            // Select the newly created node
            m_treeRenderer.setSelectedNodeId(newNodeId);
            // Mark as free-floating so it stays where placed
            m_treeRenderer.setFreeFloating(newNodeId);
        }
        // Exit create mode
        m_createMode = false;
    }
    
    // Handle link mode - link source node to target (target becomes parent)
    if (m_linkMode && clicked && linkTargetId != TreeRenderer::NO_NODE_ID && !m_selectedSpirit.empty()) {
        bool ok = performLinkToTarget(linkTargetId);
        if (!ok) {
            // performLinkToTarget already set a helpful message for the user in some cases
            if (m_treeMessage.empty()) {
                setTreeMessage("Link failed: invalid source or target", TreeMessageType::Error, std::chrono::seconds(3));
            }
        }

        // Exit link mode regardless of success so UI isn't stuck
        m_linkMode = false;
        m_linkSourceNodeId = TreeRenderer::NO_NODE_ID;
    }
}



// Helper: perform linking a source node to a given target. Returns true if link applied.
bool App::performLinkToTarget(uint64_t targetId) {
    if (m_selectedSpirit.empty()) return false;
    if (m_linkSourceNodeId == TreeRenderer::NO_NODE_ID) return false;
    if (targetId == TreeRenderer::NO_NODE_ID) return false;
    if (targetId == m_linkSourceNodeId) return false; // no self-linking

    SpiritNode* sourceNode = m_treeManager.getNode(m_selectedSpirit, m_linkSourceNodeId);
    SpiritNode* targetNode = m_treeManager.getNode(m_selectedSpirit, targetId);
    if (!sourceNode || !targetNode) return false;

    // Disallow linking a new node to another new node; new nodes must attach to main tree nodes
    if (sourceNode->isNew && targetNode->isNew) {
        setTreeMessage("Only one tree per spirits! Link to main or sub tree.", TreeMessageType::Error, std::chrono::seconds(4));
        return false;
    }

    // Perform linking: set dependency, clear flags
    sourceNode->dep = targetId;
    if (sourceNode->isNew && !targetNode->isNew) sourceNode->isNew = false;

    // Clear free-floating and any recorded snap info since the user manually reattached it
    m_treeRenderer.clearFreeFloating(m_linkSourceNodeId);
    m_treeManager.clearSnap(m_selectedSpirit, m_linkSourceNodeId);

    // Rebuild tree relationships so parent->children reflects current deps
    m_treeManager.rebuildTree(m_selectedSpirit);

    // Smart insertion: compute desired child index based on source node position so
    // linking to an occupied slot will shift existing children as appropriate.
    SpiritNode* parent = m_treeManager.getNode(m_selectedSpirit, targetId);
    if (parent) {
        // Remove any existing instance of the source id from parent's children
        parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), m_linkSourceNodeId), parent->children.end());

        // Determine insertion index using deterministic cardinal rules:
        //  - 0 children: insert at 0
        //  - 1 child (currently N): push the existing to NW and insert new at N (index 1)
        //  - 2+ children: append as NE
        size_t childCountBefore = parent->children.size();
        size_t insertIdx = childCountBefore; // default append
        if (childCountBefore == 0) insertIdx = 0;
        else if (childCountBefore == 1) insertIdx = 1; // push existing to NW, new becomes N
        else insertIdx = childCountBefore; // append as NE (or after existing children)

        if (insertIdx > parent->children.size()) insertIdx = parent->children.size();
        parent->children.insert(parent->children.begin() + insertIdx, m_linkSourceNodeId);
    }

    // Reposition the direct children of the target to their cardinal slots.
    // Each child's descendant subtree is rigidly shifted by the same delta.
    repositionChildrenOfNode(targetId);

    // Also thaw the source node itself so it participates in physics
    m_treeRenderer.thawNode(m_linkSourceNodeId);

    // Check for offending parent (too many children) and warn, but allow the link
    updateOffendingStatusForParent(targetId, m_linkSourceNodeId);
    // Ensure a persistent message is set if threshold reached (defensive)
    SpiritNode* parentCheck = m_treeManager.getNode(m_selectedSpirit, targetId);
    if (parentCheck && parentCheck->children.size() >= 4) {
        setTreeMessage("Tree structure will look off ingame", TreeMessageType::Warning, std::chrono::seconds(0));
    }

    m_treeRenderer.suppressCollisions(2.0f);
    return true;
}

// ---------------------------------------------------------------------------
// Reposition ONLY the direct children of `parentId` to their cardinal slots
// (NW / N / NE etc.) and rigidly shift each child's entire descendant subtree
// by the same delta so manually-arranged deeper nodes are preserved.
// ---------------------------------------------------------------------------
void App::repositionChildrenOfNode(uint64_t parentId) {
    SpiritNode* parent = m_treeManager.getNode(m_selectedSpirit, parentId);
    if (!parent) return;

    const float nodeSpacingY = 100.0f;
    const float nodeSpacingX = 120.0f;
    const float diagonalYOffset = -25.0f;

    size_t childCount = parent->children.size();
    for (size_t i = 0; i < childCount; ++i) {
        SpiritNode* child = m_treeManager.getNode(m_selectedSpirit, parent->children[i]);
        if (!child) continue;

        float newX = parent->x;
        float newY = parent->y + nodeSpacingY;

        if (childCount == 1) {
            newX = parent->x;
        } else if (childCount == 2) {
            if (i == 0) { newX = parent->x - nodeSpacingX; newY += diagonalYOffset; } // NW
            else        { newX = parent->x; }                                          // N
        } else if (childCount == 3) {
            if (i == 0)      { newX = parent->x - nodeSpacingX; newY += diagonalYOffset; } // NW
            else if (i == 1) { newX = parent->x; }                                          // N
            else             { newX = parent->x + nodeSpacingX; newY += diagonalYOffset; }   // NE
        } else {
            float startX = parent->x - nodeSpacingX * (float(childCount - 1) * 0.5f);
            newX = startX + (float)i * nodeSpacingX;
            float centerIndex = (childCount - 1) * 0.5f;
            if (std::fabs((float)i - centerIndex) > 0.01f) newY += diagonalYOffset;
        }

        float dx = child->x - newX;  // old - new
        float dy = child->y - newY;

        // Skip children that are already at the correct position
        if (std::fabs(dx) < 0.01f && std::fabs(dy) < 0.01f) continue;

        // Move the child to its new cardinal position
        child->x = newX;
        child->y = newY;
        m_treeRenderer.applyBaseShift(child->id, dx, dy);
        m_treeRenderer.thawNode(child->id);

        // Rigidly shift ALL descendants of this child by the same delta
        // so their relative arrangement is preserved (they "follow along").
        std::vector<uint64_t> stack;
        for (uint64_t gc : child->children) stack.push_back(gc);
        while (!stack.empty()) {
            uint64_t cur = stack.back(); stack.pop_back();
            SpiritNode* desc = m_treeManager.getNode(m_selectedSpirit, cur);
            if (!desc) continue;
            // Move by the same vector as the parent child: (newX - oldX) = -dx
            desc->x -= dx;
            desc->y -= dy;
            m_treeRenderer.applyBaseShift(desc->id, dx, dy);
            m_treeRenderer.thawNode(desc->id);
            for (uint64_t gc2 : desc->children) stack.push_back(gc2);
        }
    }
}

// Update offending parent status (too many children > 4).
void App::updateOffendingStatusForParent(uint64_t parentId, uint64_t offendingChildId) {
    if (parentId == 0 || m_selectedSpirit.empty()) return;
    SpiritNode* parent = m_treeManager.getNode(m_selectedSpirit, parentId);
    if (!parent) return;

    size_t childCount = parent->children.size();
    const std::string warn = "Tree structure will look off ingame";

    if (childCount >= 4) {
        // Determine offending child
        uint64_t offending = offendingChildId;
        // Prefer the provided offending child, otherwise reuse previous, otherwise pick the newest (last child)
        auto pit = m_parentOffendingChild.find(parentId);
        if (offending == 0 && pit != m_parentOffendingChild.end()) offending = pit->second;
        if (offending == 0 && !parent->children.empty()) offending = parent->children.back();

        // Mark offending parent and offending child node
        m_offendingParents.insert(parentId);
        m_parentOffendingChild[parentId] = offending;
        m_treeRenderer.setOffendingNode(offending);
        // Also pulse the node red ring for visibility
        m_treeRenderer.setNodeRedState(offending, true);
        m_treeMessage = warn;
        m_treeMessageUntil = std::chrono::steady_clock::time_point::max();
    } else {
        // Clear offending child if present
        auto pit = m_parentOffendingChild.find(parentId);
        if (pit != m_parentOffendingChild.end()) {
            m_treeRenderer.clearOffendingNode(pit->second);
            // Also clear red pulse state
            m_treeRenderer.setNodeRedState(pit->second, false);
            m_parentOffendingChild.erase(pit);
        }
        // Clear offending parent set
        m_offendingParents.erase(parentId);
        // If no more offending parents, clear message
        if (m_offendingParents.empty() && m_treeMessage == warn) {
            m_treeMessage.clear();
        }
    }
}

void App::setTreeMessage(const std::string& msg, App::TreeMessageType type, std::chrono::seconds duration) {
    m_treeMessage = msg;
    m_treeMessageType = type;
    if (duration.count() == 0) {
        m_treeMessageUntil = std::chrono::steady_clock::time_point::max();
    } else {
        m_treeMessageUntil = std::chrono::steady_clock::now() + duration;
    }
}

void App::clearTreeMessageIfMatches(const std::string& msg) {
    if (m_treeMessage == msg) {
        m_treeMessage.clear();
        m_treeMessageType = TreeMessageType::None;
        m_treeMessageUntil = std::chrono::steady_clock::now();
    }
}

void App::clearTreeMessage() {
    m_treeMessage.clear();
    m_treeMessageType = TreeMessageType::None;
    m_treeMessageUntil = std::chrono::steady_clock::now();
}

// Helper: perform insertion of the currently selected leaf into the reorder node at the given index
void App::performReorderInsert(size_t index) {
    if (m_selectedSpirit.empty()) return;
    if (m_reorderNodeId == TreeRenderer::NO_NODE_ID) return;
    if (m_reorderSelectedLeafId == TreeRenderer::NO_NODE_ID) return;

    SpiritNode* leaf = m_treeManager.getNode(m_selectedSpirit, m_reorderSelectedLeafId);
    if (!leaf) return;

    // Make the leaf a child of the reorder target
    leaf->dep = m_reorderNodeId;
    // Clear any recorded snap info since the user manually reattached
    m_treeManager.clearSnap(m_selectedSpirit, m_reorderSelectedLeafId);
    m_treeManager.rebuildTree(m_selectedSpirit);

    SpiritNode* parent = m_treeManager.getNode(m_selectedSpirit, m_reorderNodeId);
    if (parent) {
        // Remove from any existing position
        parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), m_reorderSelectedLeafId), parent->children.end());
        // Clamp index to [0, size]
        size_t idx = std::min(index, parent->children.size());
        parent->children.insert(parent->children.begin() + idx, m_reorderSelectedLeafId);

        // Reposition direct children and rigidly shift their subtrees
        repositionChildrenOfNode(m_reorderNodeId);
    }

    // Briefly suppress collisions and exit reorder mode
    m_treeRenderer.suppressCollisions(2.0f);
    m_reorderMode = false;
    m_reorderNodeId = TreeRenderer::NO_NODE_ID;
    m_reorderSelectedLeafId = TreeRenderer::NO_NODE_ID;
    m_treeRenderer.clearHighlightedNodes();
    m_treeRenderer.clearSelectableNodes();
}

void App::renderNodeDetails() {
    // Add padding
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("NodeDetailsContent", ImVec2(-8.0f, 0), false);
    
    // Panel title with Fix ID button if needed
    uint64_t selectedNodeId = m_treeRenderer.getSelectedNodeId();
    size_t selectedCount = m_treeRenderer.getSelectedNodeIds().size();
    bool showFixButton = false;
    uint32_t expectedId = 0;
    
    if (selectedNodeId != TreeRenderer::NO_NODE_ID && !m_selectedSpirit.empty()) {
        const SpiritTree* tree = m_treeManager.getTree(m_selectedSpirit);
        if (tree) {
            for (const auto& node : tree->nodes) {
                if (node.id == selectedNodeId) {
                    expectedId = fnv1a32(node.name);
                    showFixButton = (node.id != expectedId);
                    break;
                }
            }
        }
    }
    
    ImGui::Text("Node attribute viewer");
    // Note: The Fix ID control is shown with the main ID editor below to avoid duplicate
    // ImGui item IDs when multiple buttons with the same label appear in the same window.
    ImGui::Separator();
    
    if (selectedNodeId == TreeRenderer::NO_NODE_ID) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No node selected");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Left-click a node to");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "view its details");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }
    
    // Find the selected node
    const SpiritTree* tree = nullptr;
    if (!m_selectedSpirit.empty()) {
        tree = m_treeManager.getTree(m_selectedSpirit);
    }
    
    if (!tree) {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }
    
    SpiritNode* selectedNode = m_treeManager.getNode(m_selectedSpirit, selectedNodeId);
    if (!selectedNode) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Node not found");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }
    
    ImGui::Spacing();
    
    // Check if ID matches FNV-1a hash of name (reuse expectedId from earlier)
    uint32_t nodeExpectedId = fnv1a32(selectedNode->name);
    bool idMatches = (selectedNode->id == nodeExpectedId);
    
    ImVec4 matchColor = idMatches ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    
    // Track whether any attribute changed in this panel
    bool attrChanged = false;

    
    // Display node attributes in JSON order: ap, cst, ctyp, dep, id, nm, spirit, typ

    // ap - Adventure Pass (editable checkbox)
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Adventure Pass (ap):");
    bool ap = selectedNode->isAdventurePass;
    if (ImGui::Checkbox("##ap", &ap)) {
        selectedNode->isAdventurePass = ap;
        attrChanged = true;
    }
    ImGui::SameLine();
    ImGui::Text(ap ? "Yes" : "No");
    ImGui::Spacing();
    
    // cst - Cost (editable integer with mouse-wheel support)
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Cost (cst):");
    int cost = selectedNode->cost;
    ImGui::PushItemWidth(120.0f);
    if (ImGui::InputInt("##cst", &cost)) {
        selectedNode->cost = cost;
        attrChanged = true;
    }
    // Mouse wheel changes while hovering the widget; Shift = larger step
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            int step = io.KeyShift ? 10 : 1;
            int delta = (int)std::copysignf(1.0f, io.MouseWheel) * step;
            cost += delta;
            selectedNode->cost = cost;
            attrChanged = true;
        }
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    
    // ctyp - Currency Type (dropdown from known ctyp values)
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Currency Type (ctyp):");
    std::vector<std::string> ctyps;
    for (const auto& s : m_treeManager.getSpiritNames()) {
        const SpiritTree* t = m_treeManager.getTree(s);
        if (!t) continue;
        for (const auto& n : t->nodes) ctyps.push_back(n.costType);
    }
    for (const auto& s : m_treeManager.getGuideNames()) {
        const SpiritTree* t = m_treeManager.getTree(s);
        if (!t) continue;
        for (const auto& n : t->nodes) ctyps.push_back(n.costType);
    }
    std::sort(ctyps.begin(), ctyps.end());
    ctyps.erase(std::unique(ctyps.begin(), ctyps.end()), ctyps.end());
    int currentCtypIndex = 0;
    for (size_t i = 0; i < ctyps.size(); ++i) if (ctyps[i] == selectedNode->costType) currentCtypIndex = (int)i;

    if (m_ctypCustomInput) {
        // Inline text field replaces dropdown when toggle is on
        if (ImGui::InputText("##ctyp_inline", m_customCtypBuf, sizeof(m_customCtypBuf))) {
            selectedNode->costType = std::string(m_customCtypBuf);
            attrChanged = true;
        }
    } else {
        if (!ctyps.empty()) {
            if (ImGui::BeginCombo("##ctyp", selectedNode->costType.c_str())) {
                for (size_t i = 0; i < ctyps.size(); ++i) {
                    bool selected = (i == (size_t)currentCtypIndex);
                    if (ImGui::Selectable(ctyps[i].c_str(), selected)) {
                        selectedNode->costType = ctyps[i];
                        attrChanged = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            char ctypBuf[64];
            strncpy(ctypBuf, selectedNode->costType.c_str(), sizeof(ctypBuf));
            ctypBuf[sizeof(ctypBuf)-1] = '\0';
            if (ImGui::InputText("##ctyp", ctypBuf, sizeof(ctypBuf))) {
                selectedNode->costType = std::string(ctypBuf);
                attrChanged = true;
            }
        }
    }

    ImGui::SameLine();
    // Toggle (no text) to switch ctyp into a free text input
    {
        bool newToggle = m_ctypCustomInput;
        if (ImGui::Checkbox("##ctyp_toggle", &newToggle)) {
            m_ctypCustomInput = newToggle;
            if (m_ctypCustomInput) {
                strncpy(m_customCtypBuf, selectedNode->costType.c_str(), sizeof(m_customCtypBuf));
                m_customCtypBuf[sizeof(m_customCtypBuf)-1] = '\0';
            }
        }
        // Show 'Custom' label next to the checkbox; orange when checked
        ImGui::SameLine();
        if (m_ctypCustomInput) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Custom");
        else ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Custom");
    }

    ImGui::Spacing();
    
    // dep - Dependency (label only)
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Dependency (dep):");
    if (selectedNode->dep == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Root Node (id 0)");
    } else {
        ImGui::Text("%llu", (unsigned long long)selectedNode->dep);
    }
    ImGui::Spacing();
    
    // id - ID (editable text)
    ImGui::TextColored(matchColor, "ID (id):");
    // Fix ID button directly to the right of the label
    if (!idMatches && showFixButton) {
        ImGui::SameLine();
        // Nudge the button slightly upward for better vertical alignment with the label
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.60f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.35f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        if (ImGui::Button("Fix ID", ImVec2(80, ImGui::GetFrameHeight()))) {
            uint64_t oldId = selectedNode->id;
            if (m_treeManager.changeNodeId(m_selectedSpirit, oldId, nodeExpectedId)) {
                // Rebuild tree to ensure relationships and children arrays are consistent
                m_treeManager.rebuildTree(m_selectedSpirit);
                m_treeRenderer.setSelectedNodeId(nodeExpectedId);
                selectedNode = m_treeManager.getNode(m_selectedSpirit, nodeExpectedId);
                attrChanged = true;
                m_lastEditedNodeId = nodeExpectedId;
                // update idBuf so the input reflects the fixed value immediately
                // (we update the buffer below after we create it)
            } else {
                // fallback: assign directly
                selectedNode->id = nodeExpectedId;
                m_treeManager.rebuildTree(m_selectedSpirit);
                attrChanged = true;
                m_treeRenderer.setSelectedNodeId(nodeExpectedId);
                m_lastEditedNodeId = nodeExpectedId;
            }
        }
        ImGui::PopStyleColor(4);
    }

    char idBuf[64];
    snprintf(idBuf, sizeof(idBuf), "%llu", (unsigned long long)selectedNode->id);
    if (ImGui::InputText("##id", idBuf, sizeof(idBuf))) {
        uint64_t newId = strtoull(idBuf, nullptr, 10);
        if (newId != selectedNode->id) {
            uint64_t oldId = selectedNode->id;
            // Try to change id using manager helper so references are updated
            if (m_treeManager.changeNodeId(m_selectedSpirit, oldId, newId)) {
                // Selection should follow the node
                m_treeRenderer.setSelectedNodeId(newId);
                selectedNode = m_treeManager.getNode(m_selectedSpirit, newId);
            } else {
                // Fallback: direct assignment and rebuild
                selectedNode->id = newId;
                m_treeManager.rebuildTree(m_selectedSpirit);
            }
            attrChanged = true;
            m_lastEditedNodeId = newId;

        }
    }



    // Show expected ID on the next line for readability when it doesn't match
    if (!idMatches) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(expected: %u)", nodeExpectedId);
    }

    // ID-Name match status (placed under ID and above Name)
    if (idMatches) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Match!");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "MISMATCH!");
    }



    ImGui::Spacing();

    // nm - Name (editable)
    ImGui::TextColored(matchColor, "Name (nm):");

    // Fix name by ID button to the right of the label
    if (!idMatches) {
        ImGui::SameLine();
        // Check whether a previous attempt already determined this node id is unknown in the loaded file
        auto& failedSet = m_unknownNameFromLoadedFileIds[m_selectedSpirit];
        if (failedSet.find(selectedNode->id) != failedSet.end()) {
            // Show persistent red message for this node
            ImGui::TextColored(ImVec4(0.85f, 0.2f, 0.2f, 1.0f), "Unknown ID from file");
        } else {
            // Nudge the button slightly upward for alignment
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.60f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.35f, 0.08f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("Fix name by ID", ImVec2(120, ImGui::GetFrameHeight()))) {
                std::string restored;
                if (m_treeManager.getNameFromLoadedFile(m_selectedSpirit, selectedNode->id, &restored)) {
                    selectedNode->name = restored;
                    attrChanged = true;
                    // Remove any previously-recorded failure marker (if present)
                    failedSet.erase(selectedNode->id);
                } else {
                    // Record failure so the message persists for this node
                    failedSet.insert(selectedNode->id);
                }
            }
            ImGui::PopStyleColor(4);
        }
    }

    ImGui::Spacing();

    char nameBuf[256];
    strncpy(nameBuf, selectedNode->name.c_str(), sizeof(nameBuf));
    if (ImGui::InputText("##nm", nameBuf, sizeof(nameBuf))) {
        std::string newName = std::string(nameBuf);
        // Check for duplicate names in this spirit
        if (m_treeManager.isNameDuplicate(m_selectedSpirit, newName, selectedNode->id)) {
            // Revert to initial name and inform the user
            selectedNode->name = selectedNode->originalName;
            setTreeMessage("Node with same name found.", TreeMessageType::Error, std::chrono::seconds(3));
            // Visual pulse on offending node
            m_treeRenderer.pulseNodeRed(selectedNode->id);
        } else {
            selectedNode->name = newName;
            attrChanged = true;
        }
    }



    ImGui::Spacing();
    
    // spirit - Spirit (dropdown from known spirits)
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Spirit (spirit):");

    std::vector<std::string> allSpirits = m_treeManager.getSpiritNames();
    const auto& guides = m_treeManager.getGuideNames();
    allSpirits.insert(allSpirits.end(), guides.begin(), guides.end());
    // Remove duplicates
    std::sort(allSpirits.begin(), allSpirits.end());
    allSpirits.erase(std::unique(allSpirits.begin(), allSpirits.end()), allSpirits.end());
    int currentSpiritIndex = 0;
    for (size_t i = 0; i < allSpirits.size(); ++i) {
        if (allSpirits[i] == selectedNode->spirit) currentSpiritIndex = (int)i;
    }
    // Keep widget widths consistent between list and search modes
    float spiritWidgetWidth = 160.0f; // reduced width
    // Suppress showing the opposite toggle button during the same frame when a toggle
    // action was just taken, to avoid a temporary duplicate button flash
    bool suppressSearchButtonThisFrame = false;
    if (m_spiritCustomInput) {
        // Searchable inline input: type to filter suggestions; Enter to commit; click a suggestion to apply
        ImGui::PushItemWidth(spiritWidgetWidth);
        bool enterPressed = ImGui::InputText("##spirit_inline", m_customSpiritBuf, sizeof(m_customSpiritBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        // Inline toggle button to return to the list (shows "List")
        ImGui::SameLine();
        if (ImGui::Button("List##spirit_toggle_btn", ImVec2(56, 0))) {
            m_spiritCustomInput = false; // switch back to dropdown/list
            suppressSearchButtonThisFrame = true;
        }

        // Build case-insensitive suggestions from available spirits
        std::string query = m_customSpiritBuf;
        auto toLower = [](const std::string& s){ std::string r = s; std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return std::tolower(c); }); return r; };
        std::string ql = toLower(query);
        std::vector<std::string> suggestions;
        for (const auto& s : allSpirits) {
            if (ql.empty() || toLower(s).find(ql) != std::string::npos) {
                suggestions.push_back(s);
            }
            if (suggestions.size() >= 8) break;
        }

        if (!suggestions.empty()) {
            ImGui::BeginChild("##spirit_suggestions", ImVec2(spiritWidgetWidth, std::min(200.0f, 20.0f * (float)suggestions.size())), true);
            for (const auto& s : suggestions) {
                if (ImGui::Selectable(s.c_str())) {
                    selectedNode->spirit = s;
                    attrChanged = true;
                    m_spiritCustomInput = false;
                    break;
                }
            }
            ImGui::EndChild();
        } else {
            ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "No matches");
        }

            // Commit typed text on Enter (doesn't create or move trees)
        if (enterPressed) {
            std::string newSpirit(m_customSpiritBuf);
            if (!newSpirit.empty()) {
                selectedNode->spirit = newSpirit;
                attrChanged = true;
            }
            m_spiritCustomInput = false;
        }


    } else {
        if (!allSpirits.empty()) {
            ImGui::PushItemWidth(spiritWidgetWidth);
            if (ImGui::BeginCombo("##spirit", allSpirits[currentSpiritIndex].c_str())) {
                for (size_t i = 0; i < allSpirits.size(); ++i) {
                    bool selected = (i == (size_t)currentSpiritIndex);
                    if (ImGui::Selectable(allSpirits[i].c_str(), selected)) {
                        // Only change the attribute value; do NOT move the node or change the view
                        selectedNode->spirit = allSpirits[i];
                        attrChanged = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
        }
    }

    // Small toggle button to enable search (shows "Search")
    if (!m_spiritCustomInput && !suppressSearchButtonThisFrame) {
        ImGui::SameLine();
        if (ImGui::Button("Search##spirit_toggle_btn", ImVec2(60, 0))) {
            m_spiritCustomInput = true;
            strncpy(m_customSpiritBuf, selectedNode->spirit.c_str(), sizeof(m_customSpiritBuf));
            m_customSpiritBuf[sizeof(m_customSpiritBuf)-1] = '\0';
        }
    }

    ImGui::Spacing();
    
    // typ - Type (dropdown from known types)
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Type (typ):");

    // Collect known types: union of currently loaded trees and historically known types
    std::vector<std::string> types;
    // Add known types seen previously
    for (const auto& t : m_knownTypes) if (!t.empty()) types.push_back(t);
    // Also include current file's types (in case new file introduced new types)
    for (const auto& s : m_treeManager.getSpiritNames()) {
        const SpiritTree* t = m_treeManager.getTree(s);
        if (!t) continue;
        for (const auto& n : t->nodes) if (!n.type.empty()) types.push_back(n.type);
    }
    for (const auto& s : m_treeManager.getGuideNames()) {
        const SpiritTree* t = m_treeManager.getTree(s);
        if (!t) continue;
        for (const auto& n : t->nodes) if (!n.type.empty()) types.push_back(n.type);
    }
    std::sort(types.begin(), types.end());
    types.erase(std::unique(types.begin(), types.end()), types.end());
    int currentTypeIndex = 0;
    for (size_t i = 0; i < types.size(); ++i) if (types[i] == selectedNode->type) currentTypeIndex = (int)i;
    if (m_typeCustomInput) {
        // Inline text field replaces dropdown
        if (ImGui::InputText("##typ_inline", m_customTypeBuf, sizeof(m_customTypeBuf))) {
            selectedNode->type = std::string(m_customTypeBuf);
            attrChanged = true;
            // Remember this custom type for future edits
            if (!selectedNode->type.empty()) addKnownType(selectedNode->type);
        }
    } else {
        if (ImGui::BeginCombo("##typ", selectedNode->type.c_str())) {
            for (size_t i = 0; i < types.size(); ++i) {
                bool selected = (i == (size_t)currentTypeIndex);
                if (ImGui::Selectable(types[i].c_str(), selected)) {
                    selectedNode->type = types[i];
                    attrChanged = true;
                    if (!selectedNode->type.empty()) addKnownType(selectedNode->type);
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SameLine();
    // Toggle (no text) to switch type into free text input (always visible)
    {
        bool newToggle = m_typeCustomInput;
        if (ImGui::Checkbox("##typ_toggle", &newToggle)) {
            m_typeCustomInput = newToggle;
            if (m_typeCustomInput) {
                strncpy(m_customTypeBuf, selectedNode->type.c_str(), sizeof(m_customTypeBuf));
                m_customTypeBuf[sizeof(m_customTypeBuf)-1] = '\0';
            }
        }
        // Show 'Custom' label next to the checkbox; orange when checked
        ImGui::SameLine();
        if (m_typeCustomInput) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Custom");
        else ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Custom");
    }

    ImGui::Spacing();

    // If any attribute changed above, sync it to the JSON editor buffer so changes appear in real-time
    if (attrChanged) {
        std::string json = SpiritTreeManager::nodeToJson(*selectedNode);
        m_textEditor.SetText(json);
        m_lastEditedNodeId = selectedNodeId;
        m_jsonParseError = false;
        m_jsonErrorMsg.clear();
        // Remember the current node type in known types so the dropdown preserves it across loads
        if (!selectedNode->type.empty()) addKnownType(selectedNode->type);
    }
    
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Leaves:");
    if (selectedNode->children.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "None (leaf node)");
    } else {
        ImGui::Text("%zu node(s)", selectedNode->children.size());
        for (uint64_t childId : selectedNode->children) {
            ImGui::BulletText("%llu", (unsigned long long)childId);
        }
    }
    
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void App::renderNodeJsonEditor() {
    ImGui::Text("JSON Editor");
    // Show multiple-selection indicator beside the title when applicable
    const auto& selectedIdsForJson = m_treeRenderer.getSelectedNodeIds();
    if (selectedIdsForJson.size() > 1) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[Multiple selected]");
    }
    ImGui::Separator();
    
    const auto& selectedIds = m_treeRenderer.getSelectedNodeIds();
    uint64_t primaryId = m_treeRenderer.getSelectedNodeId();
    
    if (selectedIds.empty() || m_selectedSpirit.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select a node to edit");
        return;
    }

    // Multiple selected: reflect in JSON editor but make read-only
    if (selectedIds.size() > 1) {
        // If selection changed, rebuild the JSON array buffer. Preserve original tree order
        // by iterating the spirit's nodes in their stored order and including only the
        // selected ones. This prevents unordered_set iteration from changing ordering.
        if (m_lastEditedNodeId != primaryId || m_lastEditedSelectionCount != (int)selectedIds.size()) {
            std::string arr = "[\n";
            bool first = true;
            const SpiritTree* tree = m_treeManager.getTree(m_selectedSpirit);
            if (tree) {
                for (const auto& node : tree->nodes) {
                    if (selectedIds.count(node.id) == 0) continue;
                    if (!first) arr += ",\n";
                    arr += SpiritTreeManager::nodeToJson(node);
                    first = false;
                }
            } else {
                // Fallback to previous behavior if tree not available
                for (uint64_t id : selectedIds) {
                    SpiritNode* n = m_treeManager.getNode(m_selectedSpirit, id);
                    if (!n) continue;
                    if (!first) arr += ",\n";
                    arr += SpiritTreeManager::nodeToJson(*n);
                    first = false;
                }
            }
            arr += "\n]";
            m_textEditor.SetText(arr);
            m_lastEditedNodeId = primaryId;
            m_lastEditedSelectionCount = (int)selectedIds.size();
            m_jsonParseError = false;
            m_jsonErrorMsg.clear();
        }
    } else {
        // Single selection: keep existing editable behavior
        uint64_t selectedNodeId = primaryId;
        SpiritNode* node = m_treeManager.getNode(m_selectedSpirit, selectedNodeId);
        if (!node) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Node not found");
            return;
        }

        // If node changed, update buffer
        if (m_lastEditedNodeId != selectedNodeId || m_lastEditedSelectionCount != 1) {
            std::string json = SpiritTreeManager::nodeToJson(*node);
            m_textEditor.SetText(json);
            m_lastEditedNodeId = selectedNodeId;
            m_lastEditedSelectionCount = 1;
            m_jsonParseError = false;
            m_jsonErrorMsg.clear();
        }
    }
    
    // Calculate available height for the text editor
    float availHeight = ImGui::GetContentRegionAvail().y - 25.0f;
    if (availHeight < 50.0f) availHeight = 50.0f;
    

    // Rich editor (TextEditor). If multiple nodes are selected, render read-only; otherwise render editable.
    bool multiSelected = (m_treeRenderer.getSelectedNodeIds().size() > 1);
    m_textEditor.SetReadOnly(multiSelected);

    // Ensure JSON language definition is set (one-time check based on definition name)
    if (m_textEditor.GetLanguageDefinition().mName != "JSON") {
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::JSON());
        m_textEditor.SetShowWhitespaces(false);
    }
    
    m_textEditor.Render("##jsoneditor", ImVec2(-1, availHeight), true);
    bool edited = !multiSelected && m_textEditor.IsTextChanged();

    // Handle edits
    if (edited) {
        // Only allow edits when a single node is selected
        uint64_t newNodeId = 0;
        uint64_t selectedNodeId = m_treeRenderer.getSelectedNodeId();
        const std::string txt = m_textEditor.GetText();
        try {
            auto data = nlohmann::json::parse(txt);
            // If name change requested, validate uniqueness first
            if (data.contains("nm")) {
                std::string attempted = data["nm"].get<std::string>();
                if (m_treeManager.isNameDuplicate(m_selectedSpirit, attempted, selectedNodeId)) {
                    // Revert editor text to original node JSON and inform user
                    SpiritNode* node = m_treeManager.getNode(m_selectedSpirit, selectedNodeId);
                    if (node) {
                        m_textEditor.SetText(SpiritTreeManager::nodeToJson(*node));
                        setTreeMessage("Node with same name found.", TreeMessageType::Error, std::chrono::seconds(3));
                        m_treeRenderer.pulseNodeRed(selectedNodeId);
                    }
                    m_jsonParseError = true;
                    m_jsonErrorMsg = "Duplicate name";
                    // Skip attempting to apply the edit
                    goto JSON_EDIT_END;
                }
            }

            if (m_treeManager.updateNodeFromJson(m_selectedSpirit, selectedNodeId, txt, &newNodeId)) {
                m_jsonParseError = false;
                m_jsonErrorMsg.clear();
                if (newNodeId != selectedNodeId) {
                    m_treeRenderer.setSelectedNodeId(newNodeId);
                    m_lastEditedNodeId = newNodeId;
                }
            } else {
                m_jsonParseError = true;
                m_jsonErrorMsg = "Invalid JSON";
            }
        } catch (...) {
            m_jsonParseError = true;
            m_jsonErrorMsg = "Invalid JSON";
        }
    }
    JSON_EDIT_END: ;

    // Right-click context menu for the JSON editor has been removed

    // Show status
    if (m_jsonParseError) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", m_jsonErrorMsg.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "JSON Valid");
        // Right-anchored interactive hint: shows Ctrl + C/V/X and highlights keys when pressed
        bool ctrlDown = false;
        if (m_window) {
            ctrlDown = (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
                       (glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
        }
        bool cDown = ctrlDown && m_window && (glfwGetKey(m_window, GLFW_KEY_C) == GLFW_PRESS);
        bool vDown = ctrlDown && m_window && (glfwGetKey(m_window, GLFW_KEY_V) == GLFW_PRESS);
        bool xDown = ctrlDown && m_window && (glfwGetKey(m_window, GLFW_KEY_X) == GLFW_PRESS);

        // Use regular text color but slightly dimmed so the hint remains visible when not active
        ImVec4 defaultCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        defaultCol.w *= 0.60f;
        ImVec4 whiteCol(1.0f, 1.0f, 1.0f, 1.0f);
        // Cyan-blue for Copy (brighter, more cyan than pure blue)
        ImVec4 blueCol(0.0f, 0.8f, 0.9f, 1.0f);
        ImVec4 greenCol(0.2f, 0.75f, 0.3f, 1.0f);
        ImVec4 redCol(1.0f, 0.28f, 0.28f, 1.0f);

        const char* s_ctrl = "CTRL";
        const char* s_plus = " + ";
        // Order adjusted to match QWERTY: X is left of C and V
        const char* s_x = "X";
        const char* s_slash = " / ";
        const char* s_c = "C";
        const char* s_v = "V";

        ImVec4 ctrlCol = ctrlDown ? whiteCol : defaultCol;
        ImVec4 cCol = (ctrlDown && cDown) ? blueCol : defaultCol;
        ImVec4 vCol = (ctrlDown && vDown) ? greenCol : defaultCol;
        ImVec4 xCol = (ctrlDown && xDown) ? redCol : defaultCol;

        ImVec4 plusCol = defaultCol;
        // Prefer highlighting the key on the left first (X), then C, then V
        if (ctrlDown && xDown) plusCol = redCol;
        else if (ctrlDown && cDown) plusCol = blueCol;
        else if (ctrlDown && vDown) plusCol = greenCol;

        ImVec2 sz_ctrl = ImGui::CalcTextSize(s_ctrl);
        ImVec2 sz_plus = ImGui::CalcTextSize(s_plus);
        ImVec2 sz_x = ImGui::CalcTextSize(s_x);
        ImVec2 sz_slash = ImGui::CalcTextSize(s_slash);
        ImVec2 sz_c = ImGui::CalcTextSize(s_c);
        ImVec2 sz_v = ImGui::CalcTextSize(s_v);

        float gap = ImGui::GetStyle().ItemSpacing.x * 0.25f;
        float totalWidth = sz_ctrl.x + sz_plus.x + sz_x.x + sz_slash.x + sz_c.x + sz_slash.x + sz_v.x + gap;

        float pad = ImGui::GetStyle().WindowPadding.x;
        float windowWidth = ImGui::GetWindowWidth();
        float desiredX = windowWidth - pad - totalWidth;
        ImGui::SameLine();
        if (desiredX > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(desiredX);
        }



        ImGui::TextColored(ctrlCol, "%s", s_ctrl); ImGui::SameLine(0,0);
        ImGui::TextColored(plusCol, "%s", s_plus); ImGui::SameLine(0,0);
        ImGui::TextColored(xCol, "%s", s_x); ImGui::SameLine(0,0);
        ImGui::TextColored(defaultCol, "%s", s_slash); ImGui::SameLine(0,0);
        ImGui::TextColored(cCol, "%s", s_c); ImGui::SameLine(0,0);
        ImGui::TextColored(defaultCol, "%s", s_slash); ImGui::SameLine(0,0);
        ImGui::TextColored(vCol, "%s", s_v);
    }
}

void App::renderStatusBar() {
    ImGui::Separator();
    
    if (m_treeManager.isLoaded()) {
        ImGui::Text("Loaded: %s  |  Total Spirits: %zu",
                   m_treeManager.getLoadedFile().c_str(),
                   m_treeManager.getSpiritNames().size());
    } else {
        ImGui::Text("Ready - Open a JSON file to begin");
    }

    // Show controls hint on the right
    ImGui::SameLine(ImGui::GetWindowWidth() - 450);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), 
                      "Scroll: Zoom | Right-Click Drag: Pan | Shift: Multi-select");

    // No native file dialogs used; we display internal ImGui modals when needed.
}


void App::openFileDialog() {
    m_showInternalOpenDialog = true;
    // Initialize path to the user's home directory when possible (cross-platform),
    // otherwise fall back to the current working directory. Clear selection.
    const char* home = nullptr;
#if defined(_WIN32)
    home = std::getenv("USERPROFILE");
#else
    home = std::getenv("HOME");
#endif
    if (home && home[0] != '\0') {
        m_internalDialogPath = std::string(home);
    } else {
        m_internalDialogPath = std::filesystem::current_path().string();
    }
    m_internalSelectedFilename[0] = '\0';
}

void App::loadFile(const std::string& path) {
    if (m_treeManager.loadFromFile(path)) {
        m_currentFilePath = path;
        m_selectedSpirit.clear();
        m_searchFilter[0] = '\0';
        
        // Auto-select first spirit if available
        const auto& spirits = m_treeManager.getSpiritNames();
        if (!spirits.empty()) {
            m_selectedSpirit = spirits[0];
        }
        // Update known types from loaded trees so typ dropdown remains aware of them
        syncKnownTypesFromTrees();
    }
}

void App::saveFileDialog() {
    // Show internal save modal
    m_showInternalSaveDialog = true;

    // Initialize save path and filename: start from current directory, then prefer Documents if available
    m_internalSavePath = std::filesystem::current_path().string();

    std::string defaultDir;
#if defined(_WIN32)
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) defaultDir = std::string(userProfile) + "\\Documents";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) defaultDir = std::string(home) + "/Documents";
#else
    const char* xdgDocs = std::getenv("XDG_DOCUMENTS_DIR");
    const char* home = std::getenv("HOME");
    if (xdgDocs) defaultDir = std::string(xdgDocs);
    else if (home) defaultDir = std::string(home) + "/Documents";
#endif

    if (!defaultDir.empty()) m_internalSavePath = defaultDir;
    // Reset new-file state
    m_internalSaveNew = false;
    m_internalSaveNewName[0] = '\0';

}

void App::saveFile(const std::string& path) {
    if (m_treeManager.saveToFile(path)) {
        m_currentFilePath = path;
        try {
            std::time_t now = std::time(nullptr);
            m_forcedTimestamps[path] = now;
        } catch (...) {}
    }
}

// Save a single spirit (only nodes from one SpiritTree) into a JSON file
void App::saveSingleSpiritToPath(const std::string& path, const std::string& spiritName) {
    try {
        const SpiritTree* tree = m_treeManager.getTree(spiritName);
        if (!tree) return;
        nlohmann::json output = nlohmann::json::array();
        for (const auto& node : tree->nodes) {
            nlohmann::json nodeJson;
            nodeJson["ap"] = node.isAdventurePass;
            nodeJson["cst"] = node.cost;
            nodeJson["ctyp"] = node.costType;
            nodeJson["dep"] = node.dep;
            nodeJson["id"] = node.id;
            nodeJson["nm"] = node.name;
            nodeJson["spirit"] = node.spirit;
            nodeJson["typ"] = node.type;
            output.push_back(nodeJson);
            // Remember this type as a known type
            if (!node.type.empty()) addKnownType(node.type);
        }
        std::ofstream file(path);
        if (!file.is_open()) return;
        file << output.dump(3);
        file.close();
        // Update forced timestamp map so UI shows immediate modification time
        try { m_forcedTimestamps[path] = std::time(nullptr); } catch(...){}
    } catch (...) {}
}

void App::addKnownType(const std::string& t) {
    if (t.empty()) return;
    m_knownTypes.insert(t);
}

void App::syncKnownTypesFromTrees() {
    for (const auto& s : m_treeManager.getSpiritNames()) {
        const SpiritTree* t = m_treeManager.getTree(s);
        if (!t) continue;
        for (const auto& n : t->nodes) if (!n.type.empty()) m_knownTypes.insert(n.type);
    }
    for (const auto& s : m_treeManager.getGuideNames()) {
        const SpiritTree* t = m_treeManager.getTree(s);
        if (!t) continue;
        for (const auto& n : t->nodes) if (!n.type.empty()) m_knownTypes.insert(n.type);
    }
}
} // namespace Watercan
