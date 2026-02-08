#include "spirit_tree.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <queue>

namespace Watercan {

using json = nlohmann::json;

bool SpiritTreeManager::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    try {
        json data = json::parse(file);
        // Delegate to common loader
        // Clear existing data
        m_trees.clear();
        m_spiritNames.clear();
        m_guideNames.clear();
        m_allSpiritNamesOrdered.clear();

        // Parse all nodes and group by spirit, tracking first occurrence order
        std::unordered_map<std::string, std::vector<SpiritNode>> spiritNodes;
        std::vector<std::string> spiritOrder;  // Track order of first appearance

        for (const auto& item : data) {
            SpiritNode node;
            node.id = item.value("id", 0ULL);
            node.dep = item.value("dep", 0ULL);
            node.name = item.value("nm", "");
            node.originalName = node.name;
            node.spirit = item.value("spirit", "");
            node.type = item.value("typ", "");
            node.costType = item.value("ctyp", "");
            node.cost = item.value("cst", 0);
            node.isAdventurePass = item.value("ap", false);

            if (!node.spirit.empty()) {
                // Track first appearance order
                if (spiritNodes.find(node.spirit) == spiritNodes.end()) {
                    spiritOrder.push_back(node.spirit);
                }
                spiritNodes[node.spirit].push_back(node);
            }
        }

        // Build trees for each spirit in file order and categorize
        for (const auto& spiritName : spiritOrder) {
            auto& nodes = spiritNodes[spiritName];
            SpiritTree tree;
            tree.spiritName = spiritName;
            tree.nodes = std::move(nodes);

            buildTree(tree);
            computeLayout(tree);

            m_allSpiritNamesOrdered.push_back(spiritName);
            m_trees[spiritName] = std::move(tree);
        }

        // Categorize into spirits and guides (maintaining file order)
        for (const auto& name : m_allSpiritNamesOrdered) {
            if (checkIfGuide(m_trees[name])) {
                m_guideNames.push_back(name);
            } else {
                m_spiritNames.push_back(name);
            }
        }

        m_loadedFile = filepath;

        // Cache original node IDs per spirit for fast needsRestore checks
        m_originalNodeIds.clear();
        m_cachedState.clear();
        for (const auto& kv : m_trees) {
            auto& ids = m_originalNodeIds[kv.first];
            for (const auto& n : kv.second.nodes) ids.insert(n.id);
        }

        return true;

    } catch (const std::exception& e) {
        return false;
    }
}

bool SpiritTreeManager::getNameFromLoadedFile(const std::string& spiritName, uint64_t nodeId, std::string* outName) const {
    if (m_loadedFile.empty()) return false;
    try {
        std::ifstream f(m_loadedFile);
        if (!f.is_open()) return false;
        json data = json::parse(f);
        for (const auto &item : data) {
            uint64_t id = item.value("id", 0ULL);
            std::string spirit = item.value("spirit", std::string());
            if (id == nodeId && spirit == spiritName) {
                if (outName) *outName = item.value("nm", std::string());
                return true;
            }
        }
    } catch (...) {}
    return false;
}

bool SpiritTreeManager::loadFromString(const std::string& jsonContents) {
    try {
        json data = json::parse(jsonContents);

        // Clear existing data
        m_trees.clear();
        m_spiritNames.clear();
        m_guideNames.clear();
        m_allSpiritNamesOrdered.clear();

        // Parse all nodes and group by spirit, tracking first occurrence order
        std::unordered_map<std::string, std::vector<SpiritNode>> spiritNodes;
        std::vector<std::string> spiritOrder;  // Track order of first appearance

        for (const auto& item : data) {
            SpiritNode node;
            node.id = item.value("id", 0ULL);
            node.dep = item.value("dep", 0ULL);
            node.name = item.value("nm", "");
            node.originalName = node.name;
            node.spirit = item.value("spirit", "");
            node.type = item.value("typ", "");
            node.costType = item.value("ctyp", "");
            node.cost = item.value("cst", 0);
            node.isAdventurePass = item.value("ap", false);

            if (!node.spirit.empty()) {
                // Track first appearance order
                if (spiritNodes.find(node.spirit) == spiritNodes.end()) {
                    spiritOrder.push_back(node.spirit);
                }
                spiritNodes[node.spirit].push_back(node);
            }
        }

        // Build trees for each spirit in file order and categorize
        for (const auto& spiritName : spiritOrder) {
            auto& nodes = spiritNodes[spiritName];
            SpiritTree tree;
            tree.spiritName = spiritName;
            tree.nodes = std::move(nodes);

            buildTree(tree);
            computeLayout(tree);

            m_allSpiritNamesOrdered.push_back(spiritName);
            m_trees[spiritName] = std::move(tree);
        }

        // Categorize into spirits and guides (maintaining file order)
        for (const auto& name : m_allSpiritNamesOrdered) {
            if (checkIfGuide(m_trees[name])) {
                m_guideNames.push_back(name);
            } else {
                m_spiritNames.push_back(name);
            }
        }

        // When loading from string this is not a file on disk
        m_loadedFile.clear();

        // Cache original node IDs per spirit
        m_originalNodeIds.clear();
        m_cachedState.clear();
        for (const auto& kv : m_trees) {
            auto& ids = m_originalNodeIds[kv.first];
            for (const auto& n : kv.second.nodes) ids.insert(n.id);
        }

        return true;

    } catch (const std::exception& e) {
        return false;
    }
}

