#include "archipelago/ProgressionRules.hpp"

#include <algorithm>
#include <numeric>

#include "archipelago/LevelIdentity.hpp"

namespace Archipelago {

    namespace {
        constexpr int locationsPerNode = 6;
        constexpr int64_t categoryLocationBase = 1000;

        void UnlockNode(SlotData const& slotData, uint32_t node, Progression& progression) {
            auto entry = slotData.nodeToIdent.find(node);
            if (entry == slotData.nodeToIdent.end())
                return;
            if (std::find(progression.unlockedIdents.begin(), progression.unlockedIdents.end(), entry->second) == progression.unlockedIdents.end())
                progression.unlockedIdents.push_back(entry->second);
        }

        int CategoryOffset(SlotData const& slotData, int categoryIndex) {
            return std::accumulate(slotData.mapTypeCounts.begin(), slotData.mapTypeCounts.begin() + categoryIndex, 0);
        }
    }

    Progression ComputeProgression(SlotData const& slotData, std::vector<std::string> const& receivedItemNames) {
        Progression progression;

        if (slotData.gameMode != GameMode::Categories) {
            uint32_t songUnlocksReceived = 0;
            for (auto const& itemName : receivedItemNames) {
                if (itemName == victoryItem)
                    progression.victoryReceived = true;
                if (itemName != progressiveSongUnlockItem)
                    continue;
                songUnlocksReceived++;
                if (songUnlocksReceived <= slotData.nodeToIdent.size())
                    UnlockNode(slotData, songUnlocksReceived, progression);
            }
            return progression;
        }

        for (int startNode : slotData.startNodes)
            UnlockNode(slotData, static_cast<uint32_t>(startNode), progression);

        int categoryUnlocksReceived[categoryCount] = {};
        for (auto const& itemName : receivedItemNames) {
            if (itemName == victoryItem) {
                progression.victoryReceived = true;
                continue;
            }
            auto category = std::find(std::begin(categoryUnlockItems), std::end(categoryUnlockItems), itemName);
            if (category == std::end(categoryUnlockItems))
                continue;
            int categoryIndex = static_cast<int>(category - std::begin(categoryUnlockItems));
            int unlockCount = ++categoryUnlocksReceived[categoryIndex];
            if (unlockCount <= slotData.mapTypeCounts[categoryIndex])
                UnlockNode(slotData, static_cast<uint32_t>(unlockCount + CategoryOffset(slotData, categoryIndex)), progression);
        }
        return progression;
    }

    bool IsMapVisible(Progression const& progression, std::string_view mapIdentifier) {
        std::string prefix = std::string(mapIdentifier) + "_";
        return std::any_of(progression.unlockedIdents.begin(), progression.unlockedIdents.end(), [&](std::string const& unlocked) {
            return StartsWithIgnoreCase(unlocked, prefix);
        });
    }

    bool IsIdentUnlocked(Progression const& progression, std::string_view ident) {
        return std::any_of(progression.unlockedIdents.begin(), progression.unlockedIdents.end(), [&](std::string const& unlocked) {
            return StartsWithIgnoreCase(unlocked, ident);
        });
    }

    std::optional<uint32_t> FindNodeForIdent(SlotData const& slotData, std::string_view ident) {
        std::optional<uint32_t> prefixMatch;
        for (auto const& [candidateIdent, node] : slotData.identToNode) {
            if (EqualsIgnoreCase(candidateIdent, ident))
                return node;
            if (!prefixMatch && StartsWithIgnoreCase(candidateIdent, ident))
                prefixMatch = node;
        }
        return prefixMatch;
    }

    int GradeIndexFromRankName(std::string_view rankName) {
        constexpr std::string_view grades[] = {"C", "B", "A", "S", "SS"};
        if (rankName == "SSS")
            return 4;
        for (int i = 0; i < static_cast<int>(std::size(grades)); i++) {
            if (rankName == grades[i])
                return i;
        }
        return -1;
    }

    std::vector<int64_t> LocationsForClear(SlotData const& slotData, uint32_t node, int gradeIndex) {
        std::vector<int64_t> locations;

        switch (slotData.gameMode) {
            case GameMode::PresetPass:
                locations.push_back(node);
                break;
            case GameMode::PresetAccuracy:
                for (int grade = 0; grade <= gradeIndex; grade++)
                    locations.push_back(static_cast<int64_t>(node) * locationsPerNode + grade + 1);
                break;
            case GameMode::Categories: {
                int cumulativeCount = 0;
                for (int categoryIndex = 0; categoryIndex < categoryCount; categoryIndex++) {
                    int categorySize = slotData.mapTypeCounts[categoryIndex];
                    if (static_cast<int>(node) < cumulativeCount + categorySize) {
                        int64_t baseId = categoryLocationBase + categoryIndex * categoryLocationBase;
                        int64_t localIndex = static_cast<int64_t>(node) - cumulativeCount;
                        for (int grade = 0; grade <= gradeIndex; grade++)
                            locations.push_back(baseId + localIndex * locationsPerNode + grade + 1);
                        break;
                    }
                    cumulativeCount += categorySize;
                }
                break;
            }
        }
        return locations;
    }
}
