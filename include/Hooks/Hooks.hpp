#pragma once

#include <functional>

namespace BeatSaberQuestAP::Hooks {
    void InstallLevelListFilter();
    void InstallLevelResults();
    void InstallDifficultyLock();
    void InstallMainMenuReady();

    // the main menu has shown at least once, so game services are safe to use
    bool IsMainMenuReady();
    void RunWhenMainMenuReady(std::function<void()> work);

    // re-filters the visible song list after unlocks or downloads change
    void RefreshLevelList();

    // re-evaluates the play button for the currently selected difficulty
    void RefreshDifficultyLock();
}
