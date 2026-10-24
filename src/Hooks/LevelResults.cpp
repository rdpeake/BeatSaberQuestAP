#include <cmath>

#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/GameplayModifiers.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/MenuTransitionsHelper.hpp"
#include "GlobalNamespace/PracticeSettings.hpp"
#include "GlobalNamespace/RankModel.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "Session.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"

using namespace GlobalNamespace;

namespace {
    // both hooks below can report the same result; handle each one once
    LevelCompletionResults* lastHandledResults = nullptr;

    // practice settings may be attached to normal plays, so only a changed start or speed counts as practice
    bool IsRealPractice(PracticeSettings* practiceSettings) {
        if (!practiceSettings)
            return false;
        return practiceSettings->startSongTime > 0.01f || std::abs(practiceSettings->songSpeedMul - 1.0f) > 0.001f || practiceSettings->startInAdvanceAndClearNotes;
    }

    void ReportFinishedLevel(StandardLevelScenesTransitionSetupDataSO* setupData, LevelCompletionResults* results) {
        if (!setupData || !results || results == lastHandledResults)
            return;
        lastHandledResults = results;

        BeatmapKey beatmapKey = setupData->beatmapKey;
        if (!beatmapKey.beatmapCharacteristic) {
            BeatSaberQuestAP::logger.warn("Finished level has no characteristic, ignoring");
            return;
        }

        BeatSaberQuestAP::FinishedLevel finishedLevel;
        finishedLevel.levelId = static_cast<std::string>(beatmapKey.levelId);
        if (auto level = setupData->beatmapLevel)
            finishedLevel.songName = static_cast<std::string>(level->songName);
        finishedLevel.characteristic = static_cast<std::string>(beatmapKey.beatmapCharacteristic->serializedName);
        finishedLevel.difficulty = static_cast<int>(beatmapKey.difficulty);
        finishedLevel.cleared = results->levelEndStateType == LevelCompletionResults::LevelEndStateType::Cleared;
        finishedLevel.failed = results->levelEndStateType == LevelCompletionResults::LevelEndStateType::Failed;
        finishedLevel.usedModifiers = setupData->gameplayModifiers && !setupData->gameplayModifiers->IsWithoutModifiers();
        finishedLevel.practice = IsRealPractice(setupData->practiceSettings);
        finishedLevel.rankName = static_cast<std::string>(RankModel::GetRankName(results->rank));

        BeatSaberQuestAP::logger.info("Level {} finished: state={} rank={} modifiers={} practice={}", finishedLevel.levelId,
            static_cast<int>(results->levelEndStateType), finishedLevel.rankName, finishedLevel.usedModifiers, finishedLevel.practice);
        BeatSaberQuestAP::Session::Instance().HandleLevelFinished(finishedLevel);
    }
}

MAKE_HOOK_MATCH(
    StandardLevelScenesTransitionSetupDataSO_Finish,
    &StandardLevelScenesTransitionSetupDataSO::Finish,
    void,
    StandardLevelScenesTransitionSetupDataSO* self,
    LevelCompletionResults* results
) {
    StandardLevelScenesTransitionSetupDataSO_Finish(self, results);
    ReportFinishedLevel(self, results);
}

MAKE_HOOK_MATCH(
    MenuTransitionsHelper_HandleMainGameSceneDidFinish,
    &MenuTransitionsHelper::HandleMainGameSceneDidFinish,
    void,
    MenuTransitionsHelper* self,
    StandardLevelScenesTransitionSetupDataSO* setupData,
    LevelCompletionResults* results
) {
    MenuTransitionsHelper_HandleMainGameSceneDidFinish(self, setupData, results);
    ReportFinishedLevel(setupData, results);
}

namespace BeatSaberQuestAP::Hooks {
    void InstallLevelResults() {
        INSTALL_HOOK(logger, StandardLevelScenesTransitionSetupDataSO_Finish);
        INSTALL_HOOK(logger, MenuTransitionsHelper_HandleMainGameSceneDidFinish);
    }
}