bool SpiritTreeManager::addSpirit(const std::string& spiritName, const std::string& beforeSpirit) {
    if (spiritName.empty()) return false;
    if (m_trees.find(spiritName) != m_trees.end()) return false; // already exists

    SpiritTree tree;
    tree.spiritName = spiritName;
    // empty nodes
    m_trees[spiritName] = std::move(tree);

    // Insert into all-names ordered list
    if (!beforeSpirit.empty()) {
        auto it = std::find(m_allSpiritNamesOrdered.begin(), m_allSpiritNamesOrdered.end(), beforeSpirit);
        if (it != m_allSpiritNamesOrdered.end()) {
            m_allSpiritNamesOrdered.insert(it, spiritName);
        } else {
            m_allSpiritNamesOrdered.push_back(spiritName);
        }
    } else {
        // Default: insert new spirits at the front (top of list)
        m_allSpiritNamesOrdered.insert(m_allSpiritNamesOrdered.begin(), spiritName);
    }

    // Categorize into spirit or guide lists; insert at front so new spirits appear at top
    if (checkIfGuide(m_trees[spiritName])) {
        m_guideNames.insert(m_guideNames.begin(), spiritName);
    } else {
        m_spiritNames.insert(m_spiritNames.begin(), spiritName);
    }
    return true;
}

bool SpiritTreeManager::deleteSpirit(const std::string& spiritName) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    m_trees.erase(it);

    // Remove from lists
    auto itAll = std::find(m_allSpiritNamesOrdered.begin(), m_allSpiritNamesOrdered.end(), spiritName);
    if (itAll != m_allSpiritNamesOrdered.end()) m_allSpiritNamesOrdered.erase(itAll);
    auto itSp = std::find(m_spiritNames.begin(), m_spiritNames.end(), spiritName);
    if (itSp != m_spiritNames.end()) m_spiritNames.erase(itSp);
    auto itGu = std::find(m_guideNames.begin(), m_guideNames.end(), spiritName);
    if (itGu != m_guideNames.end()) m_guideNames.erase(itGu);
    return true;
}

bool SpiritTreeManager::saveToFile(const std::string& filepath) const {
    try {
        // Build a JSON array of all nodes from all trees
        // Maintaining the same structure and order as the original file
        json output = json::array();
        
        // Collect all nodes from all trees in original file order
        for (const auto& spiritName : m_allSpiritNamesOrdered) {
            auto it = m_trees.find(spiritName);
            if (it == m_trees.end()) continue;
            const auto& tree = it->second;
            
            for (const auto& node : tree.nodes) {
                json nodeJson;
                // Keys in alphabetical order to match original file format
                nodeJson["ap"] = node.isAdventurePass;
                nodeJson["cst"] = node.cost;
                nodeJson["ctyp"] = node.costType;
                nodeJson["dep"] = node.dep;
                nodeJson["id"] = node.id;
                nodeJson["nm"] = node.name;
                nodeJson["spirit"] = node.spirit;
                nodeJson["typ"] = node.type;
                output.push_back(nodeJson);
            }
        }
        
        // Write to file with pretty formatting (3-space indent like original)
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return false;
        }
        
        file << output.dump(3);
        file.close();
        
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

void SpiritTreeManager::buildTree(SpiritTree& tree) {
    // Create lookup map
    std::unordered_map<uint64_t, size_t> idToIndex;
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        idToIndex[tree.nodes[i].id] = i;
    }
    
    // Build parent-child relationships
    for (auto& node : tree.nodes) {
        node.children.clear();
    }
    
    for (auto& node : tree.nodes) {
        if (node.dep == 0) {
            tree.rootNodeId = node.id;
        } else {
            auto it = idToIndex.find(node.dep);
            if (it != idToIndex.end()) {
                tree.nodes[it->second].children.push_back(node.id);
            }
        }
    }
    

}

void SpiritTreeManager::computeLayout(SpiritTree& tree) {
    // Find root node
    SpiritNode* root = nullptr;
    for (auto& node : tree.nodes) {
        if (node.dep == 0) {
            root = &node;
            break;
        }
    }
    
    if (!root) return;
    
    // Layout from root going upward
    // Root is at bottom (y=0), children go up (negative y for "north")
    layoutSubtree(tree, *root, 0.0f, 0.0f, 0);
    
    // Calculate bounds
    tree.minX = tree.maxX = 0.0f;
    tree.minY = tree.maxY = 0.0f;
    
    for (const auto& node : tree.nodes) {
        tree.minX = std::min(tree.minX, node.x);
        tree.maxX = std::max(tree.maxX, node.x);
        tree.minY = std::min(tree.minY, node.y);
        tree.maxY = std::max(tree.maxY, node.y);
    }
    
    tree.width = tree.maxX - tree.minX;
    tree.height = tree.maxY - tree.minY;
}

