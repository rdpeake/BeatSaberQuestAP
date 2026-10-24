#pragma once

#include <string>
#include <vector>

namespace Archipelago {

    // an .apbs patch is a zip holding one or more .bplist playlists;
    // writes each playlist into outputDirectory and returns the written paths
    // throws std::runtime_error when the archive is unreadable or has no playlist
    std::vector<std::string> ExtractPlaylistsFromApbs(std::string const& apbsPath, std::string const& outputDirectory);
}
