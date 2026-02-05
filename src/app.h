#pragma once

#include "spirit_tree.h"
#include "tree_renderer.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <ctime>
#include <array>
#include <chrono>

struct GLFWwindow;

namespace Watercan {

class App {
public:
    App();
    ~App();
    
    // Initialize the application
    bool init();
    
    // Main run loop
    void run();
    
    // Cleanup
    void shutdown();

    // Try loading about image (used to lazily reload if missing at runtime)
    void loadAboutImage();
    // Open a URL in the default browser (platform-dependent)
    void openUrl(const std::string& url);
    
private:
    void renderUI();
    void renderMenuBar();
    void renderSpiritList();
    void renderTreeViewport();
    void renderNodeDetails();
    void renderNodeJsonEditor();
    void renderStatusBar();
    
    void openFileDialog();
    void saveFileDialog();
    void loadFile(const std::string& path);
    void saveFile(const std::string& path);
    void saveSingleSpiritToPath(const std::string& path, const std::string& spiritName);
    
    // Window handle
    GLFWwindow* m_window = nullptr;
    
    // Application state
    bool m_running = false;
    std::string m_selectedSpirit;
    std::string m_currentFilePath;
    
    // Core components
    SpiritTreeManager m_treeManager;
    TreeRenderer m_treeRenderer;
    
    // UI state
    float m_sidebarWidth = 250.0f;
    float m_detailsWidth = 300.0f;
    float m_nodeDetailsHeight = 620.0f;
    bool m_showAbout = false;
    bool m_showLicense = false;
    char m_searchFilter[256] = "";
    int m_spiritListTab = 0;  // 0 = Spirits, 1 = Guides
    
    // About image texture
    unsigned int m_aboutImageTexture = 0;
    int m_aboutImageWidth = 0;
    int m_aboutImageHeight = 0;
    
    // JSON editor state
    uint64_t m_lastEditedNodeId = 0;
    int m_lastEditedSelectionCount = 0;
    char m_jsonEditBuffer[8192] = "";
    bool m_jsonParseError = false;
    std::string m_jsonErrorMsg;

    
    // Create mode state
    bool m_createMode = false;

    // Open dialog
    bool m_showInternalOpenDialog = false;
    std::string m_internalDialogPath = ".";
    char m_internalSelectedFilename[512] = "";

    // Save dialog
    bool m_showInternalSaveDialog = false;
    std::string m_internalSavePath = ".";
    // Save dialog selection and overwrite confirmation
    char m_internalSaveSelectedFilename[512] = "";
    bool m_showOverwriteConfirm = false;
    std::string m_overwriteTargetPath;
    // Deferred save path used to perform saves after modal confirmation without blocking UI
    std::string m_pendingSavePath;
    // New-file mock entry state: when true, show a small name input next to 'Name:'
    bool m_internalSaveNew = false;
    char m_internalSaveNewName[128] = "";

    // When true, the Save dialog is being used to save a single spirit (only one tree)
    bool m_internalSaveSingle = false;
    std::string m_internalSaveSingleName;

    // Save feedback (temporary 'Saved!' button state)
    double m_saveFeedbackUntil = 0.0;
    // Icon textures for file dialogs
    unsigned int m_iconFolderTexture = 0;
    unsigned int m_iconFileTexture = 0;

    // Forced timestamps for files we've just overwritten (path -> epoch seconds)
    std::unordered_map<std::string, std::time_t> m_forcedTimestamps;

    // Create procedural icons (folder/file)
    void createIconTextures();

    
    // Link mode state
    bool m_linkMode = false;
    uint64_t m_linkSourceNodeId = TreeRenderer::NO_NODE_ID;  // The node being linked (will become child)
    
    // Delete confirmation state
    bool m_deleteConfirmMode = false;
    uint64_t m_deleteNodeId = TreeRenderer::NO_NODE_ID;

    // New / delete spirit UI
    bool m_showNewSpiritModal = false;
    char m_newSpiritName[128] = "";
    bool m_showDeleteSpiritModal = false;

    // Custom input toggles and buffers for dropdown-to-text conversion
    bool m_spiritCustomInput = false;
    char m_customSpiritBuf[256] = "";
    bool m_typeCustomInput = false;
    char m_customTypeBuf[128] = "";
    bool m_ctypCustomInput = false;
    char m_customCtypBuf[64] = "";

    // Clipboard for copy/paste
    bool m_hasClipboardNode = false;
    std::string m_clipboardNodeJson;
    // Canvas paste position (world coords) when user clicks empty canvas
    float m_canvasPasteX = 0.0f;
    float m_canvasPasteY = 0.0f;
    
    // Context menu state
    bool m_showNodeContextMenu = false;
    uint64_t m_contextMenuNodeId = TreeRenderer::NO_NODE_ID;

    // Tools -> FNV1a32 Generator dialog
    bool m_showFNVDialog = false;
    char m_fnvNameBuf[256] = "";
    uint32_t m_fnvResult = 0;
    // Tools -> Color codes dialog
    bool m_showColorCodes = false;
    // Mapping from node type (typ) to RGBA color
    std::unordered_map<std::string, std::array<float,4>> m_typeColors;
    // Time until which the "Saved!" feedback should be shown
    std::chrono::steady_clock::time_point m_typeColorsSavedUntil;

    SpiritTree m_previewTree;
    bool m_previewLoaded = false;

    // Known typ values seen across loaded files (persisted in-memory during session)
    std::unordered_set<std::string> m_knownTypes;

    // Track node IDs for which 'Fix name by ID' failed to find a name in the loaded file (spirit -> set of node ids)
    std::unordered_map<std::string, std::unordered_set<uint64_t>> m_unknownNameFromLoadedFileIds;

    // Helpers to maintain known types
    void addKnownType(const std::string& t);
    void syncKnownTypesFromTrees();




    // Persist and load user-saved type colors
    bool saveTypeColorsToDisk();
    bool loadTypeColorsFromDisk();


};

} // namespace Watercan