void SpiritTreeManager::layoutSubtree(SpiritTree& tree, SpiritNode& node, float x, float y, int depth) {
    node.x = x;
    node.y = y;
    
    // Create lookup map
    std::unordered_map<uint64_t, SpiritNode*> idToNode;
    for (auto& n : tree.nodes) {
        idToNode[n.id] = &n;
    }
    
    // Layout children
    // Parent is at south (bottom), children go north (up, so negative y in screen coords)
    // But we'll use positive y going up for logical coords
    const float nodeSpacingY = 100.0f;  // Vertical spacing
    const float nodeSpacingX = 120.0f;  // Horizontal spacing for branches
    
    size_t childCount = node.children.size();

    // Compute positions for any number of children. For 1-3 children preserve the
    // original aesthetic (NW,N,NE). For >3 distribute evenly centered over parent.
    const float diagonalYOffset = -25.0f;  // NW/NE nodes are slightly lower

    for (size_t i = 0; i < childCount; ++i) {
        auto it = idToNode.find(node.children[i]);
        if (it == idToNode.end()) continue;

        SpiritNode* child = it->second;
        float childX = x;
        float childY = y + nodeSpacingY;  // Move up (north)

        if (childCount == 1) {
            childX = x;
        } else if (childCount == 2) {
            // Keep original two-child look: left + center
            if (i == 0) {
                childX = x - nodeSpacingX;  // North-West
                childY += diagonalYOffset;  // Slightly lower
            } else {
                childX = x;  // North (directly above)
            }
        } else if (childCount == 3) {
            // Original three-child layout
            if (i == 0) {
                childX = x - nodeSpacingX;  // North-West
                childY += diagonalYOffset;  // Slightly lower
            } else if (i == 1) {
                childX = x;  // North (center)
            } else {
                childX = x + nodeSpacingX;  // North-East
                childY += diagonalYOffset;  // Slightly lower
            }
        } else {
            // Distribute children evenly centered above the parent for >3 children
            float startX = x - nodeSpacingX * (float(childCount - 1) * 0.5f);
            childX = startX + (float)i * nodeSpacingX;
            // Slightly lower for non-center children to preserve visual grouping
            float centerIndex = (childCount - 1) * 0.5f;
            if (std::fabs((float)i - centerIndex) > 0.01f) childY += diagonalYOffset;
        }

        layoutSubtree(tree, *child, childX, childY, depth + 1);
    }
}

SpiritTree* SpiritTreeManager::getTree(const std::string& spiritName) {
    auto it = m_trees.find(spiritName);
    return (it != m_trees.end()) ? &it->second : nullptr;
}

const SpiritTree* SpiritTreeManager::getTree(const std::string& spiritName) const {
    auto it = m_trees.find(spiritName);
    return (it != m_trees.end()) ? &it->second : nullptr;
}

size_t SpiritTreeManager::getNodeCount(const std::string& spiritName) const {
    auto it = m_trees.find(spiritName);
    return (it != m_trees.end()) ? it->second.nodes.size() : 0;
}

bool SpiritTreeManager::isGuide(const std::string& spiritName) const {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    return checkIfGuide(it->second);
}

bool SpiritTreeManager::isTravellingSpirit(const std::string& spiritName) const {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    const SpiritTree& tree = it->second;

    // Do not apply identification rules to guides
    if (checkIfGuide(tree)) return false;

    // Rule 1: If any node has Adventure Pass flag, it's NOT travelling
    for (const auto& node : tree.nodes) {
        if (node.isAdventurePass) return false;
    }

    // Rule X: MUST contain an emote_upgrade node somewhere (check name contains "emote_upgrade", case-insensitive)
    bool hasEmoteUpgrade = false;
    for (const auto& node : tree.nodes) {
        std::string nm = node.name;
        std::transform(nm.begin(), nm.end(), nm.begin(), [](unsigned char c){ return std::tolower(c); });
        if (nm.find("emote_upgrade") != std::string::npos) {
            hasEmoteUpgrade = true;
            break;
        }
    }
    if (!hasEmoteUpgrade) return false;

    // Rule 2: If there exists a non-root node with typ != "seasonal heart", it's a travelling spirit
    for (const auto& node : tree.nodes) {
        if (node.dep == 0) continue; // skip root/top nodes
        std::string typ = node.type;
        std::transform(typ.begin(), typ.end(), typ.begin(), [](unsigned char c){ return std::tolower(c); });
        if (typ != "seasonal heart") {
            return true;
        }
    }

    return false;
}

bool SpiritTreeManager::checkIfGuide(const SpiritTree& tree) const {
    // Check if spirit name starts with "quest" or "tgc_"
    if (tree.spiritName.size() >= 5 && tree.spiritName.substr(0, 5) == "quest") {
        return true;
    }
    if (tree.spiritName.size() >= 4 && tree.spiritName.substr(0, 4) == "tgc_") {
        return true;
    }
    return false;
}

bool SpiritTreeManager::updateNodeId(const std::string& spiritName, uint64_t oldId) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    
    SpiritTree& tree = it->second;
    for (auto& node : tree.nodes) {
        if (node.id == oldId) {
            uint32_t newId = fnv1a32(node.name);
            return changeNodeId(spiritName, oldId, newId);
        }
    }
    return false;
}

bool SpiritTreeManager::changeNodeId(const std::string& spiritName, uint64_t oldId, uint64_t newId) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;

    SpiritTree& tree = it->second;
    SpiritNode* target = nullptr;
    for (auto& node : tree.nodes) {
        if (node.id == oldId) {
            target = &node;
            break;
        }
    }
    if (!target) return false;

    // Update parent references and children references across the tree
    for (auto& otherNode : tree.nodes) {
        if (otherNode.dep == oldId) {
            otherNode.dep = newId;
        }
        for (auto& childId : otherNode.children) {
            if (childId == oldId) {
                childId = newId;
            }
        }
    }

    // Update root if needed
    if (tree.rootNodeId == oldId) tree.rootNodeId = newId;

    // Finally set the node's id
    target->id = newId;
    return true;
}

