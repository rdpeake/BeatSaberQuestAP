#pragma once

#include "archipelago/ArchipelagoClient.hpp"

namespace BeatSaberQuestAP::SettingsStore {
    // the password is deliberately never written to disk
    Archipelago::ConnectionSettings LoadConnectionSettings();
    void SaveConnectionSettings(Archipelago::ConnectionSettings const& settings);

    bool LoadShowDebugMessages();
    void SaveShowDebugMessages(bool showDebugMessages);
}
