#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "archipelago/SlotData.hpp"

namespace Archipelago {

    struct SeedPlaylistGroup {
        std::string fileStem;
        std::string title;
        // in unlock order
        std::vector<uint32_t> nodes;
    };

    // the playlists the apworld would write: one per map type in randomized modes, one for preset modes
    std::vector<SeedPlaylistGroup> GroupSeedNodes(SlotData const& slotData);
}
