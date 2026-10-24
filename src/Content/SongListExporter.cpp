#include "Content/SongListExporter.hpp"

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <span>
#include <thread>

#include "archipelago/LevelIdentity.hpp"
#include "archipelago/SongListYaml.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"
#include "beatsaverplusplus/shared/BeatSaver.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "Content/ContentResolver.hpp"
#include "Content/GameLevels.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/BeatmapLevelPack.hpp"
#include "GlobalNamespace/BeatmapLevelsModel.hpp"
#include "GlobalNamespace/BeatmapLevelsRepository.hpp"
#include "GlobalNamespace/EntitlementStatus.hpp"
#include "GlobalNamespace/IEntitlementModel.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "ModPaths.hpp"
#include "Session.hpp"
#include "System/Threading/CancellationToken.hpp"
#include "System/Threading/Tasks/Task_1.hpp"
#include "UnityEngine/GUIUtility.hpp"
#include "songcore/shared/SongCore.hpp"

namespace BeatSaberQuestAP::SongListExporter {

    namespace {
        // other apps can't list files Beat Saber owns, but USB file transfer and adb can
        constexpr char const* downloadsDirectory = "/sdcard/Download/";
        constexpr char const* exportFileName = "BeatSaberAP-songs.yaml";
        constexpr size_t hashesPerBeatSaverRequest = 50;

        struct KeyLookup {
            std::string hash;
            std::string key;
            std::vector<std::string> versionHashes;
        };

        struct ExportJob {
            std::vector<Archipelago::ExportedSong> songs;
            // song index waiting on a BeatSaver key, by hash
            std::map<std::string, size_t> songsAwaitingKey;
            int pendingSteps = 0;
            int skippedDlcPacks = 0;
            bool dlcCheckFailed = false;
        };

        bool exporting = false;
        std::string statusText;

        void SetStatus(std::string text) {
            statusText = std::move(text);
            Session::Instance().NotifyChanged();
        }

        // BeatSaver downloads are named "<key> (<song> - <mapper>)"
        std::optional<std::string> KeyFromFolderName(std::string_view levelPath) {
            auto folderName = std::filesystem::path(levelPath).filename().string();
            auto keyEnd = folderName.find(' ');
            if (keyEnd == std::string::npos || keyEnd == 0 || keyEnd > 6 || folderName.size() <= keyEnd + 1 || folderName[keyEnd + 1] != '(')
                return std::nullopt;
            auto key = folderName.substr(0, keyEnd);
            for (char character : key) {
                if (!std::isxdigit(static_cast<unsigned char>(character)))
                    return std::nullopt;
            }
            return Archipelago::ToLower(key);
        }

        std::vector<KeyLookup> LookUpKeys(std::vector<std::string> const& hashes) {
            std::vector<KeyLookup> lookups;
            auto addLookup = [&](std::string const& hash, BeatSaver::Models::Beatmap const& beatmap) {
                KeyLookup lookup{hash, Archipelago::ToLower(beatmap.Id), {}};
                for (auto const& version : beatmap.Versions)
                    lookup.versionHashes.push_back(version.Hash);
                lookups.push_back(std::move(lookup));
            };
            for (size_t start = 0; start < hashes.size(); start += hashesPerBeatSaverRequest) {
                std::span<std::string const> batch(hashes.data() + start, std::min(hashesPerBeatSaverRequest, hashes.size() - start));
                try {
                    // the multi-hash endpoint answers differently for a single hash
                    if (batch.size() == 1) {
                        auto response = BeatSaver::API::GetBeatmapByHash(batch.front());
                        if (response.IsSuccessful() && response.responseData)
                            addLookup(batch.front(), *response.responseData);
                        continue;
                    }
                    auto response = BeatSaver::API::GetBeatmapsByHashes(batch);
                    if (!response.IsSuccessful() || !response.responseData)
                        continue;
                    for (auto const& [hash, beatmap] : *response.responseData)
                        addLookup(hash, beatmap);
                } catch (std::exception const& error) {
                    logger.warn("BeatSaver hash lookup failed: {}", error.what());
                }
            }
            return lookups;
        }

        std::string CurrentDate() {
            std::time_t now = std::time(nullptr);
            char date[32] = {};
            std::strftime(date, sizeof(date), "%Y-%m-%d", std::localtime(&now));
            return date;
        }

