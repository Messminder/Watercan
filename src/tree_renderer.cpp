#include "tree_renderer.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <cstdio>

namespace Watercan {

void TreeRenderer::updatePhysics(float deltaTime, const SpiritTree* tree) {
    // Track which nodes are currently in collision
    std::unordered_set<uint64_t> nodesInCollision;
    
    // Apply collision forces between nodes
    if (tree && !tree->nodes.empty()) {
        // Build list of node positions (base + offset)
        std::vector<std::pair<uint64_t, ImVec2>> nodePositions;
        for (const auto& node : tree->nodes) {
            ImVec2 offset = getNodeOffset(node.id);
            ImVec2 pos(node.x + offset.x, node.y + offset.y);
            nodePositions.emplace_back(node.id, pos);
        }
        
        // Check each pair of nodes for collision
        for (size_t i = 0; i < nodePositions.size(); ++i) {
            for (size_t j = i + 1; j < nodePositions.size(); ++j) {
                uint64_t idA = nodePositions[i].first;
                uint64_t idB = nodePositions[j].first;
                ImVec2 posA = nodePositions[i].second;
                ImVec2 posB = nodePositions[j].second;
                
                float dx = posB.x - posA.x;
                float dy = posB.y - posA.y;
                float dist = sqrtf(dx * dx + dy * dy);
                float minDist = COLLISION_RADIUS * 2.0f;
                
                if (dist < minDist && dist > 0.001f) {
                    // Mark nodes as in collision
                    nodesInCollision.insert(idA);
                    nodesInCollision.insert(idB);
                    
                    // Skip frozen nodes
                    bool aFrozen = m_frozenNodes.count(idA) > 0;
                    bool bFrozen = m_frozenNodes.count(idB) > 0;
                    
                    // Nodes are overlapping, push them apart
                    float overlap = minDist - dist;
                    float nx = dx / dist;
                    float ny = dy / dist;
                    
                    // Apply push force to both nodes
                    float pushForce = overlap * COLLISION_STRENGTH * deltaTime;
                    
                    // Don't push the node being dragged or frozen nodes
                    if (!(idA == m_draggedNodeId && m_isDraggingNode) && !aFrozen) {
                        m_nodeOffsets[idA].x -= nx * pushForce * 0.5f;
                        m_nodeOffsets[idA].y -= ny * pushForce * 0.5f;
                        m_nodeVelocities[idA].x -= nx * pushForce * 2.0f;
                        m_nodeVelocities[idA].y -= ny * pushForce * 2.0f;
                    }
                    
                    if (!(idB == m_draggedNodeId && m_isDraggingNode) && !bFrozen) {
                        m_nodeOffsets[idB].x += nx * pushForce * 0.5f;
                        m_nodeOffsets[idB].y += ny * pushForce * 0.5f;
                        m_nodeVelocities[idB].x += nx * pushForce * 2.0f;
                        m_nodeVelocities[idB].y += ny * pushForce * 2.0f;
                    }
                }
            }
        }
    }
    
    // Update collision time tracking and freeze nodes that have been oscillating too long
    for (auto& [nodeId, offset] : m_nodeOffsets) {
        // Skip dragged node
        if (nodeId == m_draggedNodeId && m_isDraggingNode) {
            m_collisionTime.erase(nodeId);
            m_frozenNodes.erase(nodeId);
            continue;
        }
        
        ImVec2& velocity = m_nodeVelocities[nodeId];
        float velocityMag = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        
        if (nodesInCollision.count(nodeId) > 0 && velocityMag < FREEZE_VELOCITY_THRESHOLD) {
            // Node is in collision with low velocity, accumulate time
            m_collisionTime[nodeId] += deltaTime;
            
            if (m_collisionTime[nodeId] >= FREEZE_TIME_THRESHOLD) {
                // Freeze this node
                m_frozenNodes.insert(nodeId);
                velocity.x = 0;
                velocity.y = 0;
            }
        } else if (nodesInCollision.count(nodeId) == 0) {
            // Node is not in collision, reset collision time and unfreeze
            m_collisionTime.erase(nodeId);
            m_frozenNodes.erase(nodeId);
        }
    }
    
    // Apply spring physics to pull nodes back to their original positions
    std::vector<uint64_t> toRemove;

    // NOTE: if position changes are done via applyBaseShift by the app
    // (oldBase - newBase stored as offset), the spring physics above will
    // bring offsets smoothly back towards zero making nodes animate.
    
    for (auto& [nodeId, offset] : m_nodeOffsets) {
        // Don't apply spring force to the node being dragged
        if (nodeId == m_draggedNodeId && m_isDraggingNode) {
            continue;
        }
        
        // Don't apply spring force to free-floating nodes
        if (m_freeFloatingNodes.count(nodeId) > 0) {
            continue;
        }
        
        // Don't apply spring force to frozen nodes (they stay in place)
        if (m_frozenNodes.count(nodeId) > 0) {
            continue;
        }
        
        // Get or create velocity
        ImVec2& velocity = m_nodeVelocities[nodeId];
        
        // Spring force: F = -k * x (where x is offset from rest position)
        // We want to return to offset (0,0)
        float springForceX = -SPRING_STIFFNESS * offset.x;
        float springForceY = -SPRING_STIFFNESS * offset.y;
        
        // Damping force: F = -c * v
        float dampingForceX = -SPRING_DAMPING * velocity.x;
        float dampingForceY = -SPRING_DAMPING * velocity.y;
        
        // Total acceleration (assuming unit mass)
        float accelX = springForceX + dampingForceX;
        float accelY = springForceY + dampingForceY;
        
        // Update velocity
        velocity.x += accelX * deltaTime;
        velocity.y += accelY * deltaTime;
        
        // Update position
        offset.x += velocity.x * deltaTime;
        offset.y += velocity.y * deltaTime;
        
        // Check if node has essentially returned to rest
        float offsetMag = sqrtf(offset.x * offset.x + offset.y * offset.y);
        float velocityMag = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        
        if (offsetMag < OFFSET_THRESHOLD && velocityMag < VELOCITY_THRESHOLD) {
            toRemove.push_back(nodeId);
        }
    }
    
    // Remove nodes that have returned to rest
    for (uint64_t id : toRemove) {
        m_nodeOffsets.erase(id);
        m_nodeVelocities.erase(id);
    }
}

void TreeRenderer::applyBaseShift(uint64_t nodeId, float dx, float dy) {
    // Apply an immediate offset equal to oldBase - newBase so the visual world
    // position remains unchanged. The spring physics in updatePhysics will
    // then pull the offset smoothly back to zero, animating the node into place.
    m_nodeOffsets[nodeId].x += dx;
    m_nodeOffsets[nodeId].y += dy;
    // Kick the velocity toward the direction of the target (negative of offset)
    // to give the motion some responsiveness.
    m_nodeVelocities[nodeId].x += -dx * 8.0f;
    m_nodeVelocities[nodeId].y += -dy * 8.0f;
}

ImVec2 TreeRenderer::getNodeOffset(uint64_t nodeId) const {
    auto it = m_nodeOffsets.find(nodeId);
    if (it != m_nodeOffsets.end()) {
        return it->second;
    }
    return ImVec2(0.0f, 0.0f);
}

void TreeRenderer::clearNodeOffset(uint64_t nodeId) {
    m_nodeOffsets.erase(nodeId);
    m_nodeVelocities.erase(nodeId);
    if (m_draggedNodeId == nodeId) {
        m_dragGrabOffset = ImVec2(0.0f, 0.0f);
    }
}

bool TreeRenderer::render(const SpiritTree* tree, bool createMode, ImVec2* outClickPos,
                          bool linkMode, uint64_t* outLinkTargetId,
                          uint64_t* outRightClickedNodeId,
                          bool deleteConfirmMode,
                          bool readOnlyPreview,
                          const std::unordered_map<std::string, std::array<float,4>>* typeColors,
                          uint64_t* outDragReleasedNodeId,
                          ImVec2* outDragFinalOffset,
                          uint64_t* outDraggingTreeNodeId,
                          ImVec2* outDragTreeDelta) {
    bool actionOccurred = false;
    // Store the provided type color map for use by getNodeColor/getNodeBorderColor
    m_currentRenderTypeColors = typeColors;
    // Remember whether this render is a read-only preview (affects label rendering, interactivity)
    m_currentRenderIsPreview = readOnlyPreview;
    // Prepare optional drag output placeholders
    if (outDragReleasedNodeId) *outDragReleasedNodeId = 0;
    if (outDragFinalOffset) { outDragFinalOffset->x = 0.0f; outDragFinalOffset->y = 0.0f; }
    if (outDraggingTreeNodeId) *outDraggingTreeNodeId = 0;
    if (outDragTreeDelta) { outDragTreeDelta->x = 0.0f; outDragTreeDelta->y = 0.0f; }
    
    if (!tree || tree->nodes.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), 
                          "Select a spirit from the list to view its tree");
        return false;
    }
    
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    
    // Ensure minimum size
    if (canvasSize.x < 50.0f) canvasSize.x = 50.0f;
    if (canvasSize.y < 50.0f) canvasSize.y = 50.0f;
    
    // Create an invisible button for interaction
    ImGui::InvisibleButton("tree_canvas", canvasSize, 
                          ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Push clipping rectangle to prevent drawing outside the canvas area
    drawList->PushClipRect(canvasPos, 
                          ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), 
                          true);
    
    // Draw background
    drawList->AddRectFilled(canvasPos, 
                           ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                           IM_COL32(25, 30, 40, 255));
    
    // Draw subtle grid (fixed size, not affected by zoom)
    const float gridStep = 50.0f;  // Fixed grid size
    ImU32 gridColor = IM_COL32(50, 55, 65, 100);
    
    float offsetX = fmodf(m_pan.x * m_zoom, gridStep);
    float offsetY = fmodf(m_pan.y * m_zoom, gridStep);
    
    for (float x = offsetX; x < canvasSize.x; x += gridStep) {
        drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y),
                         ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y),
                         gridColor);
    }
    for (float y = offsetY; y < canvasSize.y; y += gridStep) {
        drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y),
                         ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y),
                         gridColor);
    }

    // Legend will be drawn after nodes so it appears on top of the graph
    
    // Calculate origin point (center of canvas, adjusted for tree bounds)
    // Root should be at bottom, tree grows upward
    ImVec2 origin;
    origin.x = canvasPos.x + canvasSize.x * 0.5f + m_pan.x * m_zoom;
    origin.y = canvasPos.y + canvasSize.y * 0.75f + m_pan.y * m_zoom;  // Root near bottom
    
    // Handle panning with middle mouse or right mouse (disabled during delete confirmation)
    // If readOnlyPreview is requested, suppress interactive behavior
    if (isHovered && !deleteConfirmMode && !readOnlyPreview) {
        ImGuiIO& io = ImGui::GetIO();
        
        // Handle right-click on nodes (for context menu)
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImVec2 mousePos = io.MousePos;
            uint64_t rightClickedNode = getNodeAtPosition(tree, mousePos, origin, m_zoom);
            if (rightClickedNode != 0 && outRightClickedNodeId) {
                *outRightClickedNodeId = rightClickedNode;
                // Select behavior: respect SHIFT to multi-select
                bool shift = io.KeyShift;
                if (shift) {
                    if (isNodeSelected(rightClickedNode)) removeNodeFromSelection(rightClickedNode);
                    else addNodeToSelection(rightClickedNode);
                } else {
                    clearSelection();
                    addNodeToSelection(rightClickedNode);
                }
            } else {
                // Right-clicked empty space: report click position in world coordinates so the app can show a context menu
                float worldX = (mousePos.x - origin.x) / m_zoom;
                float worldY = -(mousePos.y - origin.y) / m_zoom;  // Invert Y
                if (outClickPos) {
                    outClickPos->x = worldX;
                    outClickPos->y = worldY;
                }
                actionOccurred = true;
            }
        }
        
        // In create mode, only handle click to get position
        if (createMode) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 mousePos = io.MousePos;
                // Convert screen position to world coordinates
                float worldX = (mousePos.x - origin.x) / m_zoom;
                float worldY = -(mousePos.y - origin.y) / m_zoom;  // Invert Y
                
                if (outClickPos) {
                    outClickPos->x = worldX;
                    outClickPos->y = worldY;
                }
                actionOccurred = true;
            }
        } else if (linkMode) {
            // In link mode, clicking a node selects it as the link target
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 mousePos = io.MousePos;
                uint64_t clickedNode = getNodeAtPosition(tree, mousePos, origin, m_zoom);
                if (clickedNode != 0 && outLinkTargetId) {
                    *outLinkTargetId = clickedNode;
                    actionOccurred = true;
                }
            }
        } else {
            // Normal mode - zoom with scroll wheel
            if (io.MouseWheel != 0.0f) {
                float zoomDelta = io.MouseWheel * 0.1f;
                m_zoom = std::clamp(m_zoom + zoomDelta, 0.25f, 3.0f);
            }
            
            // Handle left mouse button for selection and dragging
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 mousePos = io.MousePos;
                uint64_t clickedNode = getNodeAtPosition(tree, mousePos, origin, m_zoom);
                if (clickedNode != 0) {
                    // Check for multi-select (SHIFT held)
                    ImGuiIO& io = ImGui::GetIO();
                    bool shift = io.KeyShift;

                    if (shift) {
                        // Toggle selection membership
                        if (isNodeSelected(clickedNode)) {
                            removeNodeFromSelection(clickedNode);
                        } else {
                            addNodeToSelection(clickedNode);
                        }
                        // Update click position for external handlers
                        float worldX = (mousePos.x - origin.x) / m_zoom;
                        float worldY = -(mousePos.y - origin.y) / m_zoom;  // Invert Y
                        if (outClickPos) { outClickPos->x = worldX; outClickPos->y = worldY; }
                    } else {
                        // Single selection: clear others and select this node
                        clearSelection();
                        addNodeToSelection(clickedNode);

                        // Determine drag mode: free-floating nodes drag individually; regular nodes drag the whole tree
                        float worldX = (mousePos.x - origin.x) / m_zoom;
                        float worldY = -(mousePos.y - origin.y) / m_zoom;  // Invert Y
                        if (isFreeFloating(clickedNode)) {
                            m_draggedNodeId = clickedNode;
                            m_isDraggingNode = true;

                            // Reset velocity when starting to drag
                            m_nodeVelocities[clickedNode] = ImVec2(0.0f, 0.0f);

                            // Compute grab offset so the node sticks to the cursor
                            ImVec2 currentOffset = getNodeOffset(clickedNode);
                            // Find base position of clicked node
                            const SpiritNode* baseNode = nullptr;
                            for (const auto& n : tree->nodes) if (n.id == clickedNode) { baseNode = &n; break; }
                            if (baseNode) {
                                m_dragGrabOffset.x = (baseNode->x + currentOffset.x) - worldX;
                                m_dragGrabOffset.y = (baseNode->y + currentOffset.y) - worldY;
                            } else {
                                m_dragGrabOffset = ImVec2(0.0f, 0.0f);
                            }
                            if (outClickPos) {
                                outClickPos->x = worldX;
                                outClickPos->y = worldY;
                            }
                        } else {
                            // Start a tree drag: move the whole tree via pan so shape is preserved
                            m_draggedNodeId = clickedNode;
                            m_isDraggingTree = true;
                            // Compute grab between node base and mouse world coords
                            const SpiritNode* baseNode = nullptr;
                            for (const auto& n : tree->nodes) if (n.id == clickedNode) { baseNode = &n; break; }
                            if (baseNode) {
                                m_dragTreeGrab.x = baseNode->x - worldX;
                                m_dragTreeGrab.y = baseNode->y - worldY;
                            } else {
                                m_dragTreeGrab = ImVec2(0.0f, 0.0f);
                            }
                        }
                    }
                    actionOccurred = true;
                }
            }
            
            // Handle node/tree dragging
            if ((m_isDraggingNode || m_isDraggingTree) && m_draggedNodeId != 0) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (m_isDraggingTree) {
                        // Tree drag: compute desired base position for the dragged node and report the
                        // delta to the caller so it can immediately shift the model (move all nodes).
                        float worldX = (io.MousePos.x - origin.x) / m_zoom;
                        float worldY = -(io.MousePos.y - origin.y) / m_zoom;  // Invert Y

                        // desired base for the node
                        float desiredX = worldX + m_dragTreeGrab.x;
                        float desiredY = worldY + m_dragTreeGrab.y;

                        // Find base position for the dragged node
                        const SpiritNode* baseNode = nullptr;
                        for (const auto& n : tree->nodes) if (n.id == m_draggedNodeId) { baseNode = &n; break; }
                        if (baseNode) {
                            float dx = desiredX - baseNode->x;
                            float dy = desiredY - baseNode->y;

                            // Report this per-frame delta to the app so it can call moveTreeBase
                            if (outDraggingTreeNodeId) *outDraggingTreeNodeId = m_draggedNodeId;
                            if (outDragTreeDelta) { outDragTreeDelta->x = dx; outDragTreeDelta->y = dy; }
                        }
                    } else {
                        // Node drag (free-floating): stick node under cursor
                        // Compute the current mouse world position
                        float worldX = (io.MousePos.x - origin.x) / m_zoom;
                        float worldY = -(io.MousePos.y - origin.y) / m_zoom;  // Invert Y

                        // Find base position for the dragged node
                        const SpiritNode* baseNode = nullptr;
                        for (const auto& n : tree->nodes) if (n.id == m_draggedNodeId) { baseNode = &n; break; }

                        if (baseNode) {
                            // Maintain the grab offset so the node stays under the cursor
                            ImVec2 desiredOffset;
                            desiredOffset.x = worldX + m_dragGrabOffset.x - baseNode->x;
                            desiredOffset.y = worldY + m_dragGrabOffset.y - baseNode->y;

                            m_nodeOffsets[m_draggedNodeId] = desiredOffset;
                            // Zero velocity to avoid spring inertia while dragging
                            m_nodeVelocities[m_draggedNodeId] = ImVec2(0.0f, 0.0f);
                        } else {
                            // Fallback to delta-based behavior if the base node can't be found
                            ImVec2 delta = io.MouseDelta;
                            float worldDeltaX = delta.x / m_zoom;
                            float worldDeltaY = -delta.y / m_zoom;  // Invert Y
                            ImVec2& offset = m_nodeOffsets[m_draggedNodeId];
                            offset.x += worldDeltaX;
                            offset.y += worldDeltaY;
                        }
                    }
                } else {
                    // Mouse released - handle release for both modes
                    if (m_isDraggingNode) {
                        // Node drag ended: report final offset
                        if (m_draggedNodeId != 0) {
                            ImVec2 finalOffset = m_nodeOffsets[m_draggedNodeId];
                            if (outDragReleasedNodeId) *outDragReleasedNodeId = m_draggedNodeId;
                            if (outDragFinalOffset) { outDragFinalOffset->x = finalOffset.x; outDragFinalOffset->y = finalOffset.y; }
                            // Do NOT erase offsets here; the app will call moveNodeBase and then
                            // notify the renderer to clear the offset so there is no frame where
                            // the visual position snaps back to the old base.
                        }
                    }
                    // End dragging state
                    m_isDraggingNode = false;
                    m_isDraggingTree = false;
                    m_draggedNodeId = 0;
                }
            }
            
            // Pan with right mouse button (only if not clicking on a node)
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                ImVec2 delta = io.MouseDelta;
                m_pan.x += delta.x / m_zoom;
                m_pan.y += delta.y / m_zoom;
            }
            
            // Pan with middle mouse button
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                ImVec2 delta = io.MouseDelta;
                m_pan.x += delta.x / m_zoom;
                m_pan.y += delta.y / m_zoom;
            }

            // Mouse wrapping disabled: panning uses raw MouseDelta and does not modify ImGui mouse position.
        }
    } else {
        // Mouse left the canvas while dragging - release the node
        if (m_isDraggingNode && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Mouse left canvas while dragging - commit final offset
            if (m_draggedNodeId != 0) {
                ImVec2 finalOffset = m_nodeOffsets[m_draggedNodeId];
                if (outDragReleasedNodeId) *outDragReleasedNodeId = m_draggedNodeId;
                if (outDragFinalOffset) { outDragFinalOffset->x = finalOffset.x; outDragFinalOffset->y = finalOffset.y; }
                // Do NOT erase offsets here; caller will commit base and then tell renderer to clear.
            }
            m_isDraggingNode = false;
            m_draggedNodeId = 0;
        }
    }
    
    // Recalculate origin after potential pan changes
    origin.x = canvasPos.x + canvasSize.x * 0.5f + m_pan.x * m_zoom;
    origin.y = canvasPos.y + canvasSize.y * 0.75f + m_pan.y * m_zoom;
    
    // Create node lookup
    std::unordered_map<uint64_t, const SpiritNode*> nodeMap;
    for (const auto& node : tree->nodes) {
        nodeMap[node.id] = &node;
    }
    
    // Draw connections first (behind nodes)
    for (const auto& node : tree->nodes) {
        for (uint64_t childId : node.children) {
            auto it = nodeMap.find(childId);
            if (it != nodeMap.end()) {
                drawConnection(drawList, node, *it->second, origin, m_zoom);
            }
        }
    }
    
    // Draw nodes
    for (const auto& node : tree->nodes) {
        bool isSelected = isNodeSelected(node.id);
        drawNode(drawList, node, origin, m_zoom, isSelected);
    }
    
    // Draw border
    drawList->AddRect(canvasPos, 
                     ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                     IM_COL32(70, 75, 85, 255));
    
    // Draw create mode overlay
    if (createMode) {
        // Dim the canvas slightly
        drawList->AddRectFilled(canvasPos,
                               ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                               IM_COL32(0, 0, 0, 100));
        
        // Draw instruction text
        const char* text = "Click anywhere to create a new node";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImVec2 textPos(canvasPos.x + (canvasSize.x - textSize.x) * 0.5f,
                       canvasPos.y + canvasSize.y * 0.3f);
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), text);
        
        // Draw border in a different color to indicate create mode
        drawList->AddRect(canvasPos, 
                         ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                         IM_COL32(100, 200, 100, 255), 0.0f, 0, 2.0f);
    }
    
    // Draw link mode overlay
    if (linkMode) {
        // Dim the canvas slightly
        drawList->AddRectFilled(canvasPos,
                               ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                               IM_COL32(0, 0, 0, 100));
        
        // Draw instruction text
        const char* text = "Click a node to link as parent";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImVec2 textPos(canvasPos.x + (canvasSize.x - textSize.x) * 0.5f,
                       canvasPos.y + canvasSize.y * 0.3f);
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), text);
        
        // Draw border in a different color to indicate link mode
        drawList->AddRect(canvasPos, 
                         ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                         IM_COL32(100, 150, 255, 255), 0.0f, 0, 2.0f);
    }
    
    // Draw delete confirmation overlay (but skip if readOnlyPreview requested)
    if (deleteConfirmMode && !readOnlyPreview) {
        // Dim the canvas more heavily for danger action
        drawList->AddRectFilled(canvasPos,
                               ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                               IM_COL32(0, 0, 0, 150));
        
        // Draw warning text
        const char* text = "Are you sure you want to krill this node?";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImVec2 textPos(canvasPos.x + (canvasSize.x - textSize.x) * 0.5f,
                       canvasPos.y + canvasSize.y * 0.35f);
        drawList->AddText(textPos, IM_COL32(255, 100, 100, 255), text);
        
        // Draw border in red to indicate danger
        drawList->AddRect(canvasPos, 
                         ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                         IM_COL32(255, 80, 80, 255), 0.0f, 0, 2.0f);
    }
    
    // Draw legend anchored at top-left of the canvas showing indicator letters (drawn last so it stays on top)
    // Don't draw the legend for read-only previews or when the Color Codes modal is open
    if (!readOnlyPreview && !ImGui::IsPopupOpen("Color Codes")) {
        const float legendPad = 8.0f; // fixed pixels
        ImVec2 legendPos(canvasPos.x + 8.0f, canvasPos.y + 8.0f);
        float entryHeight = ImGui::GetFontSize() + 6.0f; // fixed height
        std::vector<std::pair<std::string, std::string>> legendItems = {
            {"O", "Outfits"},
            {"E", "Expression"},
            {"M", "Music sheets"},
            {"L", "Lootbox/Spells"},
            {"H", "Season heart"},
            {"H", "Hearts"},
            {"TP", "Teleports"},
            {"*", "Adventure Pass (AP)"},
            {"?", "Unknown"},
        };

        // Compute legend width dynamically based on text sizes so longer descriptions (like "Unknown") fit
        float maxTextWidth = 0.0f;
        ImVec2 titleSize = ImGui::CalcTextSize("Legend:");
        maxTextWidth = std::max(maxTextWidth, titleSize.x);
        for (size_t i = 0; i < legendItems.size(); ++i) {
            ImVec2 keySize = ImGui::CalcTextSize(legendItems[i].first.c_str());
            ImVec2 descSize = ImGui::CalcTextSize(legendItems[i].second.c_str());
            float itemWidth = keySize.x + 6.0f + descSize.x; // fixed spacing
            if (legendItems[i].first.size() > 1) itemWidth += 8.0f;
            maxTextWidth = std::max(maxTextWidth, itemWidth);
        }

        float minWidth = 140.0f; // fixed pixels
        float legendWidth = std::max(minWidth, maxTextWidth + legendPad * 2.0f + 16.0f);
        float extraLegendBottom = 16.0f;
        float legendHeight = entryHeight * (float)legendItems.size() + legendPad * 2.0f + extraLegendBottom;

        // Background box
        ImU32 legendBg = IM_COL32(20, 25, 30, 220);
        ImU32 legendBorder = IM_COL32(80, 90, 100, 200);
        drawList->AddRectFilled(legendPos, ImVec2(legendPos.x + legendWidth, legendPos.y + legendHeight), legendBg, 4.0f);
        drawList->AddRect(legendPos, ImVec2(legendPos.x + legendWidth, legendPos.y + legendHeight), legendBorder, 4.0f, 0, 1.0f);

        // Title
        ImVec2 titlePos(legendPos.x + legendPad, legendPos.y + 4.0f);
        drawList->AddText(titlePos, IM_COL32(200, 200, 220, 255), "Legend:");

        // Draw each legend entry
        for (size_t i = 0; i < legendItems.size(); ++i) {
            float y = legendPos.y + legendPad + 18.0f + i * entryHeight;
            ImVec2 labelPos(legendPos.x + legendPad, y);

            std::string key = legendItems[i].first;
            std::string desc = legendItems[i].second;
            ImU32 keyColor = IM_COL32(150, 150, 150, 200);
            if (desc == "Season heart") {
                float time = (float)ImGui::GetTime();
                float pulse = (std::sin(time * 2.0f) + 1.0f) * 0.5f;
                int r = (int)(150 + (255 - 150) * pulse);
                int g = (int)(150 + (215 - 150) * pulse);
                int b = (int)(150 + (0 - 150) * pulse);
                if (b < 0) b = 0;
                keyColor = IM_COL32(r, g, b, 255);
            }

            drawList->AddText(ImVec2(labelPos.x, labelPos.y), keyColor, key.c_str());

            // Draw AP star sample
            if (key == "*") {
                ImVec2 starPos(labelPos.x + 14.0f, labelPos.y + 6.0f);
                float starSize = 4.0f;
                drawList->AddCircleFilled(starPos, starSize, IM_COL32(255, 215, 0, 255));
                drawList->AddCircle(starPos, starSize, IM_COL32(200, 160, 0, 255), 0, 1.0f);
            }

            ImVec2 descPos(labelPos.x + 20.0f, labelPos.y);
            drawList->AddText(descPos, IM_COL32(200, 200, 220, 200), legendItems[i].second.c_str());
        }
    }

    // Pop the clipping rectangle
    drawList->PopClipRect();

    // Clear temporary render state
    m_currentRenderTypeColors = nullptr;
    m_currentRenderIsPreview = false;

    return actionOccurred;
}

