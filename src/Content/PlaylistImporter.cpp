#include "Content/PlaylistImporter.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "Content/ContentResolver.hpp"
#include "Logger.hpp"
#include "ModPaths.hpp"
#include "archipelago/ApbsArchive.hpp"
#include "archipelago/LevelIdentity.hpp"
#include "rapidjson/document.h"
#include "songcore/shared/SongCore.hpp"

namespace BeatSaberQuestAP::PlaylistImporter {

    namespace {
        bool IsBeatSaverKey(std::string_view key) {
            return !key.empty() && key.size() <= 6 && std::all_of(key.begin(), key.end(), [](char character) { return std::isxdigit(static_cast<unsigned char>(character)); });
        }

        // apworld playlists list each song's BeatSaver key with its hash, which lets songs match without going online;
        // preset playlists repeat one hash for every song, so a hash listed more than once is ignored
        void LearnHashesFromPlaylist(std::string const& playlistPath) {
            std::ifstream stream(playlistPath);
            std::stringstream contents;
            contents << stream.rdbuf();
            rapidjson::Document playlist;
            playlist.Parse(contents.str().c_str());
            if (playlist.HasParseError() || !playlist.IsObject() || !playlist.HasMember("songs") || !playlist["songs"].IsArray())
                return;

            std::vector<std::pair<std::string, std::string>> keyHashPairs;
            std::map<std::string, int> occurrencesByHash;
            for (auto const& song : playlist["songs"].GetArray()) {
                if (!song.IsObject() || !song.HasMember("key") || !song["key"].IsString() || !song.HasMember("hash") || !song["hash"].IsString())
                    continue;
                std::string hash = Archipelago::ToUpper(song["hash"].GetString());
                occurrencesByHash[hash]++;
                if (IsBeatSaverKey(song["key"].GetString()))
                    keyHashPairs.emplace_back(song["key"].GetString(), hash);
            }
            for (auto const& [key, hash] : keyHashPairs) {
                if (occurrencesByHash[hash] == 1)
                    ContentResolver::Instance().LearnHashes(key, {hash});
            }
        }
    }

    std::string ImportPendingPatches() {
        std::filesystem::path const searchDirectories[] = {ModPaths::DataDirectory(), ModPaths::PlaylistsDirectory()};
        std::error_code ignoredError;

        std::vector<std::filesystem::path> patchFiles;
        for (auto const& directory : searchDirectories) {
            if (!std::filesystem::is_directory(directory, ignoredError))
                continue;
            for (auto const& entry : std::filesystem::directory_iterator(directory, ignoredError)) {
                if (entry.is_regular_file(ignoredError) && Archipelago::ToLower(entry.path().extension().string()) == ".apbs")
                    patchFiles.push_back(entry.path());
            }
        }

        // the playlists are only read for hash hints; the level list filter already shows the seed's songs
        auto extractDirectory = std::filesystem::path(ModPaths::DataDirectory()) / "apbs-extract";
        int readPatches = 0;
        int failedPatches = 0;
        for (auto const& patchFile : patchFiles) {
            try {
                for (auto const& playlistPath : Archipelago::ExtractPlaylistsFromApbs(patchFile.string(), extractDirectory.string()))
                    LearnHashesFromPlaylist(playlistPath);
                readPatches++;
                std::filesystem::rename(patchFile, patchFile.string() + ".imported", ignoredError);
                logger.info("Read {}", patchFile.string());
            } catch (std::exception const& error) {
                failedPatches++;
                logger.error("Could not read {}: {}", patchFile.string(), error.what());
            }
        }
        std::filesystem::remove_all(extractDirectory, ignoredError);

        if (readPatches == 0 && failedPatches == 0)
            return "";
        std::string summary = "Read " + std::to_string(readPatches) + " .apbs file(s)";
        if (failedPatches > 0)
            summary += ", " + std::to_string(failedPatches) + " could not be read";
        return summary;
    }

    int RemoveImportedPlaylists() {
        std::error_code ignoredError;
        auto playlistsDirectory = std::filesystem::path(ModPaths::PlaylistsDirectory());
        auto archiveDirectory = std::filesystem::path(ModPaths::DataDirectory()) / "removed-playlists";
        if (!std::filesystem::is_directory(playlistsDirectory, ignoredError))
            return 0;

        int removed = 0;
        for (auto const& entry : std::filesystem::directory_iterator(playlistsDirectory, ignoredError)) {
            if (!entry.is_regular_file(ignoredError) || Archipelago::ToLower(entry.path().extension().string()) != ".bplist")
                continue;
            std::ifstream stream(entry.path());
            std::stringstream contents;
            contents << stream.rdbuf();
            stream.close();
            rapidjson::Document playlist;
            playlist.Parse(contents.str().c_str());
            // earlier builds copied these from .apbs patches
            bool fromApworld = !playlist.HasParseError() && playlist.IsObject()
                && playlist.HasMember("playlistAuthor") && playlist["playlistAuthor"].IsString()
                && std::string_view(playlist["playlistAuthor"].GetString()) == "Archipelago"
                && playlist.HasMember("customData") && playlist["customData"].IsObject()
                && playlist["customData"].HasMember("node_connections");
            if (!fromApworld)
                continue;
            std::filesystem::create_directories(archiveDirectory, ignoredError);
            std::filesystem::rename(entry.path(), archiveDirectory / entry.path().filename(), ignoredError);
            if (!ignoredError)
                removed++;
        }
        if (removed > 0) {
            logger.info("Moved {} Archipelago playlist(s) to {}", removed, archiveDirectory.string());
            SongCore::API::Loading::RefreshLevelPacks();
        }
        return removed;
    }
}
