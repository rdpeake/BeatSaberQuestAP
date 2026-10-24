#include "Content/PlaylistImporter.hpp"

#include <filesystem>
#include <fstream>
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
        // apworld playlists list each song's BeatSaver key with its hash, which lets songs match without going online
        void LearnHashesFromPlaylist(std::string const& playlistPath) {
            std::ifstream stream(playlistPath);
            std::stringstream contents;
            contents << stream.rdbuf();
            rapidjson::Document playlist;
            playlist.Parse(contents.str().c_str());
            if (playlist.HasParseError() || !playlist.IsObject() || !playlist.HasMember("songs") || !playlist["songs"].IsArray())
                return;
            for (auto const& song : playlist["songs"].GetArray()) {
                if (!song.IsObject() || !song.HasMember("key") || !song["key"].IsString() || !song.HasMember("hash") || !song["hash"].IsString())
                    continue;
                ContentResolver::Instance().LearnHashes(song["key"].GetString(), {song["hash"].GetString()});
            }
        }
    }

    std::string ImportPendingPatches() {
        std::filesystem::path const searchDirectories[] = {ModPaths::DataDirectory(), ModPaths::PlaylistsDirectory()};
        int importedPlaylists = 0;
        int failedPatches = 0;
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

        for (auto const& patchFile : patchFiles) {
            try {
                auto written = Archipelago::ExtractPlaylistsFromApbs(patchFile.string(), ModPaths::PlaylistsDirectory());
                importedPlaylists += static_cast<int>(written.size());
                for (auto const& playlistPath : written)
                    LearnHashesFromPlaylist(playlistPath);
                // keep the patch, renamed, in case the player needs it again
                std::filesystem::rename(patchFile, patchFile.string() + ".imported", ignoredError);
                logger.info("Imported {} playlist(s) from {}", written.size(), patchFile.string());
            } catch (std::exception const& error) {
                failedPatches++;
                logger.error("Could not import {}: {}", patchFile.string(), error.what());
            }
        }

        if (importedPlaylists > 0)
            SongCore::API::Loading::RefreshLevelPacks();

        if (importedPlaylists == 0 && failedPatches == 0)
            return "";
        std::string summary = "Imported " + std::to_string(importedPlaylists) + " playlist(s)";
        if (failedPatches > 0)
            summary += ", " + std::to_string(failedPatches) + " .apbs file(s) could not be read";
        return summary;
    }
}
