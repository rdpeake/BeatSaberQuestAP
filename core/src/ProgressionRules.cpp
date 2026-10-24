#include "archipelago/ProgressionRules.hpp"

#include <algorithm>
#include <numeric>

#include "archipelago/LevelIdentity.hpp"

namespace Archipelago {

    namespace {
        constexpr int64_t locationsPerSong = 6;
        constexpr int64_t categoryLocationBase = 1000;

        int CategoryOffset(SlotData const& slotData, int categoryIndex) {
            return std::accumulate(slotData.mapTypeCounts.begin(), slotData.mapTypeCounts.begin() + categoryIndex, 0);
        }

        std::optional<int> CategoryIndexForItem(std::string_view itemName) {
            if (itemName == legacyAccuracyUnlockItem)
                return 3;
            auto category = std::find(std::begin(categoryUnlockItems), std::end(categoryUnlockItems), itemName);
            if (category == std::end(categoryUnlockItems))
                return std::nullopt;
            return static_cast<int>(category - std::begin(categoryUnlockItems));
        }

        struct SongPosition {
            int64_t baseLocationId = 0;
            int64_t songIndex = 0;
        };

        // where a node's checks live: preset songs start at 0, each randomized category at its own thousand
        std::optional<SongPosition> PositionForNode(SlotData const& slotData, uint32_t node) {
            if (!IsRandomizedMode(slotData.gameMode))
                return SongPosition{0, node};
            int cumulativeCount = 0;
            for (int categoryIndex = 0; categoryIndex < categoryCount; categoryIndex++) {
                int categorySize = slotData.mapTypeCounts[categoryIndex];
                if (static_cast<int>(node) < cumulativeCount + categorySize)
                    return SongPosition{categoryLocationBase * (categoryIndex + 1), static_cast<int64_t>(node) - cumulativeCount};
                cumulativeCount += categorySize;
            }
            return std::nullopt;
        }

        std::optional<int64_t> GoalLocationId(SlotData const& slotData) {
            if (!slotData.goalNode)
                return std::nullopt;
            auto position = PositionForNode(slotData, *slotData.goalNode);
            if (!position)
                return std::nullopt;
            int64_t firstId = position->baseLocationId + position->songIndex * locationsPerSong;
            return IsAccuracyMode(slotData.gameMode) ? firstId + slotData.gradeCount : firstId;
        }
    }

    Progression ComputeProgression(SlotData const& slotData, std::vector<std::string> const& receivedItemNames) {
        Progression progression;
        for (int startNode : slotData.startNodes)
            progression.unlockedNodes.insert(static_cast<uint32_t>(startNode));

        int songUnlocksReceived = 0;
        int categoryUnlocksReceived[categoryCount] = {};
        for (auto const& itemName : receivedItemNames) {
            if (itemName == victoryItem) {
                progression.victoryReceived = true;
            } else if (itemName == bloqKeyItem) {
                progression.bloqKeys++;
            } else if (itemName == progressiveSongUnlockItem && !IsRandomizedMode(slotData.gameMode)) {
                progression.unlockedNodes.insert(static_cast<uint32_t>(++songUnlocksReceived));
            } else if (auto categoryIndex = CategoryIndexForItem(itemName); categoryIndex && IsRandomizedMode(slotData.gameMode)) {
                int unlockCount = ++categoryUnlocksReceived[*categoryIndex];
                if (unlockCount < slotData.mapTypeCounts[*categoryIndex])
                    progression.unlockedNodes.insert(static_cast<uint32_t>(unlockCount + CategoryOffset(slotData, *categoryIndex)));
            }
        }

        progression.goalMapUnlocked = !slotData.lockGoalMap || progression.bloqKeys >= slotData.bloqKeysRequired;
        // with the goal locked, its final check only needs the keys, so the keys alone make it playable
        if (slotData.lockGoalMap && slotData.goalNode && progression.goalMapUnlocked)
            progression.unlockedNodes.insert(*slotData.goalNode);

        for (uint32_t node : progression.unlockedNodes) {
            auto ident = slotData.nodeToIdent.find(node);
            if (ident != slotData.nodeToIdent.end())
                progression.unlockedIdents.push_back(ident->second);
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
            return EqualsIgnoreCase(unlocked, ident);
        });
    }

    std::optional<uint32_t> FindNodeForIdent(SlotData const& slotData, std::string_view ident) {
        for (auto const& [candidateIdent, node] : slotData.identToNode) {
            if (EqualsIgnoreCase(candidateIdent, ident))
                return node;
        }
        return std::nullopt;
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

    std::vector<int64_t> LocationsForClear(SlotData const& slotData, Progression const& progression, uint32_t node, int gradeIndex) {
        std::vector<int64_t> locations;
        auto position = PositionForNode(slotData, node);
        if (!position)
            return locations;

        int64_t firstId = position->baseLocationId + position->songIndex * locationsPerSong;
        if (IsAccuracyMode(slotData.gameMode)) {
            int highestGrade = std::min(gradeIndex, slotData.gradeCount - 1);
            for (int grade = 0; grade <= highestGrade; grade++)
                locations.push_back(firstId + grade + 1);
        } else {
            locations.push_back(firstId);
        }

        if (!progression.goalMapUnlocked) {
            if (auto goalId = GoalLocationId(slotData))
                locations.erase(std::remove(locations.begin(), locations.end(), *goalId), locations.end());
        }
        return locations;
    }
}
