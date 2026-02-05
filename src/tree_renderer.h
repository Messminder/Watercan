#pragma once

#include "spirit_tree.h"
#include <imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <string>
#include <limits>

namespace Watercan {

// Handles rendering of spirit trees in the viewport
class TreeRenderer {
public:
    TreeRenderer() = default;

    
    // Render a spirit tree in the current ImGui window
    // createMode: click places new node, returns position via outClickPos
    // linkMode: click on node returns target node ID via outLinkTargetId
    // outRightClickedNodeId: returns ID of right-clicked node (for context menu)
    // deleteConfirmMode: shows delete confirmation overlay
    // Returns true if a relevant click occurred
    bool render(const SpiritTree* tree, bool createMode = false, ImVec2* outClickPos = nullptr,
                bool linkMode = false, uint64_t* outLinkTargetId = nullptr,
                uint64_t* outRightClickedNodeId = nullptr,
                bool deleteConfirmMode = false,
                bool readOnlyPreview = false,
                const std::unordered_map<std::string, std::array<float,4>>* typeColors = nullptr,
                // Optional outputs: when a drag ends, renderer will set outDragReleasedNodeId
                // and outDragFinalOffset to the final world offset to apply to the node base.
                uint64_t* outDragReleasedNodeId = nullptr,
                ImVec2* outDragFinalOffset = nullptr,
                // Optional continuous tree-drag outputs: while a non-free-floating node is being
                // dragged, the renderer will set outDraggingTreeNodeId to the dragged node ID
                // and outDragTreeDelta to the per-frame delta to apply to the whole tree base.
                uint64_t* outDraggingTreeNodeId = nullptr,
                ImVec2* outDragTreeDelta = nullptr);
    
    // Update physics (call each frame for spring simulation)
    // Pass the current tree for collision detection between nodes
    void updatePhysics(float deltaTime, const SpiritTree* tree = nullptr);
    
    // Pan and zoom controls
    void resetView();
    void setZoom(float zoom) { m_zoom = zoom; }
    float getZoom() const { return m_zoom; }
    void setPan(const ImVec2& pan) { m_pan = pan; }
    
    // Special sentinel used to indicate 'no node' (distinct from a real node id which can be 0)
    static constexpr uint64_t NO_NODE_ID = std::numeric_limits<uint64_t>::max();

    // Selection (supports multi-select via SHIFT)
    uint64_t getSelectedNodeId() const { return m_selectedNodeId; } // primary selected node
    void setSelectedNodeId(uint64_t id) { m_selectedNodeId = id; m_selectedNodes.clear(); if (id != NO_NODE_ID) m_selectedNodes.insert(id); }
    void clearSelection() { m_selectedNodeId = NO_NODE_ID; m_selectedNodes.clear(); }

    const std::unordered_set<uint64_t>& getSelectedNodeIds() const { return m_selectedNodes; }
    bool isNodeSelected(uint64_t id) const { return m_selectedNodes.count(id) > 0; }
    void addNodeToSelection(uint64_t id) { if (id != NO_NODE_ID) { m_selectedNodes.insert(id); m_selectedNodeId = id; } }
    void removeNodeFromSelection(uint64_t id) { bool wasPrimary = (m_selectedNodeId == id); m_selectedNodes.erase(id); if (m_selectedNodes.empty()) m_selectedNodeId = NO_NODE_ID; else if (wasPrimary) m_selectedNodeId = *m_selectedNodes.begin(); }
    
    // Reset all node offsets (return to computed positions)
    void resetNodeOffsets() { m_nodeOffsets.clear(); m_nodeVelocities.clear(); }

    // Apply a base-position shift for a node (oldBase - newBase). This is used when the
    // computed layout changes the base coordinates of a node; applyBaseShift allows the
    // visual position to remain steady and then smoothly spring into the new position.
    void applyBaseShift(uint64_t nodeId, float dx, float dy);

    // Clear the renderer's transient offset/velocity for a node after the model has
    // committed the base position (used to avoid a snap during drag-release commit)
    void clearNodeOffset(uint64_t nodeId);

