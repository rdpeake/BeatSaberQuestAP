#include "Content/SeedPlaylists.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "archipelago/LevelIdentity.hpp"
#include "archipelago/SeedPlaylists.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "Content/ContentResolver.hpp"
#include "Content/PlaylistIcon.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/BeatmapLevelsModel.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "ModPaths.hpp"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "Session.hpp"
#include "songcore/shared/SongCore.hpp"

namespace BeatSaberQuestAP::SeedPlaylists {

    namespace {
        // marks files this mod owns so stale ones can be removed
        constexpr std::string_view filePrefix = "BeatSaberQuestAP_";
        constexpr char const* difficultyNames[] = {"Easy", "Normal", "Hard", "Expert", "ExpertPlus"};

        std::map<std::string, std::string> lastSynced;
        bool synced = false;

        struct PlaylistSong {
            std::string levelId;
            std::string hash;
            std::string key;
            std::string songName;
            std::string characteristic;
            int difficulty = 0;
        };

        GlobalNamespace::BeatmapLevelsModel* GetBeatmapLevelsModel() {
            auto mainFlowCoordinator = BSML::Helpers::GetMainFlowCoordinator();
            return mainFlowCoordinator ? mainFlowCoordinator->_beatmapLevelsModel : nullptr;
        }

        std::optional<PlaylistSong> SongForNode(Archipelago::SlotData const& slotData, uint32_t node, GlobalNamespace::BeatmapLevelsModel* levelsModel) {
            auto ident = slotData.nodeToIdent.find(node);
            if (ident == slotData.nodeToIdent.end())
                return std::nullopt;
            auto parsed = Archipelago::ParseIdent(ident->second);
            if (!parsed || parsed->difficulty < 0 || parsed->difficulty > 4)
                return std::nullopt;

            PlaylistSong song;
            song.characteristic = parsed->characteristic;
            song.difficulty = parsed->difficulty;
            if (auto name = slotData.songNames.find(node); name != slotData.songNames.end())
                song.songName = name->second;

            if (parsed->isOfficial) {
                GlobalNamespace::BeatmapLevel* level = nullptr;
                if (levelsModel) {
                    level = levelsModel->GetBeatmapLevel(parsed->officialLevelId);
                    if (!level)
                        level = levelsModel->GetBeatmapLevel("level_" + parsed->officialLevelId);
                }
                song.levelId = level ? static_cast<std::string>(level->levelID) : parsed->officialLevelId;
                return song;
            }

            song.key = parsed->beatSaverKey;
            auto hashes = ContentResolver::Instance().HashesForBeatSaverKey(parsed->beatSaverKey);
            for (auto const& hash : hashes) {
                if (SongCore::API::Loading::GetLevelByHash(hash)) {
                    song.hash = hash;
                    break;
                }
            }
            // a missing song still gets listed so PlaylistManager can download it
            if (song.hash.empty() && !hashes.empty())
                song.hash = hashes.back();
            if (song.hash.empty())
                return std::nullopt;
            song.levelId = "custom_level_" + song.hash;
            return song;
        }

        std::string BuildPlaylistJson(std::string const& title, std::vector<PlaylistSong> const& songs) {
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            writer.StartObject();
            writer.Key("playlistTitle");
            writer.String(title);
            writer.Key("playlistAuthor");
            writer.String("Archipelago");
            writer.Key("image");
            writer.String("data:image/png;base64," + std::string(archipelagoPlaylistIcon));
            writer.Key("songs");
            writer.StartArray();
            for (auto const& song : songs) {
                writer.StartObject();
                writer.Key("levelid");
                writer.String(song.levelId);
                if (!song.hash.empty()) {
                    writer.Key("hash");
                    writer.String(song.hash);
                }
                if (!song.key.empty()) {
                    writer.Key("key");
                    writer.String(song.key);
                }
                if (!song.songName.empty()) {
                    writer.Key("songName");
                    writer.String(song.songName);
                }
                writer.Key("difficulties");
                writer.StartArray();
                writer.StartObject();
                writer.Key("characteristic");
                writer.String(song.characteristic);
                writer.Key("name");
                writer.String(difficultyNames[song.difficulty]);
                writer.EndObject();
                writer.EndArray();
                writer.EndObject();
            }
            writer.EndArray();
            writer.EndObject();
            return buffer.GetString();
        }

        std::map<std::string, std::string> DesiredPlaylists() {
            std::map<std::string, std::string> playlists;
            auto& session = Session::Instance();
            auto const& slotData = session.GetSlotData();
            if (!session.IsInArchipelagoMode() || !slotData)
                return playlists;
            auto levelsModel = GetBeatmapLevelsModel();
            for (auto const& group : Archipelago::GroupSeedNodes(*slotData)) {
                std::vector<PlaylistSong> songs;
                for (auto node : group.nodes) {
                    if (auto song = SongForNode(*slotData, node, levelsModel))
                        songs.push_back(std::move(*song));
                }
                if (!songs.empty())
                    playlists[std::string(filePrefix) + group.fileStem + ".bplist"] = BuildPlaylistJson(group.title, songs);
            }
            return playlists;
        }

        std::string ReadFile(std::filesystem::path const& path) {
            std::ifstream stream(path, std::ios::binary);
            std::stringstream contents;
            contents << stream.rdbuf();
            return contents.str();
        }

        // writes changed playlists and removes ours that are no longer wanted; true when anything changed
        bool WritePlaylists(std::map<std::string, std::string> const& playlists) {
            std::error_code ignoredError;
            auto directory = std::filesystem::path(ModPaths::PlaylistsDirectory());
            std::filesystem::create_directories(directory, ignoredError);
            bool changed = false;

            for (auto const& entry : std::filesystem::directory_iterator(directory, ignoredError)) {
                auto fileName = entry.path().filename().string();
                if (fileName.starts_with(filePrefix) && !playlists.contains(fileName)) {
                    std::filesystem::remove(entry.path(), ignoredError);
                    changed = true;
                }
            }
            for (auto const& [fileName, json] : playlists) {
                auto path = directory / fileName;
                if (std::filesystem::exists(path, ignoredError) && ReadFile(path) == json)
                    continue;
                std::ofstream stream(path, std::ios::binary | std::ios::trunc);
                stream << json;
                changed = true;
            }
            return changed;
        }
    }

    void Sync() {
        if (!Hooks::IsMainMenuReady() || !SongCore::API::Loading::AreSongsLoaded() || SongCore::API::Loading::AreSongsRefreshing())
            return;
        try {
            auto playlists = DesiredPlaylists();
            if (synced && playlists == lastSynced)
                return;
            bool changed = WritePlaylists(playlists);
            lastSynced = std::move(playlists);
            synced = true;
            if (changed) {
                logger.info("Updated {} seed playlist(s)", lastSynced.size());
                SongCore::API::Loading::RefreshLevelPacks();
            }
        } catch (std::exception const& error) {
            logger.error("Could not update seed playlists: {}", error.what());
        }
    }
}
