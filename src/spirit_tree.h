#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace Watercan {

// Represents a single node in a spirit tree
struct SpiritNode {
    uint64_t id = 0;
    uint64_t dep = 0;  // Parent dependency (0 = root node)
    std::string name;
    std::string originalName; // preserved initial name loaded from file (for reversion on duplicates)
    std::string spirit;
    std::string type;
    std::string costType;
    int cost = 0;
    bool isAdventurePass = false;
    bool isNew = false; // true for nodes created at runtime (not originally in loaded file)
    
    // Computed layout information
    float x = 0.0f;
    float y = 0.0f;
    std::vector<uint64_t> children;  // IDs of dependent nodes
};

// Represents a complete spirit tree
struct SpiritTree {
    std::string spiritName;
    std::vector<SpiritNode> nodes;
    uint64_t rootNodeId = 0;
    
    // Computed bounds for rendering
    float minX = 0.0f, maxX = 0.0f;
    float minY = 0.0f, maxY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// FNV-1a 32-bit hash function (matches Python fnv1a32)
inline uint32_t fnv1a32(const std::string& data) {
    constexpr uint32_t FNV_OFFSET_BASIS = 0x811C9DC5;
    constexpr uint32_t FNV_PRIME = 0x01000193;
    uint32_t h = FNV_OFFSET_BASIS;
    for (char c : data) {
        h = (h ^ static_cast<uint8_t>(c)) * FNV_PRIME;
    }
    return h;
}

// Manager class for loading and organizing spirit trees
class SpiritTreeManager {
public:
    SpiritTreeManager() = default;
    
    // Load spirits from a JSON file
    bool loadFromFile(const std::string& filepath);

    // Load spirits from an in-memory JSON string (useful for embedded assets)
    bool loadFromString(const std::string& jsonContents);
    
    // Save spirits to a JSON file (preserving original structure)
    bool saveToFile(const std::string& filepath) const;
    
    // Update a node's ID based on its name (FNV-1a hash)
    bool updateNodeId(const std::string& spiritName, uint64_t oldId);
    // Change a node id from oldId to newId, updating any references (deps/children/root)
    bool changeNodeId(const std::string& spiritName, uint64_t oldId, uint64_t newId);
    
    // Get list of all spirit names
    const std::vector<std::string>& getSpiritNames() const { return m_spiritNames; }
    
    // Get list of guide spirit names
    const std::vector<std::string>& getGuideNames() const { return m_guideNames; }
    
    // Get a specific spirit tree
    SpiritTree* getTree(const std::string& spiritName);
    const SpiritTree* getTree(const std::string& spiritName) const;
    
    // Get node count for a spirit
    size_t getNodeCount(const std::string& spiritName) const;
    
    // Get a mutable node by ID
    SpiritNode* getNode(const std::string& spiritName, uint64_t nodeId);
    
    // Update a node from JSON string, returns true if successful
    // If newNodeId is provided, it will be set to the new ID (in case ID was changed)
    bool updateNodeFromJson(const std::string& spiritName, uint64_t nodeId, const std::string& jsonStr, uint64_t* newNodeId = nullptr);
    
    // Create a new node at the given position, returns the new node's ID
    uint64_t createNode(const std::string& spiritName, float x, float y);
    
    // Delete a node by ID, returns true if successful
    bool deleteNode(const std::string& spiritName, uint64_t nodeId);
    
    // Rebuild tree relationships (call after editing nodes)
    void rebuildTree(const std::string& spiritName);
    
    // Position a node as a child of its parent according to tree layout rules
    // Optionally, provide an output map of nodeId -> (dx, dy) shifts representing the
    // visual offset that should be applied to keep the node visually steady and then animate
    // into the new base position (oldBase - newBase).
    void positionLinkedNode(const std::string& spiritName, uint64_t nodeId,
                            std::unordered_map<uint64_t, std::pair<float,float>>* outShifts = nullptr);
    // Re-layout the subtree rooted at nodeId and return per-node shifts (oldBase - newBase)
    // for nodes in that subtree so callers can animate them smoothly. The root node's base
    // position is assumed to already be set in the tree (e.g., via moveNodeBase) and will
    // not be included in the shifts map (so it can be dragged directly without animation).
    bool layoutSubtreeAndCollectShifts(const std::string& spiritName, uint64_t rootNodeId,
                                       std::unordered_map<uint64_t, std::pair<float,float>>* outShifts = nullptr);

    // Recompute the computed layout for the entire tree and collect (oldBase - newBase)
    // shifts for all nodes that moved. Returns true if successful and outShifts is filled.
    bool reshapeTreeAndCollectShifts(const std::string& spiritName,
                                     std::unordered_map<uint64_t, std::pair<float,float>>* outShifts);

    // Record and restore snaps so detached children can be reattached on Reshape
    void recordSnap(const std::string& spiritName, uint64_t childId, uint64_t oldParentId);
    // Restore snaps and return list of node ids that were reattached
    std::vector<uint64_t> restoreSnaps(const std::string& spiritName);
    bool hasSnaps(const std::string& spiritName) const;
    // Return true if the given spirit has pending snapped children recorded
    bool hasSnapsInternal(const std::string& spiritName) const;