        std::optional<std::string> WriteExport(std::string const& yaml) {
            for (std::string directory : {std::string(downloadsDirectory), ModPaths::DataDirectory()}) {
                std::error_code error;
                std::filesystem::create_directories(directory, error);
                auto path = directory + exportFileName;
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file << yaml;
                file.close();
                if (file.good())
                    return path;
                logger.warn("Could not write {}", path);
            }
            return std::nullopt;
        }

        void Finish(std::shared_ptr<ExportJob> job) {
            std::vector<Archipelago::ExportedSong> exportedSongs;
            int customCount = 0;
            int officialCount = 0;
            int withoutDifficulties = 0;
            for (auto& song : job->songs) {
                if (song.levelId.empty())
                    continue;
                if (song.difficulties.empty()) {
                    withoutDifficulties++;
                    continue;
                }
                (song.isOfficial ? officialCount : customCount)++;
                exportedSongs.push_back(std::move(song));
            }
            int unknownToBeatSaver = static_cast<int>(job->songsAwaitingKey.size());

            std::string header = "Exported by BeatSaberQuestAP on " + CurrentDate() + "\n"
                + "Copy the songs block under \"Beat Saber:\" in your YAML, set game_mode to presetPass or presetAccuracy,\n"
                + "and delete any songs you don't want. Each song only counts on the difficulty listed.";
            if (unknownToBeatSaver > 0)
                header += "\n" + std::to_string(unknownToBeatSaver) + " installed song(s) were left out because BeatSaver doesn't know them";
            auto yaml = Archipelago::BuildSongsYaml(std::move(exportedSongs), header);
            exporting = false;

            bool copied = false;
            try {
                UnityEngine::GUIUtility::set_systemCopyBuffer(yaml);
                copied = true;
            } catch (std::exception const& error) {
                logger.warn("Could not copy the song list to the clipboard: {}", error.what());
            }
            auto path = WriteExport(yaml);
            if (!copied && !path) {
                SetStatus("<color=#FF6B6B>Could not copy or save the song list</color>");
                return;
            }

            std::string songCounts = std::to_string(customCount) + " custom and " + std::to_string(officialCount) + " official songs";
            std::string summary = copied ? "Copied " + songCounts + " to the clipboard" : "Exported " + songCounts;
            if (path)
                summary += "\nSaved to " + *path + " (copy it off over USB)";
            if (unknownToBeatSaver > 0)
                summary += "\n" + std::to_string(unknownToBeatSaver) + " not on BeatSaver were left out";
            if (withoutDifficulties > 0)
                summary += "\n" + std::to_string(withoutDifficulties) + " with no readable difficulties were left out";
            if (job->dlcCheckFailed)
                summary += "\nDLC ownership couldn't be checked, so DLC songs were left out";
            Session::Instance().AddLogMessage(summary);
            SetStatus(summary);
        }

        void CompleteStep(std::shared_ptr<ExportJob> job) {
            if (--job->pendingSteps == 0)
                Finish(std::move(job));
        }

        void AddOfficialPack(ExportJob& job, GlobalNamespace::BeatmapLevelPack* pack) {
            if (!pack || !pack->_beatmapLevels)
                return;
            for (auto level : pack->_beatmapLevels) {
                if (!level)
                    continue;
                job.songs.push_back({
                    static_cast<std::string>(level->songName),
                    Archipelago::OfficialSongLevelId(static_cast<std::string>(level->levelID)),
                    true,
                    GameLevels::ListDifficulties(level),
                });
            }
        }

        void AddCustomSongs(ExportJob& job) {
            auto& resolver = ContentResolver::Instance();
            for (auto level : SongCore::API::Loading::GetAllLevels()) {
                if (!level)
                    continue;
                auto hash = Archipelago::HashFromCustomLevelId(static_cast<std::string>(level->levelID));
                auto key = resolver.BeatSaverKeyForHash(hash);
                if (!key)
                    key = KeyFromFolderName(level->customLevelPath);
                job.songs.push_back({
                    static_cast<std::string>(level->songName),
                    key ? Archipelago::ToLower(*key) : "",
                    false,
                    GameLevels::ListDifficulties(level),
                });
                if (!key && !hash.empty())
                    job.songsAwaitingKey.emplace(hash, job.songs.size() - 1);
            }
        }

