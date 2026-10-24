#pragma once

#include <string>

namespace BeatSaberQuestAP::PlaylistImporter {
    // reads any .apbs patch found in the mod or playlists folder for key/hash hints;
    // returns a short summary for the UI
    std::string ImportPendingPatches();

    // moves playlists that earlier builds unpacked from .apbs patches out of PlaylistManager's folder
    int RemoveImportedPlaylists();
}
