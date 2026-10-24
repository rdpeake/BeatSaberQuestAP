#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "archipelago/SlotData.hpp"

namespace Archipelago {

    inline constexpr std::string_view progressiveSongUnlockItem = "Progressive Song Unlock";
    inline constexpr std::string_view victoryItem = "Victory";
    inline constexpr std::string_view categoryUnlockItems[categoryCount] = {
        "Progressive Speed Unlock",
        "Progressive Tech Unlock",
        "Progressive Midspeed Unlock",
        "Progressive Acc Unlock",
    };

    struct Progression {
        std::vector<std::string> unlockedIdents;
        bool victoryReceived = false;
    };

    // rebuilt from the full received item list each time, matching the PC client's login-time rules
    Progression ComputeProgression(SlotData const& slotData, std::vector<std::string> const& receivedItemNames);

    bool IsMapVisible(Progression const& progression, std::string_view mapIdentifier);

    bool IsIdentUnlocked(Progression const& progression, std::string_view ident);

    std::optional<uint32_t> FindNodeForIdent(SlotData const& slotData, std::string_view ident);

    // C, B, A, S, SS map to 0-4; anything below C is -1
    int GradeIndexFromRankName(std::string_view rankName);

    std::vector<int64_t> LocationsForClear(SlotData const& slotData, uint32_t node, int gradeIndex);
}
