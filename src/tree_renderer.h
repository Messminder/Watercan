#pragma once

#include "spirit_tree.h"
#include <imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <string>

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
                const std::unordered_map<std::string, std::array<float,4>>* typeColors = nullptr);
    
    // Update physics (call each frame for spring simulation)
    // Pass the current tree for collision detection between nodes
    void updatePhysics(float deltaTime, const SpiritTree* tree = nullptr);
    
    // Pan and zoom controls
    void resetView();
    void setZoom(float zoom) { m_zoom = zoom; }
    float getZoom() const { return m_zoom; }
    void setPan(const ImVec2& pan) { m_pan = pan; }
    
    // Selection
    uint64_t getSelectedNodeId() const { return m_selectedNodeId; }
    void setSelectedNodeId(uint64_t id) { m_selectedNodeId = id; }
    void clearSelection() { m_selectedNodeId = 0; }
    
    // Reset all node offsets (return to computed positions)
    void resetNodeOffsets() { m_nodeOffsets.clear(); m_nodeVelocities.clear(); }
    
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
    
    // Check if mouse is over a node, returns node ID or 0
    uint64_t getNodeAtPosition(const SpiritTree* tree, ImVec2 mousePos, ImVec2 origin, float zoom);
    
    // Get node offset from original position
    ImVec2 getNodeOffset(uint64_t nodeId) const;
    
    // View state
    float m_zoom = 1.0f;
    ImVec2 m_pan = {0.0f, 0.0f};
    bool m_isPanning = false;
    ImVec2 m_lastMousePos = {0.0f, 0.0f};
    
    // Selection state
    uint64_t m_selectedNodeId = 0;
    
    // Node dragging state
    uint64_t m_draggedNodeId = 0;
    bool m_isDraggingNode = false;

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
