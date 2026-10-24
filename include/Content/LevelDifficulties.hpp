#pragma once

#include <string>
#include <utility>
#include <vector>

namespace GlobalNamespace {
    class BeatmapLevel;
}

namespace BeatSaberQuestAP {
    // characteristic serialized name and difficulty (0 Easy to 4 Expert+) for every beatmap a level has
    std::vector<std::pair<std::string, int>> ListLevelDifficulties(GlobalNamespace::BeatmapLevel* level);
}
