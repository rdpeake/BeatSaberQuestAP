#include "ModPaths.hpp"

#include "beatsaber-hook/shared/config/config-utils.hpp"

namespace BeatSaberQuestAP::ModPaths {

    std::string DataDirectory() {
        return getDataDir(MOD_ID);
    }

    std::string ConnectionSettingsFile() {
        return DataDirectory() + "connection.json";
    }

    std::string SessionFile() {
        return DataDirectory() + "session.json";
    }

    std::string CertificateBundleFile() {
        return DataDirectory() + "cacert.pem";
    }

    std::string BeatSaverKeyCacheFile() {
        return DataDirectory() + "beatsaver-keys.json";
    }

    // PlaylistCore reads playlists from PlaylistManager's data folder
    std::string PlaylistsDirectory() {
        return getDataDir("PlaylistManager") + "Playlists/";
    }
}
