#include "DeathLink.hpp"

#include "GlobalNamespace/StandardLevelGameplayManager.hpp"
#include "Logger.hpp"
#include "UnityEngine/Resources.hpp"

namespace BeatSaberQuestAP::DeathLink {

    namespace {
        bool failingFromDeathLink = false;
    }

    void ForceFailCurrentLevel() {
        auto gameplayManagers = UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::StandardLevelGameplayManager*>();
        for (auto gameplayManager : gameplayManagers) {
            if (!gameplayManager || !gameplayManager->isActiveAndEnabled)
                continue;
            if (auto initData = gameplayManager->_initData)
                initData->continueGameplayWith0Energy = false;
            failingFromDeathLink = true;
            gameplayManager->HandleGameEnergyDidReach0();
            logger.info("Level failed by DeathLink");
            return;
        }
    }

    bool ConsumeFailFromDeathLink() {
        bool wasDeathLink = failingFromDeathLink;
        failingFromDeathLink = false;
        return wasDeathLink;
    }
}
