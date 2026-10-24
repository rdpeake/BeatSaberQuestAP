#pragma once

#include <string>

namespace BeatSaberQuestAP::MediaScanner {
    // registers a file written straight to shared storage so the Files app and pickers can see it
    void ScanFile(std::string path);
}