SpiritNode* SpiritTreeManager::getNode(const std::string& spiritName, uint64_t nodeId) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return nullptr;
    
    for (auto& node : it->second.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

std::string SpiritTreeManager::nodeToJson(const SpiritNode& node) {
    // Format JSON in the same order as the file: ap, cst, ctyp, dep, id, nm, spirit, typ
    std::string json = "{\n";
    json += "   \"ap\" : " + std::string(node.isAdventurePass ? "true" : "false") + ",\n";
    json += "   \"cst\" : " + std::to_string(node.cost) + ",\n";
    json += "   \"ctyp\" : \"" + node.costType + "\",\n";
    json += "   \"dep\" : " + std::to_string(node.dep) + ",\n";
    json += "   \"id\" : " + std::to_string(node.id) + ",\n";
    json += "   \"nm\" : \"" + node.name + "\",\n";
    json += "   \"spirit\" : \"" + node.spirit + "\",\n";
    json += "   \"typ\" : \"" + node.type + "\"\n";
    json += "}";
    return json;
}

bool SpiritTreeManager::updateNodeFromJson(const std::string& spiritName, uint64_t nodeId, const std::string& jsonStr, uint64_t* newNodeId) {
    SpiritNode* node = getNode(spiritName, nodeId);
    if (!node) return false;
    
    try {
        json data = json::parse(jsonStr);
        
        // Update fields if present
        if (data.contains("ap")) node->isAdventurePass = data["ap"].get<bool>();
        if (data.contains("cst")) node->cost = data["cst"].get<int>();
        if (data.contains("ctyp")) node->costType = data["ctyp"].get<std::string>();
        if (data.contains("dep")) node->dep = data["dep"].get<uint64_t>();
        if (data.contains("id")) {
            uint64_t newId = data["id"].get<uint64_t>();
            if (newId != node->id) {
                // Use changeNodeId to update references consistently
                changeNodeId(spiritName, node->id, newId);
            }
        }
        if (data.contains("nm")) {
            std::string newName = data["nm"].get<std::string>();
            // Prevent duplicate names within the same spirit
            if (isNameDuplicate(spiritName, newName, node->id)) {
                // Revert to original name
                node->name = node->originalName;
                return false;
            }
            node->name = newName;
        }
        if (data.contains("spirit")) node->spirit = data["spirit"].get<std::string>();
        if (data.contains("typ")) node->type = data["typ"].get<std::string>();
        
        // Return the new ID if requested
        if (newNodeId) {
            *newNodeId = node->id;
        }
        
        // Rebuild tree relationships after editing
        rebuildTree(spiritName);
        
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

void SpiritTreeManager::rebuildTree(const std::string& spiritName) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return;
    
    SpiritTree& tree = it->second;
    buildTree(tree);
    markDirty(spiritName);
    // Note: We don't recompute layout here to preserve node positions
}

bool SpiritTreeManager::moveNodeBase(const std::string& spiritName, uint64_t nodeId, float dx, float dy) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    SpiritTree& tree = it->second;
    for (auto& node : tree.nodes) {
        if (node.id == nodeId) {
            node.x += dx;
            node.y += dy;
            // Update bounds to include the new position
            tree.minX = std::min(tree.minX, node.x);
            tree.maxX = std::max(tree.maxX, node.x);
            tree.minY = std::min(tree.minY, node.y);
            tree.maxY = std::max(tree.maxY, node.y);
            tree.width = tree.maxX - tree.minX;
            tree.height = tree.maxY - tree.minY;
            markDirty(spiritName);
            return true;
        }
    }
    return false;
}

bool SpiritTreeManager::moveTreeBase(const std::string& spiritName, float dx, float dy) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    SpiritTree& tree = it->second;
    if (dx == 0.0f && dy == 0.0f) return true;

    tree.minX = std::numeric_limits<float>::infinity();
    tree.maxX = -std::numeric_limits<float>::infinity();
    tree.minY = std::numeric_limits<float>::infinity();
    tree.maxY = -std::numeric_limits<float>::infinity();

    for (auto& node : tree.nodes) {
        node.x += dx;
        node.y += dy;
        tree.minX = std::min(tree.minX, node.x);
        tree.maxX = std::max(tree.maxX, node.x);
        tree.minY = std::min(tree.minY, node.y);
        tree.maxY = std::max(tree.maxY, node.y);
    }
    tree.width = tree.maxX - tree.minX;
    tree.height = tree.maxY - tree.minY;
    return true;
}


bool SpiritTreeManager::moveSubtreeBase(const std::string& spiritName, uint64_t subtreeRootId, float dx, float dy,
                                         std::unordered_set<uint64_t>* outMovedIds) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    SpiritTree& tree = it->second;
    if (dx == 0.0f && dy == 0.0f) return true;

    // Build id->node map
    std::unordered_map<uint64_t, SpiritNode*> idToNode;
    for (auto& n : tree.nodes) idToNode[n.id] = &n;

    // Collect subtree via DFS
    std::unordered_set<uint64_t> subtree;
    std::vector<uint64_t> stack;
    stack.push_back(subtreeRootId);
    while (!stack.empty()) {
        uint64_t cur = stack.back(); stack.pop_back();
        if (subtree.count(cur)) continue;
        subtree.insert(cur);
        auto itn = idToNode.find(cur);
        if (itn == idToNode.end()) continue;
        for (uint64_t c : itn->second->children) stack.push_back(c);
    }

    // Move only subtree nodes
    for (uint64_t id : subtree) {
        auto itn = idToNode.find(id);
        if (itn == idToNode.end()) continue;
        itn->second->x += dx;
        itn->second->y += dy;
    }

    // Recompute bounds for the whole tree
    tree.minX = std::numeric_limits<float>::infinity();
    tree.maxX = -std::numeric_limits<float>::infinity();
    tree.minY = std::numeric_limits<float>::infinity();
    tree.maxY = -std::numeric_limits<float>::infinity();
    for (const auto& n : tree.nodes) {
        tree.minX = std::min(tree.minX, n.x);
        tree.maxX = std::max(tree.maxX, n.x);
        tree.minY = std::min(tree.minY, n.y);
        tree.maxY = std::max(tree.maxY, n.y);
    }
    tree.width = tree.maxX - tree.minX;
    tree.height = tree.maxY - tree.minY;

    if (outMovedIds) *outMovedIds = std::move(subtree);
    return true;
}

