#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Archipelago {

    // the last joined slot, kept so songs stay locked to it across disconnects and restarts;
    // the password is never stored
    struct SavedSession {
        std::string host;
        int port = 0;
        std::string slotName;
        bool passwordRequired = false;
        std::string slotDataJson;
        std::vector<std::string> receivedItemNames;
        std::vector<int64_t> unconfirmedLocations;
    };

    std::optional<SavedSession> LoadSavedSession(std::string const& filePath);
    bool WriteSavedSession(std::string const& filePath, SavedSession const& session);
    void DeleteSavedSession(std::string const& filePath);
}