        void ResolveUnknownKeys(std::shared_ptr<ExportJob> job) {
            if (job->songsAwaitingKey.empty())
                return;
            std::vector<std::string> hashes;
            for (auto const& [hash, songIndex] : job->songsAwaitingKey)
                hashes.push_back(hash);
            job->pendingSteps++;
            std::thread([job, hashes = std::move(hashes)] {
                auto lookups = LookUpKeys(hashes);
                BSML::MainThreadScheduler::Schedule([job, lookups = std::move(lookups)] {
                    for (auto const& lookup : lookups) {
                        auto awaiting = job->songsAwaitingKey.find(Archipelago::ToUpper(lookup.hash));
                        if (awaiting == job->songsAwaitingKey.end())
                            continue;
                        job->songs[awaiting->second].levelId = lookup.key;
                        job->songsAwaitingKey.erase(awaiting);
                        ContentResolver::Instance().LearnHashes(lookup.key, lookup.versionHashes);
                    }
                    CompleteStep(job);
                });
            }).detach();
        }

        void AddOwnedDlc(std::shared_ptr<ExportJob> job, GlobalNamespace::BeatmapLevelsModel* levelsModel) {
            auto dlcRepository = levelsModel->dlcBeatmapLevelsRepository;
            if (!dlcRepository || !dlcRepository->_beatmapLevelPacks)
                return;
            auto entitlements = levelsModel->entitlements;
            if (!entitlements) {
                job->dlcCheckFailed = true;
                return;
            }
            for (auto pack : dlcRepository->_beatmapLevelPacks) {
                if (!pack)
                    continue;
                SafePtr<System::Threading::Tasks::Task_1<GlobalNamespace::EntitlementStatus>> task;
                try {
                    task.emplace(entitlements->GetPackEntitlementStatusAsync(pack->packID, System::Threading::CancellationToken::get_None()));
                } catch (std::exception const& error) {
                    logger.warn("Pack entitlement check failed for {}: {}", static_cast<std::string>(pack->packID), error.what());
                    job->dlcCheckFailed = true;
                    continue;
                }
                job->pendingSteps++;
                SafePtr<GlobalNamespace::BeatmapLevelPack> packReference(pack);
                BSML::MainThreadScheduler::ScheduleUntil(
                    [task] { return !task || task->IsCompleted; },
                    [job, task, packReference]() mutable {
                        try {
                            bool owned = task && !task->IsFaulted && task->Result == GlobalNamespace::EntitlementStatus::Owned;
                            if (owned && packReference)
                                AddOfficialPack(*job, packReference.ptr());
                        } catch (std::exception const& error) {
                            logger.warn("Pack entitlement result unreadable: {}", error.what());
                            job->dlcCheckFailed = true;
                        }
                        CompleteStep(job);
                    }
                );
            }
        }
    }

    bool IsExporting() {
        return exporting;
    }

    std::string const& GetStatusText() {
        return statusText;
    }

    void Export() {
        if (exporting)
            return;
        if (!Hooks::IsMainMenuReady() || !SongCore::API::Loading::AreSongsLoaded() || SongCore::API::Loading::AreSongsRefreshing()) {
            SetStatus("Wait for songs to finish loading, then export again");
            return;
        }
        exporting = true;
        SetStatus("Exporting song list...");

        auto job = std::make_shared<ExportJob>();
        // held until every lookup has been queued
        job->pendingSteps = 1;
        try {
            AddCustomSongs(*job);
            if (auto levelsModel = GameLevels::GetBeatmapLevelsModel()) {
                if (auto ostRepository = levelsModel->ostAndExtrasBeatmapLevelsRepository; ostRepository && ostRepository->_beatmapLevelPacks) {
                    for (auto pack : ostRepository->_beatmapLevelPacks)
                        AddOfficialPack(*job, pack);
                }
                AddOwnedDlc(job, levelsModel);
            } else {
                job->dlcCheckFailed = true;
            }
            ResolveUnknownKeys(job);
        } catch (std::exception const& error) {
            logger.error("Song list export failed: {}", error.what());
            exporting = false;
            SetStatus("<color=#FF6B6B>Song list export failed</color>");
            return;
        }
        CompleteStep(job);
    }
}
