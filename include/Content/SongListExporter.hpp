#pragma once

#include <string>

namespace BeatSaberQuestAP::SongListExporter {
    // writes every playable custom, official and owned DLC song as a YAML `songs:` block; main thread only
    void Export();
    bool IsExporting();
    std::string const& GetStatusText();
}
