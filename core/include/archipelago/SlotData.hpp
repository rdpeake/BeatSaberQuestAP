#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "rapidjson/document.h"

namespace Archipelago {

    // values match the apworld's GameMode choice
    enum class GameMode {
        RandomizedPP = 0,
        RandomizedAccuracy = 1,
        RandomizedPass = 2,
        PresetAccuracy = 3,
        PresetPass = 4,
    };

    inline constexpr int categoryCount = 4;  // speed, tech, midspeed, acc

    bool IsRandomizedMode(GameMode gameMode);
    bool IsAccuracyMode(GameMode gameMode);

    struct SlotData {
        bool deathLinkEnabled = false;
        std::string campaignName;
        GameMode gameMode = GameMode::RandomizedAccuracy;
        // locations per song in accuracy modes: grades C up to the threshold
        int gradeCount = 1;
        std::map<uint32_t, std::string> nodeToIdent;
        std::map<std::string, uint32_t> identToNode;
        std::map<uint32_t, std::string> songNames;
        // randomized modes: "speed", "tech", "midspeed" or "acc" per node
        std::map<uint32_t, std::string> nodeMapTypes;
        std::vector<int> startNodes;
        // only present in randomized modes
        std::vector<int> mapTypeCounts;
        bool lockGoalMap = false;
        std::optional<uint32_t> goalNode;
        int bloqKeysRequired = 0;
        std::map<std::string, std::string> locationNameToMnemonic;
    };

    // throws std::runtime_error naming the first missing or malformed key
    SlotData ParseSlotData(rapidjson::Value const& slotDataJson);

    // accepts the numeric choice value or its option_ name
    GameMode ParseGameMode(rapidjson::Value const& value);
}
