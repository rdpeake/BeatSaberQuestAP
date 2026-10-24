#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/GameplayModifiers.hpp"
#include "GlobalNamespace/GameplayModifiersPanelController.hpp"
#include "GlobalNamespace/GameplaySetupViewController.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "Session.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "songcore/shared/SongCore.hpp"

using namespace GlobalNamespace;

namespace {
    struct PlayBlock {
        // shown on the play button itself, so it has to be short
        std::string buttonText;
        // listed by SongCore's requirements panel
        std::string reason;
    };

    UnityW<StandardLevelDetailView> lastDetailView;
    UnityW<GameplaySetupViewController> gameplaySetup;
    std::optional<std::string> activeReason;
    std::string originalPlayButtonText;

    TMPro::TextMeshProUGUI* PlayButtonText(StandardLevelDetailView* detailView) {
        if (!detailView)
            return nullptr;
        UnityW<UnityEngine::UI::Button> actionButton = detailView->actionButton;
        return actionButton ? actionButton->GetComponentInChildren<TMPro::TextMeshProUGUI*>() : nullptr;
    }

    void ApplyPlayBlock(StandardLevelDetailView* detailView, std::optional<PlayBlock> const& block) {
        auto buttonText = PlayButtonText(detailView);
        if (block) {
            if (buttonText && originalPlayButtonText.empty())
                originalPlayButtonText = static_cast<std::string>(buttonText->text);
            if (buttonText)
                buttonText->text = block->buttonText;
            if (activeReason != block->reason) {
                // SongCore keeps the first reason if a mod disables twice, so re-enable before changing it
                if (activeReason)
                    SongCore::API::PlayButton::EnablePlayButton(MOD_ID);
                SongCore::API::PlayButton::DisablePlayButton(MOD_ID, block->reason);
                activeReason = block->reason;
            }
            return;
        }

        if (buttonText && !originalPlayButtonText.empty())
            buttonText->text = originalPlayButtonText;
        if (activeReason) {
            SongCore::API::PlayButton::EnablePlayButton(MOD_ID);
            activeReason.reset();
        }
    }

    bool AreModifiersEnabled() {
        if (!gameplaySetup) {
            auto setups = UnityEngine::Resources::FindObjectsOfTypeAll<GameplaySetupViewController*>();
            if (setups.size() == 0)
                return false;
            gameplaySetup = setups[0];
        }
        auto modifiers = gameplaySetup->gameplayModifiers;
        return modifiers && !modifiers->IsWithoutModifiers();
    }

    // only difficulties that are part of the seed are guarded; anything else plays as normal
    std::optional<PlayBlock> FindPlayBlock(StandardLevelDetailView* detailView) {
        auto& session = BeatSaberQuestAP::Session::Instance();
        if (!detailView || !session.IsInArchipelagoMode())
            return std::nullopt;
        BeatmapKey beatmapKey = detailView->beatmapKey;
        if (!beatmapKey.beatmapCharacteristic || !beatmapKey.levelId)
            return std::nullopt;

        auto ident = session.IdentFor(
            static_cast<std::string>(beatmapKey.levelId),
            static_cast<std::string>(beatmapKey.beatmapCharacteristic->serializedName),
            static_cast<int>(beatmapKey.difficulty)
        );
        if (!ident || !session.IsIdentInSeed(*ident))
            return std::nullopt;
        if (!Archipelago::IsIdentUnlocked(session.GetProgression(), *ident))
            return PlayBlock{"Locked", "This difficulty is still locked in Archipelago"};
        if (AreModifiersEnabled())
            return PlayBlock{"Modifiers on", "Turn off modifiers: Archipelago only counts clears without them"};
        return std::nullopt;
    }

    void EvaluateSelectedDifficulty(StandardLevelDetailView* detailView) {
        ApplyPlayBlock(detailView, FindPlayBlock(detailView));
    }
}

MAKE_HOOK_MATCH(StandardLevelDetailView_RefreshContent, &StandardLevelDetailView::RefreshContent, void, StandardLevelDetailView* self) {
    StandardLevelDetailView_RefreshContent(self);
    lastDetailView = self;
    EvaluateSelectedDifficulty(self);
}

// runs whenever a modifier is toggled
MAKE_HOOK_MATCH(
    GameplayModifiersPanelController_RefreshTotalMultiplierAndRankUI,
    &GameplayModifiersPanelController::RefreshTotalMultiplierAndRankUI,
    void,
    GameplayModifiersPanelController* self
) {
    GameplayModifiersPanelController_RefreshTotalMultiplierAndRankUI(self);
    BeatSaberQuestAP::Hooks::RefreshDifficultyLock();
}

namespace BeatSaberQuestAP::Hooks {
    void InstallDifficultyLock() {
        INSTALL_HOOK(logger, StandardLevelDetailView_RefreshContent);
        INSTALL_HOOK(logger, GameplayModifiersPanelController_RefreshTotalMultiplierAndRankUI);
    }

    void RefreshDifficultyLock() {
        StandardLevelDetailView* detailView = lastDetailView ? static_cast<StandardLevelDetailView*>(lastDetailView) : nullptr;
        EvaluateSelectedDifficulty(detailView);
    }
}