bool SpiritTreeManager::reshapeTreeAndCollectShifts(const std::string& spiritName,
                                                     std::unordered_map<uint64_t, std::pair<float,float>>* outShifts) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    SpiritTree& tree = it->second;

    std::unordered_map<uint64_t, std::pair<float,float>> mergedShifts;
    // Find root nodes (dep == 0)
    for (const auto& n : tree.nodes) {
        if (n.dep == 0) {
            std::unordered_map<uint64_t, std::pair<float,float>> shifts;
            if (layoutSubtreeAndCollectShifts(spiritName, n.id, &shifts)) {
                for (const auto& kv : shifts) mergedShifts[kv.first] = kv.second;
            }
        }
    }

    if (outShifts) *outShifts = std::move(mergedShifts);
    markDirty(spiritName);
    return true;
}

bool SpiritTreeManager::needsReshape(const std::string& spiritName, float epsilon) {
    // Check cache first
    auto& cs = m_cachedState[spiritName];
    if (!cs.reshapeDirty) return cs.reshapeResult;
    cs.reshapeDirty = false;

    // If the spirit name is empty, nothing to do
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) { cs.reshapeResult = false; return false; }
    const SpiritTree& tree = it->second;

    // If there are snapped nodes recorded for this spirit, we consider reshape necessary
    auto pit = m_perTreeSnaps.find(spiritName);
    if (pit != m_perTreeSnaps.end() && !pit->second.empty()) { cs.reshapeResult = true; return true; }

    // Make a copy and recompute positions using layoutSubtree on each root without mutating the real tree
    SpiritTree tmp = tree;
    // Build id map for tmp and find roots
    std::unordered_map<uint64_t, SpiritNode*> idToNode;
    for (auto& n : tmp.nodes) idToNode[n.id] = &n;
    for (auto& n : tmp.nodes) {
        if (n.dep == 0) {
            SpiritNode* root = idToNode[n.id];
            if (root) layoutSubtree(tmp, *root, root->x, root->y, 0);
        }
    }

    // Compare positions
    std::unordered_map<uint64_t, const SpiritNode*> origMap;
    for (const auto& n : tree.nodes) origMap[n.id] = &n;
    for (const auto& n : tmp.nodes) {
        auto itn = origMap.find(n.id);
        if (itn == origMap.end()) continue;
        const SpiritNode* orig = itn->second;
        float dx = fabsf(orig->x - n.x);
        float dy = fabsf(orig->y - n.y);
        if (dx > epsilon || dy > epsilon) { cs.reshapeResult = true; return true; }
    }
    cs.reshapeResult = false;
    return false;
}

bool SpiritTreeManager::needsRestore(const std::string& spiritName) const {
    // Check cache first
    auto& cs = m_cachedState[spiritName];
    if (!cs.restoreDirty) return cs.restoreResult;
    cs.restoreDirty = false;

    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) { cs.restoreResult = false; return false; }
    const SpiritTree& tree = it->second;

    // Check for any new/custom nodes (added at runtime)
    for (const auto& n : tree.nodes) {
        if (n.isNew) { cs.restoreResult = true; return true; }
    }

    // Compare current node IDs against cached original IDs
    auto oit = m_originalNodeIds.find(spiritName);
    if (oit == m_originalNodeIds.end()) { cs.restoreResult = false; return false; }
    const auto& origIds = oit->second;

    // Quick size check
    if (tree.nodes.size() != origIds.size()) { cs.restoreResult = true; return true; }

    // Check all current nodes exist in originals
    for (const auto& n : tree.nodes) {
        if (origIds.find(n.id) == origIds.end()) { cs.restoreResult = true; return true; }
    }

    cs.restoreResult = false;
    return false;
}

void SpiritTreeManager::markDirty(const std::string& spiritName) {
    auto& cs = m_cachedState[spiritName];
    cs.reshapeDirty = true;
    cs.restoreDirty = true;
}

void SpiritTreeManager::positionLinkedNode(const std::string& spiritName, uint64_t nodeId,
                                             std::unordered_map<uint64_t, std::pair<float,float>>* outShifts) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return;
    
    SpiritTree& tree = it->second;
    
    // Find the node
    SpiritNode* node = nullptr;
    for (auto& n : tree.nodes) {
        if (n.id == nodeId) {
            node = &n;
            break;
        }
    }
    if (!node || node->dep == 0) return;  // No node or no parent
    
    // Find the parent
    SpiritNode* parent = nullptr;
    for (auto& n : tree.nodes) {
        if (n.id == node->dep) {
            parent = &n;
            break;
        }
    }
    if (!parent) return;
    
    // Create lookup map for all nodes
    std::unordered_map<uint64_t, SpiritNode*> idToNode;
    for (auto& n : tree.nodes) {
        idToNode[n.id] = &n;
    }
    
    // Layout constants (same as in layoutSubtree)
    const float nodeSpacingY = 100.0f;
    const float nodeSpacingX = 120.0f;
    const float diagonalYOffset = -25.0f;
    
    size_t childCount = parent->children.size();

    // Reposition ALL children of this parent according to the layout rules
    for (size_t i = 0; i < childCount; ++i) {
        auto childIt = idToNode.find(parent->children[i]);
        if (childIt == idToNode.end()) continue;

        SpiritNode* child = childIt->second;
        float x = parent->x;
        float y = parent->y + nodeSpacingY;

        // Position based on child index (same logic as layoutSubtree)
        if (childCount == 1) {
            x = parent->x;  // Directly above (N)
        } else if (childCount == 2) {
            if (i == 0) {
                x = parent->x - nodeSpacingX;  // NW
                y += diagonalYOffset;
            } else {
                x = parent->x;  // N
            }
        } else if (childCount == 3) {
            if (i == 0) {
                x = parent->x - nodeSpacingX;  // NW
                y += diagonalYOffset;
            } else if (i == 1) {
                x = parent->x;  // N
            } else {
                x = parent->x + nodeSpacingX;  // NE
                y += diagonalYOffset;
            }
        } else {
            // Distribute children evenly centered above the parent for >3 children
            float startX = parent->x - nodeSpacingX * (float(childCount - 1) * 0.5f);
            x = startX + (float)i * nodeSpacingX;
            float centerIndex = (childCount - 1) * 0.5f;
            if (std::fabs((float)i - centerIndex) > 0.01f) y += diagonalYOffset;
        }

        // If caller requested, record the visual shift (oldBase - newBase) so the renderer can
        // apply an immediate offset that will be springed back to zero (producing a smooth motion)
        if (outShifts) {
            float dx = child->x - x; // oldX - newX
            float dy = child->y - y; // oldY - newY
            (*outShifts)[child->id] = std::make_pair(dx, dy);
        }

        child->x = x;
        child->y = y;
    }
}

