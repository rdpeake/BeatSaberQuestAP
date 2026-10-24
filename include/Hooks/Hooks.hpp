#pragma once

namespace BeatSaberQuestAP::Hooks {
    void InstallLevelListFilter();
    void InstallLevelResults();
    void InstallDifficultyLock();

    // re-filters the visible song list after unlocks or downloads change
    void RefreshLevelList();

    // re-evaluates the play button for the currently selected difficulty
    void RefreshDifficultyLock();
}
