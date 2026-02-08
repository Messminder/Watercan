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
                ImVec2* outDragTreeDelta = nullptr,
                // Reorder mode: shows overlay and highlight for reordering
                bool reorderMode = false);
    
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
    // When external highlights are active (e.g., reorder mode), only allow selecting highlighted nodes
    void setSelectedNodeId(uint64_t id) { 
        // If selectable set is active, enforce it strictly
        if (!m_selectableNodes.empty() && id != NO_NODE_ID && m_selectableNodes.count(id) == 0) return;
        m_selectedNodeId = id; m_selectedNodes.clear(); if (id != NO_NODE_ID) m_selectedNodes.insert(id); }
    void clearSelection() { m_selectedNodeId = NO_NODE_ID; m_selectedNodes.clear(); }

    const std::unordered_set<uint64_t>& getSelectedNodeIds() const { return m_selectedNodes; }
    bool isNodeSelected(uint64_t id) const { return m_selectedNodes.count(id) > 0; }
    void addNodeToSelection(uint64_t id) { if (id != NO_NODE_ID) { if (!m_selectableNodes.empty() && m_selectableNodes.count(id) == 0) return; m_selectedNodes.insert(id); m_selectedNodeId = id; } }
    void removeNodeFromSelection(uint64_t id) { bool wasPrimary = (m_selectedNodeId == id); m_selectedNodes.erase(id); if (m_selectedNodes.empty()) m_selectedNodeId = NO_NODE_ID; else if (wasPrimary) m_selectedNodeId = *m_selectedNodes.begin(); }

    // Public helper: query which node (if any) is located at the given screen position using the
    // renderer's last-known canvas origin and zoom. Returns NO_NODE_ID when none.
    uint64_t getNodeAtScreenPosition(const SpiritTree* tree, ImVec2 screenPos) const { 
        ImVec2 origin;
        origin.x = m_lastCanvasPos.x + m_lastCanvasSize.x * 0.5f + m_pan.x * m_zoom;
        origin.y = m_lastCanvasPos.y + m_lastCanvasSize.y * 0.75f + m_pan.y * m_zoom;
        return getNodeAtPosition(tree, screenPos, origin, m_zoom);
    }
    
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

    // Thaw a node so it participates in physics again (clears frozen/collision state and nudges velocity)
    void thawNode(uint64_t nodeId);

    // Deletion animation: start an animated 'split & fall & fade' for a node that's
    // about to be deleted. worldX/worldY are node coordinates in world space.
    void startDeleteAnimation(uint64_t nodeId, float worldX, float worldY, ImU32 fillColor = IM_COL32(190, 60, 60, 255));

    // Query a node fill color for a node (helper used by App when starting delete)
    ImU32 getNodeFillColorForNode(const SpiritNode& node) const; 
    
