#pragma once

namespace BeatSaberQuestAP::ArchipelagoMenu {
    // adds the Archipelago button to the main menu's mod list
    void Register();

    // updates the open menu, if any, from the current session and content state
    void Refresh();
}
