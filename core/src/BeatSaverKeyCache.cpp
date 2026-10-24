#include "archipelago/BeatSaverKeyCache.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "archipelago/LevelIdentity.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Archipelago {

    BeatSaverKeyCache::BeatSaverKeyCache(std::string cacheFilePath) : cacheFilePath(std::move(cacheFilePath)) {}

    void BeatSaverKeyCache::Load() {
        std::ifstream stream(cacheFilePath);
        if (!stream)
            return;
        std::stringstream contents;
        contents << stream.rdbuf();

        rapidjson::Document document;
        document.Parse(contents.str().c_str());
        if (document.HasParseError() || !document.IsObject())
            return;

        for (auto const& entry : document.GetObject()) {
            if (!entry.value.IsArray())
                continue;
            std::vector<std::string> hashes;
            for (auto const& hash : entry.value.GetArray()) {
                if (hash.IsString())
                    hashes.emplace_back(hash.GetString());
            }
            Store(entry.name.GetString(), hashes);
        }
    }

    bool BeatSaverKeyCache::Save() const {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        for (auto const& [beatSaverKey, hashes] : hashesByKey) {
            writer.Key(beatSaverKey.c_str());
            writer.StartArray();
            for (auto const& hash : hashes)
                writer.String(hash.c_str());
            writer.EndArray();
        }
        writer.EndObject();

        std::error_code ignoredError;
        std::filesystem::create_directories(std::filesystem::path(cacheFilePath).parent_path(), ignoredError);
        std::ofstream stream(cacheFilePath, std::ios::trunc);
        stream << buffer.GetString();
        return static_cast<bool>(stream);
    }

    bool BeatSaverKeyCache::Contains(std::string const& beatSaverKey) const {
        return hashesByKey.contains(ToLower(beatSaverKey));
    }

    void BeatSaverKeyCache::Store(std::string const& beatSaverKey, std::vector<std::string> const& hashes) {
        std::string normalizedKey = ToLower(beatSaverKey);
        auto& storedHashes = hashesByKey[normalizedKey];
        for (auto const& hash : hashes) {
            std::string normalizedHash = ToUpper(hash);
            if (std::find(storedHashes.begin(), storedHashes.end(), normalizedHash) == storedHashes.end())
                storedHashes.push_back(normalizedHash);
            keysByHash[normalizedHash] = normalizedKey;
        }
    }

    std::vector<std::string> BeatSaverKeyCache::HashesForKey(std::string const& beatSaverKey) const {
        auto entry = hashesByKey.find(ToLower(beatSaverKey));
        return entry == hashesByKey.end() ? std::vector<std::string>{} : entry->second;
    }

    std::optional<std::string> BeatSaverKeyCache::KeyForHash(std::string const& hash) const {
        auto entry = keysByHash.find(ToUpper(hash));
        if (entry == keysByHash.end())
            return std::nullopt;
        return entry->second;
    }
}