    // Mark a node as free-floating so it doesn't snap back
    void setFreeFloating(uint64_t nodeId) { m_freeFloatingNodes.insert(nodeId); }
    void clearFreeFloating(uint64_t nodeId) { m_freeFloatingNodes.erase(nodeId); }
    bool isFreeFloating(uint64_t nodeId) const { return m_freeFloatingNodes.count(nodeId) > 0; }
    
private:
    void drawNode(ImDrawList* drawList, const SpiritNode& node, 
                  ImVec2 origin, float zoom, bool isSelected);
    void drawConnection(ImDrawList* drawList, const SpiritNode& parent, 
                        const SpiritNode& child, ImVec2 origin, float zoom);
    
    ImU32 getNodeColor(const SpiritNode& node) const;
    ImU32 getNodeBorderColor(const SpiritNode& node) const;
    
    // Check if mouse is over a node, returns node ID or NO_NODE_ID when none
    uint64_t getNodeAtPosition(const SpiritTree* tree, ImVec2 mousePos, ImVec2 origin, float zoom);    
    // Get node offset from original position
    ImVec2 getNodeOffset(uint64_t nodeId) const;
    
    // View state
    float m_zoom = 1.0f;
    ImVec2 m_pan = {0.0f, 0.0f};
    bool m_isPanning = false;
    ImVec2 m_lastMousePos = {0.0f, 0.0f};
    
    // Selection state
    uint64_t m_selectedNodeId = NO_NODE_ID;
    std::unordered_set<uint64_t> m_selectedNodes; // Multi-selection set (contains selected node ids)
    
    // Node dragging state
    uint64_t m_draggedNodeId = NO_NODE_ID;
    bool m_isDraggingNode = false;
    // Tree dragging state (dragging a non-free-floating node moves the entire tree via pan)
    bool m_isDraggingTree = false;
    ImVec2 m_dragTreeGrab = ImVec2(0.0f, 0.0f);

    // When dragging, the grab offset between cursor world position and visual node position
    // so the node will stick under the cursor: grab = (base + offset) - mouseWorld
    ImVec2 m_dragGrabOffset = ImVec2(0.0f, 0.0f);

    // Pointer to per-typ color map active during the current render call
    const std::unordered_map<std::string, std::array<float,4>>* m_currentRenderTypeColors = nullptr;
    // Indicates the current render call is a read-only preview (used to alter display, e.g. show typ instead of nm)
    bool m_currentRenderIsPreview = false;
    
    // Node offsets from original computed positions (for elastic dragging)
    std::unordered_map<uint64_t, ImVec2> m_nodeOffsets;
    std::unordered_map<uint64_t, ImVec2> m_nodeVelocities;
    std::unordered_set<uint64_t> m_freeFloatingNodes;  // Nodes that don't snap back
    std::unordered_set<uint64_t> m_frozenNodes;        // Nodes frozen due to sustained collision
    std::unordered_map<uint64_t, float> m_collisionTime; // Time spent in collision with low velocity
    
    // Spring physics settings
    static constexpr float SPRING_STIFFNESS = 15.0f;  // How fast nodes return
    static constexpr float SPRING_DAMPING = 6.0f;     // How quickly motion settles
    static constexpr float VELOCITY_THRESHOLD = 0.1f; // Stop when velocity is tiny
    static constexpr float OFFSET_THRESHOLD = 0.5f;   // Stop when offset is tiny
    
    // Collision settings
    static constexpr float COLLISION_RADIUS = 30.0f;  // Collision radius per node
    static constexpr float COLLISION_STRENGTH = 150.0f; // Push force strength (gentler)
    static constexpr float FREEZE_TIME_THRESHOLD = 0.5f; // Time in seconds before freezing
    static constexpr float FREEZE_VELOCITY_THRESHOLD = 0.4f; // Velocity threshold for freeze consideration
    
    // Visual settings
    static constexpr float NODE_RADIUS = 25.0f;
    static constexpr float CONNECTION_THICKNESS = 2.0f;


};

} // namespace Watercan
