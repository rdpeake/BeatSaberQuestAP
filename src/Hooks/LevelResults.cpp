#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/GameplayModifiers.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/MenuTransitionsHelper.hpp"
#include "GlobalNamespace/RankModel.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "Session.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"

using namespace GlobalNamespace;

MAKE_HOOK_MATCH(
    MenuTransitionsHelper_HandleMainGameSceneDidFinish,
    &MenuTransitionsHelper::HandleMainGameSceneDidFinish,
    void,
    MenuTransitionsHelper* self,
    StandardLevelScenesTransitionSetupDataSO* setupData,
    LevelCompletionResults* results
) {
    MenuTransitionsHelper_HandleMainGameSceneDidFinish(self, setupData, results);

    if (!setupData || !results)
        return;
    BeatmapKey beatmapKey = setupData->beatmapKey;
    if (!beatmapKey.beatmapCharacteristic)
        return;

    BeatSaberQuestAP::FinishedLevel finishedLevel;
    finishedLevel.levelId = static_cast<std::string>(beatmapKey.levelId);
    finishedLevel.characteristic = static_cast<std::string>(beatmapKey.beatmapCharacteristic->serializedName);
    finishedLevel.difficulty = static_cast<int>(beatmapKey.difficulty);
    finishedLevel.cleared = results->levelEndStateType == LevelCompletionResults::LevelEndStateType::Cleared;
    finishedLevel.failed = results->levelEndStateType == LevelCompletionResults::LevelEndStateType::Failed;
    finishedLevel.usedModifiers = setupData->gameplayModifiers && !setupData->gameplayModifiers->IsWithoutModifiers();
    finishedLevel.practice = setupData->practiceSettings != nullptr;
    finishedLevel.rankName = static_cast<std::string>(RankModel::GetRankName(results->rank));

    BeatSaberQuestAP::logger.info("Level {} finished: cleared={} failed={} rank={}", finishedLevel.levelId, finishedLevel.cleared, finishedLevel.failed, finishedLevel.rankName);
    BeatSaberQuestAP::Session::Instance().HandleLevelFinished(finishedLevel);
}

namespace BeatSaberQuestAP::Hooks {
    void InstallLevelResults() {
        INSTALL_HOOK(logger, MenuTransitionsHelper_HandleMainGameSceneDidFinish);
    }
}
