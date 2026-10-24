#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "rapidjson/document.h"

namespace Archipelago {

    enum class GameMode {
        PresetPass,
        PresetAccuracy,
        Categories,
    };

    inline constexpr int categoryCount = 4;  // speed, tech, midspeed, acc

    struct SlotData {
        bool deathLinkEnabled = false;
        std::string campaignName;
        std::string gameModeName;
        GameMode gameMode = GameMode::Categories;
        int gradeCount = 0;
        std::map<uint32_t, std::string> nodeToIdent;
        std::map<std::string, uint32_t> identToNode;
        std::vector<int> startNodes;
        std::vector<int> mapTypeCounts;
        std::map<std::string, std::string> locationNameToMnemonic;
    };

    // throws std::runtime_error naming the first missing or malformed key
    SlotData ParseSlotData(rapidjson::Value const& slotDataJson);

    GameMode GameModeFromName(std::string_view gameModeName);
}