bool SpiritTreeManager::layoutSubtreeAndCollectShifts(const std::string& spiritName, uint64_t rootNodeId,
                                       std::unordered_map<uint64_t, std::pair<float,float>>* outShifts) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    SpiritTree& tree = it->second;

    // Build id lookup and find root
    std::unordered_map<uint64_t, SpiritNode*> idToNode;
    SpiritNode* root = nullptr;
    for (auto& n : tree.nodes) {
        idToNode[n.id] = &n;
        if (n.id == rootNodeId) root = &n;
    }
    if (!root) return false;

    // Collect subtree nodes
    std::vector<uint64_t> stack;
    std::unordered_set<uint64_t> subtreeSet;
    stack.push_back(root->id);
    while (!stack.empty()) {
        uint64_t cur = stack.back(); stack.pop_back();
        if (subtreeSet.count(cur)) continue;
        subtreeSet.insert(cur);
        auto itn = idToNode.find(cur);
        if (itn == idToNode.end()) continue;
        for (uint64_t c : itn->second->children) stack.push_back(c);
    }

    // Save old positions for subtree nodes
    std::unordered_map<uint64_t, std::pair<float,float>> oldPos;
    for (uint64_t id : subtreeSet) {
        auto itn = idToNode.find(id);
        if (itn != idToNode.end()) oldPos[id] = std::make_pair(itn->second->x, itn->second->y);
    }

    // Re-layout the subtree rooted at 'root' using current root->x, root->y
    layoutSubtree(tree, *root, root->x, root->y, 0);

    // Recompute bounds for the whole tree
    tree.minX = tree.maxX = 0.0f;
    tree.minY = tree.maxY = 0.0f;
    for (const auto& n : tree.nodes) {
        tree.minX = std::min(tree.minX, n.x);
        tree.maxX = std::max(tree.maxX, n.x);
        tree.minY = std::min(tree.minY, n.y);
        tree.maxY = std::max(tree.maxY, n.y);
    }
    tree.width = tree.maxX - tree.minX;
    tree.height = tree.maxY - tree.minY;

    // Compute shifts for subtree nodes (oldBase - newBase) excluding the root node
    if (outShifts) {
        for (auto &kv : oldPos) {
            uint64_t id = kv.first;
            if (id == rootNodeId) continue; // skip root (dragged node kept directly under cursor)
            auto itn = idToNode.find(id);
            if (itn == idToNode.end()) continue;
            float oldX = kv.second.first;
            float oldY = kv.second.second;
            float newX = itn->second->x;
            float newY = itn->second->y;
            (*outShifts)[id] = std::make_pair(oldX - newX, oldY - newY);
        }
    }

    markDirty(spiritName);
    return true;
}

uint64_t SpiritTreeManager::createNode(const std::string& spiritName, float x, float y) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return 0;
    
    SpiritTree& tree = it->second;
    
    // Generate a unique name for the new node
    std::string baseName = "new_node";
    std::string nodeName = baseName;
    int counter = 1;
    
    // Make sure the name is unique
    bool nameExists = true;
    while (nameExists) {
        nameExists = false;
        for (const auto& node : tree.nodes) {
            if (node.name == nodeName) {
                nameExists = true;
                nodeName = baseName + "_" + std::to_string(counter++);
                break;
            }
        }
    }
    
    // Create the new node
    SpiritNode newNode;
    newNode.name = nodeName;
    newNode.originalName = nodeName;
    newNode.id = fnv1a32(nodeName);  // Generate ID from name
    newNode.dep = 0;  // No parent (free-floating)
    newNode.spirit = spiritName;
    newNode.type = "outfit";  // Default type
    newNode.costType = "candle";  // Default cost type
    newNode.cost = 1;
    newNode.isAdventurePass = false;
    newNode.x = x;
    newNode.y = y;
    newNode.isNew = true;

    // Add to tree
    tree.nodes.push_back(newNode);
    
    // Rebuild tree relationships
    buildTree(tree);
    markDirty(spiritName);
    
    return newNode.id;
}

bool SpiritTreeManager::deleteNode(const std::string& spiritName, uint64_t nodeId) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    
    SpiritTree& tree = it->second;
    
    // Find and remove the node
    auto nodeIt = std::find_if(tree.nodes.begin(), tree.nodes.end(),
        [nodeId](const SpiritNode& n) { return n.id == nodeId; });
    
    if (nodeIt == tree.nodes.end()) return false;
    
    // Remove the node
    tree.nodes.erase(nodeIt);
    
    // Update any nodes that had this as their parent (orphan them)
    for (auto& node : tree.nodes) {
        if (node.dep == nodeId) {
            node.dep = 0;  // Make them free-floating
        }
    }
    
    // Rebuild tree relationships
    buildTree(tree);
    markDirty(spiritName);
    
    return true;
}

