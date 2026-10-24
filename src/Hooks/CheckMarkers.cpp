#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/LevelListTableCell.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "Session.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"

using namespace GlobalNamespace;

namespace {
    std::string ProgressTag(BeatSaberQuestAP::CheckProgress const& progress) {
        char const* color = progress.completed == progress.total ? "#8FE388" : progress.completed > 0 ? "#FFD24A" : "#AAAAAA";
        return std::string(" <size=80%><color=") + color + ">[" + std::to_string(progress.completed) + "/" + std::to_string(progress.total) + "]</color></size>";
    }
}

// shows how many of a seed song's checks are done next to its name in the song list
MAKE_HOOK_MATCH(
    LevelListTableCell_SetDataFromLevelAsync,
    &LevelListTableCell::SetDataFromLevelAsync,
    void,
    LevelListTableCell* self,
    BeatmapLevel* beatmapLevel,
    bool isFavorite,
    bool isPromoted,
    bool isUpdated,
    bool interactable
) {
    LevelListTableCell_SetDataFromLevelAsync(self, beatmapLevel, isFavorite, isPromoted, isUpdated, interactable);
    auto& session = BeatSaberQuestAP::Session::Instance();
    if (!beatmapLevel || !session.IsInArchipelagoMode())
        return;
    UnityW<TMPro::TextMeshProUGUI> songNameText = self->_songNameText;
    if (!songNameText)
        return;
    try {
        if (auto progress = session.GetCheckProgress(static_cast<std::string>(beatmapLevel->levelID)); progress && progress->total > 0)
            songNameText->text = songNameText->text + ProgressTag(*progress);
    } catch (std::exception const& error) {
        logger.warn("Could not mark check progress: {}", error.what());
    }
}

namespace BeatSaberQuestAP::Hooks {
    void InstallCheckMarkers() {
        INSTALL_HOOK(logger, LevelListTableCell_SetDataFromLevelAsync);
    }
}
