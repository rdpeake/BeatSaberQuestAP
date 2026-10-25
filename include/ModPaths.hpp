#pragma once

#include <string>

namespace BeatSaberQuestAP::ModPaths {
    std::string DataDirectory();
    std::string ConnectionSettingsFile();
    std::string SessionFile();
    std::string PreferencesFile();
    std::string CertificateBundleFile();
    std::string BeatSaverKeyCacheFile();
    std::string PlaylistsDirectory();
}
