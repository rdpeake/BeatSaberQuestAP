#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "Session.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "songcore/shared/SongCore.hpp"

using namespace GlobalNamespace;

namespace {
    UnityW<StandardLevelDetailView> lastDetailView;
    bool playButtonDisabled = false;

    void SetPlayButtonDisabled(bool disabled, std::string const& reason) {
        if (disabled == playButtonDisabled)
            return;
        playButtonDisabled = disabled;
        if (disabled)
            SongCore::API::PlayButton::DisablePlayButton(MOD_ID, reason);
        else
            SongCore::API::PlayButton::EnablePlayButton(MOD_ID);
    }

    // a difficulty that belongs to the seed but is still locked must not be playable
    void EvaluateSelectedDifficulty(StandardLevelDetailView* detailView) {
        auto& session = BeatSaberQuestAP::Session::Instance();
        if (!detailView || !session.IsConnected()) {
            SetPlayButtonDisabled(false, "");
            return;
        }
        BeatmapKey beatmapKey = detailView->beatmapKey;
        if (!beatmapKey.beatmapCharacteristic || !beatmapKey.levelId) {
            SetPlayButtonDisabled(false, "");
            return;
        }

        auto ident = session.IdentFor(
            static_cast<std::string>(beatmapKey.levelId),
            static_cast<std::string>(beatmapKey.beatmapCharacteristic->serializedName),
            static_cast<int>(beatmapKey.difficulty)
        );
        bool locked = ident && session.IsIdentInSeed(*ident) && !Archipelago::IsIdentUnlocked(session.GetProgression(), *ident);
        SetPlayButtonDisabled(locked, "This difficulty is still locked in Archipelago");
    }
}

MAKE_HOOK_MATCH(StandardLevelDetailView_RefreshContent, &StandardLevelDetailView::RefreshContent, void, StandardLevelDetailView* self) {
    StandardLevelDetailView_RefreshContent(self);
    lastDetailView = self;
    EvaluateSelectedDifficulty(self);
}

namespace BeatSaberQuestAP::Hooks {
    void InstallDifficultyLock() {
        INSTALL_HOOK(logger, StandardLevelDetailView_RefreshContent);
    }

    void RefreshDifficultyLock() {
        StandardLevelDetailView* detailView = lastDetailView ? static_cast<StandardLevelDetailView*>(lastDetailView) : nullptr;
        EvaluateSelectedDifficulty(detailView);
    }
}