private:
    void drawNode(ImDrawList* drawList, const SpiritNode& node, 
                  ImVec2 origin, float zoom, bool isSelected);
    void drawConnection(ImDrawList* drawList, const SpiritNode& parent, 
                        const SpiritNode& child, ImVec2 origin, float zoom);
    
    ImU32 getNodeColor(const SpiritNode& node) const;
    ImU32 getNodeBorderColor(const SpiritNode& node) const;
    
    // Check if mouse is over a node, returns node ID or NO_NODE_ID when none
    uint64_t getNodeAtPosition(const SpiritTree* tree, ImVec2 mousePos, ImVec2 origin, float zoom) const;    
    // Get node offset from original position
    ImVec2 getNodeOffset(uint64_t nodeId) const;
    
    // View state
    float m_zoom = 1.0f;
    ImVec2 m_pan = {0.0f, 0.0f};
    bool m_isPanning = false;
    ImVec2 m_lastMousePos = {0.0f, 0.0f};

    // Last canvas geometry (updated each render) so callers can query node positions
    ImVec2 m_lastCanvasPos = {0.0f, 0.0f};
    ImVec2 m_lastCanvasSize = {0.0f, 0.0f};
    
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
    // Global collision suppression timer (seconds remaining) - when >0 collision checks are skipped
    float m_collisionSuppressRemaining = 0.0f;


    
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

    // Deletion animations (nodes that have been removed from the model but are still
    // animating on-screen as two halves)
    struct DeleteAnim {
        ImVec2 leftPos;
        ImVec2 rightPos;
        ImVec2 leftVel;
        ImVec2 rightVel;
        float leftRot = 0.0f;
        float rightRot = 0.0f;
        float alpha = 1.0f;
        double startTime = 0.0;
        float lifetime = 1.2f; // seconds
        float radius = NODE_RADIUS;
        ImU32 color = IM_COL32(190, 60, 60, 255);

    };
    std::unordered_map<uint64_t, DeleteAnim> m_deleteAnims;

    // Highlighted nodes (used by external modes like reorder) - renderer will adjust border
    std::unordered_set<uint64_t> m_highlightedNodes;
    // Selectable nodes (when non-empty, only these nodes may be selected)
    std::unordered_set<uint64_t> m_selectableNodes;

    // Box selection state (allow users to draw a marquee to select nodes)
    bool m_isBoxSelecting = false;
    ImVec2 m_boxSelectStart = ImVec2(0.0f, 0.0f);
    ImVec2 m_boxSelectCurrent = ImVec2(0.0f, 0.0f);
    std::unordered_set<uint64_t> m_boxSelectedNodes;
    static constexpr float BOX_SELECT_MIN_DRAG = 4.0f;

    // Helper to clear box selection state
    void clearBoxSelection() { m_isBoxSelecting = false; m_boxSelectedNodes.clear(); }

    // Arrow visibility state
    bool m_showArrows = true;
public:
    // Allow callers to highlight specific nodes so the renderer can visually emphasize them
    void setHighlightedNodes(const std::unordered_set<uint64_t>& nodes) { m_highlightedNodes = nodes; }
    void clearHighlightedNodes() { m_highlightedNodes.clear(); }
    // Restrict selection: when set, only these node ids may be selected
    void setSelectableNodes(const std::unordered_set<uint64_t>& nodes) { m_selectableNodes = nodes; }
    void clearSelectableNodes() { m_selectableNodes.clear(); }

    // Suppress collisions for a given amount of seconds (used after reorder)
    void suppressCollisions(float seconds) { m_collisionSuppressRemaining = std::max(m_collisionSuppressRemaining, seconds); m_collisionTime.clear(); m_frozenNodes.clear(); }

    // Group drag helpers: when multiple nodes are dragged together we temporarily
    // freeze and mark them free-floating so they stay locked together until release.
    void startGroupDrag(const std::unordered_set<uint64_t>& nodes);
    void endGroupDrag();

    // Snapping: detect stretched links and produce snap events for the app to commit
    // The renderer will color the line progressively and queue a pending snap when
    // the stretch holds beyond a threshold.
    struct SnapEvent { uint64_t parentId; uint64_t childId; };
    std::vector<SnapEvent> popPendingSnaps();

    // Arrow visibility (kept public so App can toggle)
    void setShowArrows(bool s) { m_showArrows = s; }
    void toggleShowArrows() { m_showArrows = !m_showArrows; }
    bool showArrows() const { return m_showArrows; }

private:
    bool m_groupDragging = false; // true while a multi-node grouped drag is in progress
    std::unordered_set<uint64_t> m_groupAddedFreeFloating; // nodes that were made free-floating by startGroupDrag
    std::unordered_set<uint64_t> m_groupAddedFrozen; // nodes that were frozen by startGroupDrag

    // Snap tracking
    std::unordered_map<uint64_t, float> m_snapTimers; // key -> seconds held beyond threshold
    std::vector<SnapEvent> m_pendingSnaps;

    // Get the screen position (in pixels) of the node's center, return false if node not found
    bool getNodeScreenPosition(const SpiritTree* tree, uint64_t nodeId, ImVec2* outPos) const;

};

} // namespace Watercan
