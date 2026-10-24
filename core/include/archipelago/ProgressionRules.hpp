#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "archipelago/SlotData.hpp"

namespace Archipelago {

    // rules mirror the apworld (Rules.py, Locations.py) since the server only accepts its location ids

    inline constexpr std::string_view progressiveSongUnlockItem = "Progressive Song Unlock";
    inline constexpr std::string_view bloqKeyItem = "Bloq Key";
    inline constexpr std::string_view victoryItem = "Victory";
    inline constexpr std::string_view categoryUnlockItems[categoryCount] = {
        "Progressive Speed Unlock",
        "Progressive Tech Unlock",
        "Progressive Midspeed Unlock",
        "Progressive Accuracy Unlock",
    };
    // name the PC client used for the acc category
    inline constexpr std::string_view legacyAccuracyUnlockItem = "Progressive Acc Unlock";

    struct Progression {
        std::set<uint32_t> unlockedNodes;
        std::vector<std::string> unlockedIdents;
        int bloqKeys = 0;
        bool goalMapUnlocked = true;
        bool victoryReceived = false;
    };

    // rebuilt from the full received item list each time
    Progression ComputeProgression(SlotData const& slotData, std::vector<std::string> const& receivedItemNames);

    bool IsMapVisible(Progression const& progression, std::string_view mapIdentifier);

    bool IsIdentUnlocked(Progression const& progression, std::string_view ident);

    std::optional<uint32_t> FindNodeForIdent(SlotData const& slotData, std::string_view ident);

    // C, B, A, S, SS map to 0-4 (SSS counts as SS); anything below C is -1
    int GradeIndexFromRankName(std::string_view rankName);

    // ids checked by clearing a node with the given grade; the goal check is held back until enough Bloq Keys arrive
    std::vector<int64_t> LocationsForClear(SlotData const& slotData, Progression const& progression, uint32_t node, int gradeIndex);
}