void TreeRenderer::drawNode(ImDrawList* drawList, const SpiritNode& node, 
                            ImVec2 origin, float zoom, bool isSelected) {
    // Get node offset from dragging
    ImVec2 offset = getNodeOffset(node.id);
    
    // Convert node position to screen position (with offset applied)
    // Note: y is inverted (positive y goes up in logic, but down on screen)
    ImVec2 screenPos;
    screenPos.x = origin.x + (node.x + offset.x) * zoom;
    screenPos.y = origin.y - (node.y + offset.y) * zoom;  // Invert Y so "up" in tree is "up" on screen
    
    float radius = NODE_RADIUS * zoom;
    
    // Check for ID mismatch
    uint32_t expectedId = fnv1a32(node.name);
    bool idMismatch = (node.id != expectedId);
    
    // Get colors
    ImU32 fillColor = getNodeColor(node);
    ImU32 borderColor = getNodeBorderColor(node);
    
    // Draw selection circle if selected (green if ID matches, red if mismatch)
    if (isSelected) {
        float selectionRadius = radius + 6.0f * zoom;
        ImU32 selectionColor = idMismatch ? IM_COL32(255, 50, 50, 255) : IM_COL32(50, 255, 50, 255);
        drawList->AddCircle(screenPos, selectionRadius, selectionColor, 0, 3.0f * zoom);
    }
    
    // Draw node circle with shadow
    ImVec2 shadowOffset(2 * zoom, 2 * zoom);
    drawList->AddCircleFilled(
        ImVec2(screenPos.x + shadowOffset.x, screenPos.y + shadowOffset.y),
        radius, IM_COL32(0, 0, 0, 80));
    
    // Main circle
    drawList->AddCircleFilled(screenPos, radius, fillColor);
    drawList->AddCircle(screenPos, radius, borderColor, 0, 2.0f * zoom);
    
    // Draw AP indicator (small star/diamond) at north west of node
    if (node.isAdventurePass) {
        float starSize = 6.0f * zoom;
        ImVec2 starPos(screenPos.x - radius * 0.7f, screenPos.y - radius * 0.7f);
        drawList->AddCircleFilled(starPos, starSize, IM_COL32(255, 215, 0, 255));
        drawList->AddCircle(starPos, starSize, IM_COL32(200, 160, 0, 255), 0, 1.5f);
    }
    
    // Draw label inside the node: in preview show typ (highlighted); otherwise show name (nm)
    std::string label;
    ImU32 labelColor = IM_COL32(255, 255, 255, 255);
    if (m_currentRenderIsPreview) {
        if (!node.type.empty()) {
            label = node.type;
            labelColor = IM_COL32(220, 220, 140, 255); // slightly warm highlight to indicate typ
        } else {
            // Fallback when typ is missing - show nm but in a distinct color so it's obvious
            label = node.name;
            labelColor = IM_COL32(200, 120, 120, 255);
        }
    } else {
        label = node.name;
        labelColor = IM_COL32(255, 255, 255, 255);
    }

    if (zoom >= 0.5f && !label.empty()) {
        ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        ImVec2 textPos(screenPos.x - textSize.x * 0.5f, screenPos.y - textSize.y * 0.5f);
        drawList->AddText(textPos, labelColor, label.c_str());

        // When zoomed in enough, display the node's object id under the nm
        // (show as 0xHEX for readability). Skip in preview mode.
        // Show node's 'id' attribute when zoomed in more (higher threshold)
        constexpr float ID_ZOOM_THRESHOLD = 1.8f; // show only when quite zoomed in
        if (!m_currentRenderIsPreview && zoom >= ID_ZOOM_THRESHOLD) {
            char idBuf[64];
            // Display as decimal 'id: 12345' so it matches the node attribute users expect
            std::snprintf(idBuf, sizeof(idBuf), "id: %llu", (unsigned long long)node.id);
            ImVec2 idSize = ImGui::CalcTextSize(idBuf);
            ImVec2 idPos(screenPos.x - idSize.x * 0.5f, textPos.y + textSize.y + 3.0f * zoom);
            drawList->AddText(idPos, IM_COL32(200, 200, 210, 240), idBuf);
        }
    }
    
    // Draw type indicator at south east of node
    if (zoom >= 0.7f) {
        std::string typeLabel;
        bool isSeasonHeart = false;
        if (node.type == "outfit") typeLabel = "O";
        else if (node.type == "spirit_upgrade") typeLabel = "E";
        else if (node.type == "music") typeLabel = "M";
        else if (node.type == "lootbox") typeLabel = "L";
        else if (node.type == "season_heart") { typeLabel = "H"; isSeasonHeart = true; }
        else if (node.type == "heart") typeLabel = "H";
        else if (node.type == "teleport_to") typeLabel = "TP";
        else typeLabel = "?";
        
        ImVec2 labelPos(screenPos.x + radius * 1.0f, screenPos.y + radius * 1.0f);
        
        // Draw type letter - pulse to gold for season hearts
        ImU32 typeLabelColor;
        if (isSeasonHeart) {
            // Gentle pulse from gray to gold using sine wave
            float time = (float)ImGui::GetTime();
            float pulse = (std::sin(time * 2.0f) + 1.0f) * 0.5f;  // 0 to 1
            int r = (int)(150 + (255 - 150) * pulse);
            int g = (int)(150 + (215 - 150) * pulse);
            int b = (int)(150 + (0 - 150) * pulse);
            if (b < 0) b = 0;
            typeLabelColor = IM_COL32(r, g, b, 255);
        } else {
            typeLabelColor = IM_COL32(150, 150, 150, 200);
        }
        drawList->AddText(labelPos, typeLabelColor, typeLabel.c_str());
        
        // Draw cost, offset by type letter width
        // White for candles, golden for season_candle
        ImVec2 typeSize = ImGui::CalcTextSize(typeLabel.c_str());
        std::string costStr = " " + std::to_string(node.cost);
        ImVec2 costPos(labelPos.x + typeSize.x, labelPos.y);
        ImU32 costColor = (node.costType == "season_candle") 
            ? IM_COL32(255, 215, 0, 255)   // Golden for season candles
            : IM_COL32(255, 255, 255, 255); // White for regular candles
        drawList->AddText(costPos, costColor, costStr.c_str());
    }
}