    // Clear any recorded snap data for a given child in a spirit (used when user manually re-links)
    void clearSnap(const std::string& spiritName, uint64_t childId);

    // Check whether a given name already exists in the spirit (excluding optional node id)
    bool isNameDuplicate(const std::string& spiritName, const std::string& name, uint64_t excludeId = 0) const;
    // Return set of node IDs that have duplicate names within the spirit (size>1 names)
    std::unordered_set<uint64_t> getDuplicateNodeIds(const std::string& spiritName) const;

public:
    // Map of snapped child -> original parent id and original index (persistent until restored)
    struct SnapInfo { uint64_t parentId; size_t index; };
    std::unordered_map<uint64_t, SnapInfo> m_snappedParents;
    // Per-tree list of snapped child ids (helps quickly check if a spirit has snaps)
    std::unordered_map<std::string, std::vector<uint64_t>> m_perTreeSnaps;
    // layout positions (within a small epsilon). Used to enable/disable the Reshape button.
    bool needsReshape(const std::string& spiritName, float epsilon = 0.1f);

    // Mark a spirit's cached reshape/restore results as stale (call after any mutation)
    void markDirty(const std::string& spiritName);

    
    // Convert a node to JSON string
    static std::string nodeToJson(const SpiritNode& node);

    // Try to find a node name in the originally loaded file for the given spirit and id.
    // If the app loaded from a file, this will scan that file and return the original
    // "nm" value if present. Returns true on success.
    bool getNameFromLoadedFile(const std::string& spiritName, uint64_t nodeId, std::string* outName) const;

    // Reload a single spirit from the originally loaded file, discarding all changes.
    // Returns true if the spirit was successfully reloaded.
    bool reloadSpirit(const std::string& spiritName);

    // Clear ALL snap records for a given spirit.
    void clearAllSnaps(const std::string& spiritName);

    // Check if the spirit has been structurally modified from the loaded file:
    // any original nodes missing, or any new/custom nodes added.
    bool needsRestore(const std::string& spiritName) const;

    // Check if a spirit is a guide (has nodes with "questap" prefix or "tgc" in name)
    bool isGuide(const std::string& spiritName) const;
    // Rules (applies only to non-guides):
    // 1) If any node has isAdventurePass==true, it's NOT a travelling spirit
    // 2) It MUST contain at least one node whose name contains "emote_upgrade"
    // 3) Otherwise, if there exists a node with typ != "seasonal heart" AND that node is not a root/top node (dep != 0),
    //    then it's considered a travelling spirit.
    bool isTravellingSpirit(const std::string& spiritName) const;

    // Add a new empty spirit; if beforeSpirit is non-empty, insert before that spirit in file order
    bool addSpirit(const std::string& spiritName, const std::string& beforeSpirit = "");

    // Delete a spirit entirely
    bool deleteSpirit(const std::string& spiritName);

    // Move a node from one spirit to another (preserve node ID)
    bool moveNode(const std::string& fromSpirit, const std::string& toSpirit, uint64_t nodeId);

    // Shift the stored base position of a node by (dx,dy). Used to persist a drag operation
    // when the user releases the mouse so the node remains at the dropped location.
    bool moveNodeBase(const std::string& spiritName, uint64_t nodeId, float dx, float dy);
    // Shift the stored base position of ALL nodes in a spirit tree by (dx,dy).
    // This is intended for continuous tree dragging where the entire tree should
    // move with the cursor while preserving relative layout.
    bool moveTreeBase(const std::string& spiritName, float dx, float dy);
    // Shift the stored base position of only the subtree rooted at subtreeRootId by (dx,dy).
    // Returns the set of node IDs that were moved.
    bool moveSubtreeBase(const std::string& spiritName, uint64_t subtreeRootId, float dx, float dy,
                         std::unordered_set<uint64_t>* outMovedIds = nullptr);

    // Check if data is loaded
    bool isLoaded() const { return !m_trees.empty(); }
    
    // Get loaded file path
    const std::string& getLoadedFile() const { return m_loadedFile; }
    
private:
    void buildTree(SpiritTree& tree);
    void computeLayout(SpiritTree& tree);
    void layoutSubtree(SpiritTree& tree, SpiritNode& node, float x, float y, int depth);
    bool checkIfGuide(const SpiritTree& tree) const;
    
    std::unordered_map<std::string, SpiritTree> m_trees;
    std::vector<std::string> m_spiritNames;  // Regular spirits (in file order)
    std::vector<std::string> m_guideNames;   // Guide spirits (in file order)
    std::vector<std::string> m_allSpiritNamesOrdered;  // All spirits in original file order
    std::string m_loadedFile;

    // Cached original node IDs per spirit (populated at load time)
    std::unordered_map<std::string, std::unordered_set<uint64_t>> m_originalNodeIds;

    // Per-spirit dirty flags and cached results for needsReshape / needsRestore
    struct CachedState {
        bool reshapeDirty = true;
        bool restoreDirty = true;
        bool reshapeResult = false;
        bool restoreResult = false;
    };
    mutable std::unordered_map<std::string, CachedState> m_cachedState;

};

} // namespace Watercan
