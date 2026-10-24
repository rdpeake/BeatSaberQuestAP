#include "Content/LevelDifficulties.hpp"

#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapDifficulty.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "System/Collections/Generic/IEnumerable_1.hpp"
#include "System/Collections/Generic/IEnumerator_1.hpp"
#include "System/Collections/IEnumerator.hpp"

namespace BeatSaberQuestAP {

    // walks characteristics then their difficulties, the same enumerators PlaylistManager relies on;
    // reading whole BeatmapKey structs from GetBeatmapKeys() reported nothing on device
    std::vector<std::pair<std::string, int>> ListLevelDifficulties(GlobalNamespace::BeatmapLevel* level) {
        std::vector<std::pair<std::string, int>> difficulties;
        if (!level)
            return difficulties;
        auto characteristics = level->GetCharacteristics()->GetEnumerator();
        while (characteristics->i___System__Collections__IEnumerator()->MoveNext()) {
            UnityW<GlobalNamespace::BeatmapCharacteristicSO> characteristic = characteristics->Current;
            if (!characteristic)
                continue;
            std::string characteristicName = static_cast<std::string>(characteristic->serializedName);
            auto levelDifficulties = level->GetDifficulties(characteristic)->GetEnumerator();
            while (levelDifficulties->i___System__Collections__IEnumerator()->MoveNext())
                difficulties.emplace_back(characteristicName, static_cast<int>(levelDifficulties->Current));
        }
        return difficulties;
    }
}