void TreeRenderer::drawConnection(ImDrawList* drawList, const SpiritNode& parent, 
                                  const SpiritNode& child, ImVec2 origin, float zoom) {
    // Get offsets for both nodes
    ImVec2 parentOffset = getNodeOffset(parent.id);
    ImVec2 childOffset = getNodeOffset(child.id);
    
    // Convert positions (with offsets applied)
    ImVec2 parentPos, childPos;
    parentPos.x = origin.x + (parent.x + parentOffset.x) * zoom;
    parentPos.y = origin.y - (parent.y + parentOffset.y) * zoom;
    childPos.x = origin.x + (child.x + childOffset.x) * zoom;
    childPos.y = origin.y - (child.y + childOffset.y) * zoom;
    
    // Also compute the original positions (without offset) for elastic anchor points
    ImVec2 parentOriginal, childOriginal;
    parentOriginal.x = origin.x + parent.x * zoom;
    parentOriginal.y = origin.y - parent.y * zoom;
    childOriginal.x = origin.x + child.x * zoom;
    childOriginal.y = origin.y - child.y * zoom;
    
    float radius = NODE_RADIUS * zoom;
    
    // Calculate connection points on node edges (using current positions)
    float dx = childPos.x - parentPos.x;
    float dy = childPos.y - parentPos.y;
    float dist = sqrtf(dx * dx + dy * dy);
    
    if (dist < 0.001f) return;
    
    float nx = dx / dist;
    float ny = dy / dist;
    
    ImVec2 start(parentPos.x + nx * radius, parentPos.y + ny * radius);
    ImVec2 end(childPos.x - nx * radius, childPos.y - ny * radius);
    
    // Calculate the "tension" based on how far nodes are from their original positions
    float parentOffsetDist = sqrtf(parentOffset.x * parentOffset.x + parentOffset.y * parentOffset.y);
    float childOffsetDist = sqrtf(childOffset.x * childOffset.x + childOffset.y * childOffset.y);
    float maxOffset = std::max(parentOffsetDist, childOffsetDist);
    
    // Draw curved connection using bezier with elastic appearance
    // Curve only at the start (parent), straight at the end (child)
    ImVec2 ctrl1, ctrl2;
    float baseCurveStrength = 0.5f;  // Stronger curve at parent since child end is straight
    
    // When nodes are offset, the control points pull toward the original line path
    // This creates an "elastic" look where the line wants to return to its original shape
    float elasticity = 0.5f;  // How much the control points are attracted to original path
    
    // Original control points (without offset)
    float origDx = childOriginal.x - parentOriginal.x;
    float origDy = childOriginal.y - parentOriginal.y;
    
    ImVec2 origCtrl1;
    origCtrl1.x = parentOriginal.x;
    origCtrl1.y = parentOriginal.y + origDy * baseCurveStrength;
    
    // Current control point at parent (always curves downward at the start)
    ctrl1.x = start.x;
    // Always offset downward regardless of direction
    float downwardCurve = 40.0f * zoom; // You can tune this value for more/less curve
    ctrl1.y = start.y + downwardCurve;
    
    // Control point at child - very slight downward curve at the end
    float endCurveStrength = 0.05f; // keep it subtle
    // Small fixed downward offset to make the end curve downward very slightly
    float downwardEndOffset = 4.0f * zoom;
    ctrl2.x = end.x;
    ctrl2.y = end.y + downwardEndOffset;
    
    // Blend ctrl1 toward original control point based on tension (creates elastic look)
    float tensionFactor = std::min(1.0f, maxOffset / 100.0f);  // Normalize tension
    float blend = tensionFactor * elasticity;
    
    ctrl1.x = ctrl1.x * (1.0f - blend) + origCtrl1.x * blend;
    ctrl1.y = ctrl1.y * (1.0f - blend) + origCtrl1.y * blend;
    
    // Color changes based on stretch - more orange when stretched
    int r = (int)(120 + 80 * tensionFactor);
    int g = (int)(140 - 40 * tensionFactor);
    int b = (int)(160 - 100 * tensionFactor);
    ImU32 lineColor = IM_COL32(r, g, b, 200);
    
    // Thickness can increase slightly when stretched
    float thickness = CONNECTION_THICKNESS * zoom * (1.0f + 0.5f * tensionFactor);
    
    drawList->AddBezierCubic(start, ctrl1, ctrl2, end, lineColor, thickness);
    
    // Draw arrowhead pointing toward the child node
    float arrowSize = 8.0f * zoom;
    
    // Use the straight-line direction from parent to child for consistent arrow orientation
    // This looks cleaner than using the bezier tangent which can be at odd angles
    float arrowDirX = nx;  // Already normalized direction from parent to child
    float arrowDirY = ny;
    
    // Perpendicular vector for arrow wings
    float perpX = -arrowDirY;
    float perpY = arrowDirX;
    
    // Arrow tip is at 'end', wings are behind
    ImVec2 arrowTip = end;
    ImVec2 arrowLeft(end.x - arrowDirX * arrowSize + perpX * arrowSize * 0.5f,
                     end.y - arrowDirY * arrowSize + perpY * arrowSize * 0.5f);
    ImVec2 arrowRight(end.x - arrowDirX * arrowSize - perpX * arrowSize * 0.5f,
                      end.y - arrowDirY * arrowSize - perpY * arrowSize * 0.5f);
    
    drawList->AddTriangleFilled(arrowTip, arrowLeft, arrowRight, lineColor);
}

