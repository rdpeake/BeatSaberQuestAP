#include "Content/LevelDifficulties.hpp"

#include <algorithm>

#include "GlobalNamespace/BeatmapBasicData.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapDifficulty.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "Logger.hpp"
#include "System/Collections/Generic/Dictionary_2.hpp"
#include "System/ValueTuple_2.hpp"
#include "UnityEngine/Resources.hpp"

namespace BeatSaberQuestAP {

    namespace {
        constexpr int difficultyCount = 5;

        using BeatmapLookupKey = System::ValueTuple_2<UnityW<GlobalNamespace::BeatmapCharacteristicSO>, GlobalNamespace::BeatmapDifficulty>;

        std::vector<UnityW<GlobalNamespace::BeatmapCharacteristicSO>> knownCharacteristics;

        std::vector<UnityW<GlobalNamespace::BeatmapCharacteristicSO>> const& KnownCharacteristics() {
            bool anyDestroyed = std::any_of(knownCharacteristics.begin(), knownCharacteristics.end(), [](auto const& characteristic) { return !characteristic; });
            if (!knownCharacteristics.empty() && !anyDestroyed)
                return knownCharacteristics;
            knownCharacteristics.clear();
            for (auto characteristic : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::BeatmapCharacteristicSO*>()) {
                if (characteristic)
                    knownCharacteristics.emplace_back(characteristic);
            }
            logger.debug("Found {} beatmap characteristics", knownCharacteristics.size());
            return knownCharacteristics;
        }

        bool HasBeatmap(GlobalNamespace::BeatmapLevel* level, GlobalNamespace::BeatmapCharacteristicSO* characteristic, GlobalNamespace::BeatmapDifficulty difficulty) {
            try {
                if (auto basicData = level->_beatmapBasicDatas)
                    return basicData->ContainsKey(BeatmapLookupKey(UnityW<GlobalNamespace::BeatmapCharacteristicSO>(characteristic), difficulty));
            } catch (std::exception const& error) {
                logger.debug("ContainsKey failed, falling back: {}", error.what());
            }
            try {
                return level->GetDifficultyBeatmapData(characteristic, difficulty) != nullptr;
            } catch (...) {
                return false;
            }
        }
    }

    // enumerating GetCharacteristics() or GetBeatmapKeys() yields nothing on device, so look up each pair instead
    std::vector<std::pair<std::string, int>> ListLevelDifficulties(GlobalNamespace::BeatmapLevel* level) {
        std::vector<std::pair<std::string, int>> difficulties;
        if (!level)
            return difficulties;
        for (auto characteristic : KnownCharacteristics()) {
            std::string characteristicName = static_cast<std::string>(characteristic->serializedName);
            for (int difficulty = 0; difficulty < difficultyCount; difficulty++) {
                if (!HasBeatmap(level, characteristic, static_cast<GlobalNamespace::BeatmapDifficulty>(difficulty)))
                    continue;
                std::pair<std::string, int> entry{characteristicName, difficulty};
                if (std::find(difficulties.begin(), difficulties.end(), entry) == difficulties.end())
                    difficulties.push_back(std::move(entry));
            }
        }
        return difficulties;
    }
}
