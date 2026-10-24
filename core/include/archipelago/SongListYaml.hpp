#pragma once

#include <string>
#include <vector>

namespace Archipelago {

    struct ExportedDifficulty {
        std::string characteristic;
        int difficulty = 0;
    };

    struct ExportedSong {
        std::string name;
        // BeatSaver key for custom songs, the game's level id for official ones
        std::string levelId;
        bool isOfficial = false;
        std::vector<ExportedDifficulty> difficulties;
    };

    // the entry each song gets: hardest Standard difficulty, else the hardest of the first characteristic
    ExportedDifficulty ChooseExportDifficulty(std::vector<ExportedDifficulty> const& difficulties);

    // a `Beat Saber:` section holding a `songs:` option for the apworld's preset game modes
    std::string BuildSongsYaml(std::vector<ExportedSong> songs, std::string const& headerComment);
}