ImU32 TreeRenderer::getNodeColor(const SpiritNode& node) const {
    // If a render-time per-type color map is provided, prefer it
    if (m_currentRenderTypeColors) {
        auto it = m_currentRenderTypeColors->find(node.type);
        if (it != m_currentRenderTypeColors->end()) {
            const auto &c = it->second;
            int r = (int)(std::clamp(c[0], 0.0f, 1.0f) * 255.0f);
            int g = (int)(std::clamp(c[1], 0.0f, 1.0f) * 255.0f);
            int b = (int)(std::clamp(c[2], 0.0f, 1.0f) * 255.0f);
            int a = (int)(std::clamp(c[3], 0.0f, 1.0f) * 255.0f);
            return IM_COL32(r, g, b, a);
        }
    }

    if (node.type == "outfit") {
        return IM_COL32(100, 140, 200, 255);  // Blue
    } else if (node.type == "spirit_upgrade") {
        return IM_COL32(180, 120, 200, 255);  // Purple
    } else if (node.type == "music") {
        return IM_COL32(200, 160, 100, 255);  // Gold
    } else if (node.type == "lootbox") {
        return IM_COL32(200, 100, 100, 255);  // Red
    }
    return IM_COL32(120, 120, 120, 255);  // Gray default
}

ImU32 TreeRenderer::getNodeBorderColor(const SpiritNode& node) const {
    // If a render-time per-type color map is provided, prefer a darker border variant
    if (m_currentRenderTypeColors) {
        auto it = m_currentRenderTypeColors->find(node.type);
        if (it != m_currentRenderTypeColors->end()) {
            const auto &c = it->second;
            int r = (int)(std::clamp(c[0] * 0.85f, 0.0f, 1.0f) * 255.0f);
            int g = (int)(std::clamp(c[1] * 0.85f, 0.0f, 1.0f) * 255.0f);
            int b = (int)(std::clamp(c[2] * 0.85f, 0.0f, 1.0f) * 255.0f);
            int a = (int)(std::clamp(c[3], 0.0f, 1.0f) * 255.0f);
            return IM_COL32(r, g, b, a);
        }
    }

    if (node.dep == 0) {
        // Root node - special border
        return IM_COL32(255, 220, 100, 255);  // Golden
    }
    if (node.isAdventurePass) {
        return IM_COL32(255, 200, 50, 255);  // Gold border for AP items
    }
    return IM_COL32(200, 200, 200, 180);
}

void TreeRenderer::resetView() {
    m_zoom = 1.0f;
    m_pan = {0.0f, 0.0f};
    m_selectedNodeId = 0;
}

uint64_t TreeRenderer::getNodeAtPosition(const SpiritTree* tree, ImVec2 mousePos, ImVec2 origin, float zoom) {
    if (!tree) return 0;
    
    float radius = NODE_RADIUS * zoom;
    
    for (const auto& node : tree->nodes) {
        // Get node offset
        ImVec2 offset = getNodeOffset(node.id);
        
        // Convert node position to screen position (with offset)
        ImVec2 screenPos;
        screenPos.x = origin.x + (node.x + offset.x) * zoom;
        screenPos.y = origin.y - (node.y + offset.y) * zoom;
        
        // Check if mouse is within the node circle
        float dx = mousePos.x - screenPos.x;
        float dy = mousePos.y - screenPos.y;
        float distSq = dx * dx + dy * dy;
        
        if (distSq <= radius * radius) {
            return node.id;
        }
    }
    
    return 0;
}

} // namespace Watercan
