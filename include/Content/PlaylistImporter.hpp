#pragma once

#include <string>

namespace BeatSaberQuestAP::PlaylistImporter {
    // unpacks any .apbs patch found in the mod or playlists folder into PlaylistManager's playlists;
    // returns a short summary for the UI
    std::string ImportPendingPatches();
}
