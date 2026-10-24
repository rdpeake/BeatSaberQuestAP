#pragma once

#include <string>

namespace BeatSaberQuestAP::ModPaths {
    std::string DataDirectory();
    std::string ConnectionSettingsFile();
    std::string CertificateBundleFile();
    std::string BeatSaverKeyCacheFile();
    std::string PlaylistsDirectory();
}
