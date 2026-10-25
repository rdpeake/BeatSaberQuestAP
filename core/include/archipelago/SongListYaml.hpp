#pragma once

#include <string>
#include <vector>

#include "archipelago/LevelIdentity.hpp"

namespace Archipelago {

    struct ExportedSong {
        std::string name;
        // BeatSaver key for custom songs, the game's level id for official ones
        std::string levelId;
        bool isOfficial = false;
        std::vector<MapDifficulty> difficulties;
    };

    // the entry each song gets: hardest Standard difficulty, else the hardest of the first characteristic
    MapDifficulty ChooseExportDifficulty(std::vector<MapDifficulty> const& difficulties);

    // a `Beat Saber:` section holding a `songs:` option for the apworld's preset game modes
    std::string BuildSongsYaml(std::vector<ExportedSong> songs, std::string const& headerComment);
}