void SpiritTreeManager::recordSnap(const std::string& spiritName, uint64_t childId, uint64_t oldParentId) {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return;
    // Keep map per-manager; record mapping child -> {oldParent, oldIndex} (only if child exists in this spirit)
    SpiritTree& tree = it->second;
    bool childExists = false;
    for (const auto& n : tree.nodes) if (n.id == childId) { childExists = true; break; }
    if (!childExists) return;

    // Find the old parent's child index if possible
    size_t oldIndex = 0;
    bool foundIndex = false;
    for (const auto& n : tree.nodes) {
        if (n.id == oldParentId) {
            const SpiritNode* parent = &n;
            for (size_t i = 0; i < parent->children.size(); ++i) {
                if (parent->children[i] == childId) { oldIndex = i; foundIndex = true; break; }
            }
            break;
        }
    }

    // If we couldn't find old index (caller may have already removed child), default to append
    if (!foundIndex) {
        oldIndex = 0; // will clamp later during restore
    }

    m_snappedParents[childId] = SnapInfo{oldParentId, oldIndex};

    // Also persist the mapping inside the per-tree structure in case the app needs to query it
    auto &vec = m_perTreeSnaps[spiritName];
    if (std::find(vec.begin(), vec.end(), childId) == vec.end()) vec.push_back(childId);
    markDirty(spiritName);
}

void SpiritTreeManager::clearSnap(const std::string& spiritName, uint64_t childId) {
    // Remove global mapping if present
    auto it = m_snappedParents.find(childId);
    if (it != m_snappedParents.end()) m_snappedParents.erase(it);

    // Remove from per-tree list
    auto pit = m_perTreeSnaps.find(spiritName);
    if (pit != m_perTreeSnaps.end()) {
        auto &vec = pit->second;
        vec.erase(std::remove(vec.begin(), vec.end(), childId), vec.end());
        if (vec.empty()) m_perTreeSnaps.erase(pit);
    }
    markDirty(spiritName);
}

void SpiritTreeManager::clearAllSnaps(const std::string& spiritName) {
    // Remove per-tree snap list
    auto pit = m_perTreeSnaps.find(spiritName);
    if (pit != m_perTreeSnaps.end()) {
        // Also remove each child from the global map
        for (uint64_t cid : pit->second) {
            m_snappedParents.erase(cid);
        }
        m_perTreeSnaps.erase(pit);
    }
    markDirty(spiritName);
}

bool SpiritTreeManager::reloadSpirit(const std::string& spiritName) {
    if (m_loadedFile.empty()) return false;
    auto treeIt = m_trees.find(spiritName);
    if (treeIt == m_trees.end()) return false;

    try {
        std::ifstream f(m_loadedFile);
        if (!f.is_open()) return false;
        json data = json::parse(f);

        // Collect original nodes for this spirit from the file
        std::vector<SpiritNode> originalNodes;
        for (const auto& item : data) {
            std::string spirit = item.value("spirit", std::string());
            if (spirit != spiritName) continue;
            SpiritNode node;
            node.id = item.value("id", 0ULL);
            node.dep = item.value("dep", 0ULL);
            node.name = item.value("nm", std::string());
            node.originalName = node.name;
            node.spirit = spirit;
            node.type = item.value("typ", std::string());
            node.costType = item.value("ctyp", std::string());
            node.cost = item.value("cst", 0);
            node.isAdventurePass = item.value("ap", false);
            node.isNew = false;
            originalNodes.push_back(node);
        }

        // Replace the tree's nodes entirely and rebuild
        SpiritTree& tree = treeIt->second;
        tree.nodes = std::move(originalNodes);
        buildTree(tree);
        computeLayout(tree);

        // Clear all snap records for this spirit
        clearAllSnaps(spiritName);
        markDirty(spiritName);

        return true;
    } catch (...) {
        return false;
    }
}
std::vector<uint64_t> SpiritTreeManager::restoreSnaps(const std::string& spiritName) {
    std::vector<uint64_t> restored;
    if (spiritName.empty()) return restored;
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return restored;
    SpiritTree& tree = it->second;

    // Build id->node map
    std::unordered_map<uint64_t, SpiritNode*> idToNode;
    for (auto& n : tree.nodes) idToNode[n.id] = &n;

    // Reattach any snapped nodes if their original parent still exists
    std::vector<uint64_t> toErase;
    std::vector<std::pair<uint64_t,size_t>> restoredWithIndex; // (childId, originalIndex)
    for (auto itKv = m_snappedParents.begin(); itKv != m_snappedParents.end(); ) {
        uint64_t childId = itKv->first;
        SnapInfo info = itKv->second;
        uint64_t oldParent = info.parentId;
        auto itChild = idToNode.find(childId);
        auto itParent = idToNode.find(oldParent);
        if (itChild != idToNode.end() && itParent != idToNode.end()) {
            // Reattach to old parent
            itChild->second->dep = oldParent;
            toErase.push_back(childId);
            restored.push_back(childId);
            restoredWithIndex.push_back(std::make_pair(childId, info.index));
            // Remove from map and per-tree lists now, we'll apply original indices after rebuild
            itKv = m_snappedParents.erase(itKv);
            auto &vec = m_perTreeSnaps[spiritName];
            vec.erase(std::remove(vec.begin(), vec.end(), childId), vec.end());
        } else {
            ++itKv;
        }
    }

    // Rebuild tree relationships if we restored any nodes so subsequent layout calls
    // operate on the full tree structure
    if (!toErase.empty()) {
        rebuildTree(spiritName);
        // After rebuild we should ensure each restored node occupies its original index
        const SpiritTree* tptr = getTree(spiritName);
        if (tptr) {
            // Convert tree to mutable reference to adjust parent->children ordering
            SpiritTree& treeRef = const_cast<SpiritTree&>(*tptr);
            for (const auto &pr : restoredWithIndex) {
                uint64_t rid = pr.first;
                size_t idx = pr.second;
                // Find child
                SpiritNode* child = nullptr;
                for (auto &n : treeRef.nodes) {
                    if (n.id == rid) { child = &n; break; }
                }
                if (!child) continue;
                // Find parent by id recorded in child->dep
                SpiritNode* parent = nullptr;
                for (auto &n : treeRef.nodes) {
                    if (n.id == child->dep) { parent = &n; break; }
                }
                if (!parent) continue;
                // Remove any existing entries of child from parent's list
                parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), rid), parent->children.end());
                // Clamp index and insert
                size_t insertIdx = std::min(idx, parent->children.size());
                parent->children.insert(parent->children.begin() + insertIdx, rid);
            }
        }
    }

    // Rebuild tree relationships if we restored any nodes so subsequent layout calls
    // operate on the full tree structure
    if (!toErase.empty()) {
        rebuildTree(spiritName);
        // After rebuild we should ensure each restored node occupies its original index
        // (so reorder and manual child ordering persists across snaps)
        const SpiritTree* tptr = getTree(spiritName);
        if (tptr) {
            for (uint64_t rid : restored) {
                // We didn't keep the original index in the map because we erased entries above.
                // Instead, check if we have any stored information in m_perTreeSnaps? Not directly.
                // Strategy: when recordSnap is called it already stored index in the SnapInfo map; since
                // we removed the map entry above, we need to reconstruct: callers should record SnapInfo
                // in m_perTreeSnaps as needed. For now, best-effort: leave ordering as-is (append) if no index found.
                // (This branch is conservative and will be improved if we keep temp index mapping)
                // TODO: preserve and apply original index for restored children.
            }
        }
    }

    // Additionally, if any snaps remain (children detached but their parent missing),
    // keep the reshape button enabled so the user can Restore once file state is consistent.

    // Also, if snapped nodes remain detached from this spirit, keep them recorded in per-tree list
    // so the UI can know about them; we already maintain m_perTreeSnaps for that purpose.

    return restored;
}

