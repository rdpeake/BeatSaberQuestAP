#include "archipelago/SeedPlaylists.hpp"

#include <algorithm>

#include "archipelago/LevelIdentity.hpp"

namespace Archipelago {

    namespace {
        constexpr char const* categoryNames[categoryCount] = {"Speed", "Tech", "Midspeed", "Acc"};

        std::string CampaignName(SlotData const& slotData) {
            return slotData.campaignName.empty() ? "Archipelago" : slotData.campaignName;
        }
    }

    std::vector<SeedPlaylistGroup> GroupSeedNodes(SlotData const& slotData) {
        std::vector<SeedPlaylistGroup> groups;
        if (!IsRandomizedMode(slotData.gameMode) || slotData.mapTypeCounts.size() < categoryCount) {
            SeedPlaylistGroup group{"Songs", CampaignName(slotData), {}};
            for (auto const& [node, ident] : slotData.nodeToIdent)
                group.nodes.push_back(node);
            groups.push_back(std::move(group));
            return groups;
        }

        // the seed names each song's map type; the counts are only a fallback for seeds without it
        bool everyNodeTyped = std::all_of(slotData.nodeToIdent.begin(), slotData.nodeToIdent.end(), [&](auto const& entry) {
            return slotData.nodeMapTypes.contains(entry.first);
        });
        if (everyNodeTyped) {
            for (int categoryIndex = 0; categoryIndex < categoryCount; categoryIndex++) {
                SeedPlaylistGroup group{categoryNames[categoryIndex], std::string(categoryNames[categoryIndex]) + " - " + CampaignName(slotData), {}};
                for (auto const& [node, ident] : slotData.nodeToIdent) {
                    if (slotData.nodeMapTypes.at(node) == ToLower(categoryNames[categoryIndex]))
                        group.nodes.push_back(node);
                }
                if (!group.nodes.empty())
                    groups.push_back(std::move(group));
            }
            return groups;
        }

        uint32_t firstNode = 0;
        for (int categoryIndex = 0; categoryIndex < categoryCount; categoryIndex++) {
            int categorySize = slotData.mapTypeCounts[categoryIndex];
            SeedPlaylistGroup group{categoryNames[categoryIndex], std::string(categoryNames[categoryIndex]) + " - " + CampaignName(slotData), {}};
            for (uint32_t node = firstNode; node < firstNode + categorySize; node++) {
                if (slotData.nodeToIdent.contains(node))
                    group.nodes.push_back(node);
            }
            firstNode += categorySize;
            if (!group.nodes.empty())
                groups.push_back(std::move(group));
        }
        return groups;
    }
}
