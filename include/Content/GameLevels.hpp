#pragma once

#include <string>
#include <vector>

#include "archipelago/LevelIdentity.hpp"

namespace GlobalNamespace {
    class BeatmapLevel;
    class BeatmapLevelsModel;
}

// lookups into the game's song library; main thread only
namespace BeatSaberQuestAP::GameLevels {
    // nullptr until the main menu has shown
    GlobalNamespace::BeatmapLevelsModel* GetBeatmapLevelsModel();

    // accepts ids with or without the legacy "level_" prefix
    GlobalNamespace::BeatmapLevel* FindOfficialLevel(std::string const& officialLevelId);

    // the first installed version among a BeatSaver map's hashes
    GlobalNamespace::BeatmapLevel* FindInstalledCustomLevel(std::vector<std::string> const& hashes);

    // every characteristic and difficulty the level has
    std::vector<Archipelago::MapDifficulty> ListDifficulties(GlobalNamespace::BeatmapLevel* level);
}