bool SpiritTreeManager::hasSnapsInternal(const std::string& spiritName) const {
    auto it = m_perTreeSnaps.find(spiritName);
    if (it == m_perTreeSnaps.end()) return false;
    return !it->second.empty();
}

bool SpiritTreeManager::isNameDuplicate(const std::string& spiritName, const std::string& name, uint64_t excludeId) const {
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    const SpiritTree& tree = it->second;
    for (const auto& n : tree.nodes) {
        if (n.id == excludeId) continue;
        if (n.name == name) return true;
    }
    return false;
}

std::unordered_set<uint64_t> SpiritTreeManager::getDuplicateNodeIds(const std::string& spiritName) const {
    std::unordered_set<uint64_t> out;
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return out;
    const SpiritTree& tree = it->second;
    // map name -> vector of ids
    std::unordered_map<std::string, std::vector<uint64_t>> nameMap;
    for (const auto& n : tree.nodes) {
        nameMap[n.name].push_back(n.id);
    }
    for (const auto& kv : nameMap) {
        if (kv.second.size() > 1) {
            for (uint64_t id : kv.second) out.insert(id);
        }
    }
    return out;
}

bool SpiritTreeManager::hasSnaps(const std::string& spiritName) const {
    // Check per-tree list for snaps first
    if (hasSnapsInternal(spiritName)) return true;
    // Fallback: check global map for any child ids that belong to this spirit
    auto it = m_trees.find(spiritName);
    if (it == m_trees.end()) return false;
    const SpiritTree& tree = it->second;
    std::unordered_set<uint64_t> ids;
    for (const auto& n : tree.nodes) ids.insert(n.id);
    for (const auto &kv : m_snappedParents) {
        if (ids.count(kv.first)) return true;
    }
    return false;
}


bool SpiritTreeManager::moveNode(const std::string& fromSpirit, const std::string& toSpirit, uint64_t nodeId) {
    if (fromSpirit == toSpirit) return true; // nothing to do
    auto itFrom = m_trees.find(fromSpirit);
    auto itTo = m_trees.find(toSpirit);
    if (itFrom == m_trees.end() || itTo == m_trees.end()) return false;

    SpiritTree& fromTree = itFrom->second;
    SpiritTree& toTree = itTo->second;

    auto nodeIt = std::find_if(fromTree.nodes.begin(), fromTree.nodes.end(),
        [nodeId](const SpiritNode& n) { return n.id == nodeId; });
    if (nodeIt == fromTree.nodes.end()) return false;

    // Move node by copying and erasing
    SpiritNode nodeCopy = *nodeIt;
    nodeCopy.spirit = toSpirit;

    // Erase from source
    fromTree.nodes.erase(nodeIt);

    // Remove parent references in source that pointed to this node
    for (auto& n : fromTree.nodes) {
        if (n.dep == nodeId) n.dep = 0;
    }

    // Add to destination
    toTree.nodes.push_back(nodeCopy);

    // Rebuild both trees
    buildTree(fromTree);
    buildTree(toTree);

    return true;
}



} // namespace Watercan
