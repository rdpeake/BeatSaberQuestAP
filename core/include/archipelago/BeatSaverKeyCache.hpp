#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Archipelago {

    // remembers which map hashes belong to each BeatSaver key so idents can be
    // built for installed songs without asking BeatSaver every launch
    class BeatSaverKeyCache {
    public:
        explicit BeatSaverKeyCache(std::string cacheFilePath);

        void Load();
        bool Save() const;

        bool Contains(std::string const& beatSaverKey) const;
        void Store(std::string const& beatSaverKey, std::vector<std::string> const& hashes);

        std::vector<std::string> HashesForKey(std::string const& beatSaverKey) const;
        std::optional<std::string> KeyForHash(std::string const& hash) const;

    private:
        std::string cacheFilePath;
        std::map<std::string, std::vector<std::string>> hashesByKey;
        std::map<std::string, std::string> keysByHash;
    };
}
