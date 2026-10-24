#include "Content/ContentResolver.hpp"
#include "Content/GameLevels.hpp"

#include <algorithm>
#include <iterator>

#include <thread>

#include "beatsaverplusplus/shared/BeatSaver.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/BeatmapLevelPack.hpp"
#include "GlobalNamespace/BeatmapLevelsModel.hpp"
#include "GlobalNamespace/EntitlementStatus.hpp"
#include "GlobalNamespace/IAdditionalContentModel.hpp"
#include "GlobalNamespace/IEntitlementModel.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/OpenProductStoreResult.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "ModPaths.hpp"
#include "Session.hpp"
#include "System/Collections/Generic/IEnumerable_1.hpp"
#include "System/Collections/Generic/IEnumerator_1.hpp"
#include "System/Collections/IEnumerator.hpp"
#include "System/Threading/CancellationToken.hpp"
#include "System/Threading/Tasks/Task_1.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "songcore/shared/SongCore.hpp"

namespace BeatSaberQuestAP {

    namespace {
        std::string DescribeAvailableDifficulties(GlobalNamespace::BeatmapLevel* level) {
            std::string available;
            for (auto const& difficulty : GameLevels::ListDifficulties(level))
                available += (available.empty() ? "" : ", ") + Archipelago::DescribeDifficulty(difficulty);
            return available.empty() ? "none found" : available;
        }

        bool LevelHasDifficulty(GlobalNamespace::BeatmapLevel* level, Archipelago::ParsedIdent const& ident) {
            auto difficulties = GameLevels::ListDifficulties(level);
            // nothing listed means the lookup failed, not that the map is empty
            if (difficulties.empty())
                return true;
            return std::any_of(difficulties.begin(), difficulties.end(), [&](auto const& difficulty) {
                return difficulty.difficulty == ident.difficulty && Archipelago::EqualsIgnoreCase(difficulty.characteristic, ident.characteristic);
            });
        }

        std::string MissingDifficultyMessage(GlobalNamespace::BeatmapLevel* level, Archipelago::ParsedIdent const& ident) {
            return static_cast<std::string>(level->songName) + " has no " + Archipelago::DescribeDifficulty({ident.characteristic, ident.difficulty})
                + " (has " + DescribeAvailableDifficulties(level) + ")";
        }

        // BeatSaver requests block, so they run off the main thread
        void RunInBackground(std::function<void()> work) {
            std::thread(std::move(work)).detach();
        }

        std::vector<std::string> LookUpVersionHashes(std::string const& beatSaverKey) {
            std::vector<std::string> hashes;
            auto response = BeatSaver::API::GetBeatmapByKey(beatSaverKey);
            if (!response.IsSuccessful() || !response.responseData)
                return hashes;
            for (auto const& version : response.responseData->Versions)
                hashes.push_back(version.Hash);
            return hashes;
        }
    }

    ContentResolver& ContentResolver::Instance() {
        static ContentResolver instance;
        return instance;
    }

    ContentResolver::ContentResolver() : keyCache(ModPaths::BeatSaverKeyCacheFile()) {
        keyCache.Load();
    }

    void ContentResolver::Reset() {
        generation++;
        active = false;
        resolvingKeys = false;
        waitingForSongs = false;
        requiredIdents.clear();
        keysToResolve.clear();
        keysUnknownToBeatSaver.clear();
        keysAlreadyRedownloaded.clear();
        downloadQueue.clear();
        report = {};
    }

    void ContentResolver::Begin(Archipelago::SlotData const& slotData) {
        Reset();
        active = true;

        std::set<std::string> uniqueKeys;
        for (auto const& [node, ident] : slotData.nodeToIdent) {
            auto parsed = Archipelago::ParseIdent(ident);
            if (!parsed) {
                report.unavailableDescriptions.push_back("Unrecognised song entry " + ident);
                continue;
            }
            if (!parsed->isOfficial && !keyCache.Contains(parsed->beatSaverKey) && uniqueKeys.insert(parsed->beatSaverKey).second)
                keysToResolve.push_back(parsed->beatSaverKey);
            requiredIdents.push_back(std::move(*parsed));
        }

        report.checking = true;
        if (!keysToResolve.empty()) {
            report.progressText = "Looking up " + std::to_string(keysToResolve.size()) + " song(s) on BeatSaver...";
            Publish();
            resolvingKeys = true;
            ResolveNextUnknownKey();
            return;
        }
        Evaluate();
    }

    void ContentResolver::RetryFailedLookups() {
        if (!active || resolvingKeys || report.downloading || keysUnknownToBeatSaver.empty())
            return;
        keysToResolve.assign(keysUnknownToBeatSaver.begin(), keysUnknownToBeatSaver.end());
        keysUnknownToBeatSaver.clear();
        report.checking = true;
        report.progressText = "Looking up " + std::to_string(keysToResolve.size()) + " song(s) on BeatSaver...";
        Publish();
        resolvingKeys = true;
        ResolveNextUnknownKey();
    }

    void ContentResolver::LearnHashes(std::string const& beatSaverKey, std::vector<std::string> const& hashes) {
        if (beatSaverKey.empty() || hashes.empty())
            return;
        keyCache.Store(beatSaverKey, hashes);
        keyCache.Save();
    }

    std::optional<std::string> ContentResolver::BeatSaverKeyForHash(std::string const& hash) const {
        return keyCache.KeyForHash(hash);
    }

    std::vector<std::string> ContentResolver::HashesForBeatSaverKey(std::string const& beatSaverKey) const {
        return keyCache.HashesForKey(beatSaverKey);
    }

    std::optional<std::string> ContentResolver::MapIdentifierForLevelId(std::string_view levelId) const {
        if (!Archipelago::IsCustomLevelId(levelId))
            return Archipelago::OfficialMapIdentifier(levelId);
        return keyCache.KeyForHash(Archipelago::HashFromCustomLevelId(levelId));
    }

    ContentReport const& ContentResolver::GetReport() const {
        return report;
    }

    void ContentResolver::ResolveNextUnknownKey() {
        if (keysToResolve.empty()) {
            resolvingKeys = false;
            keyCache.Save();
            Evaluate();
            return;
        }

        std::string key = keysToResolve.back();
        keysToResolve.pop_back();
        int requestGeneration = generation;
        RunInBackground([this, key, requestGeneration] {
            auto hashes = LookUpVersionHashes(key);
            BSML::MainThreadScheduler::Schedule([this, key, requestGeneration, hashes = std::move(hashes)] {
                if (requestGeneration != generation)
                    return;
                if (hashes.empty()) {
                    logger.warn("BeatSaver lookup failed for {}", key);
                    keysUnknownToBeatSaver.insert(key);
                } else {
                    keyCache.Store(key, hashes);
                }
                ResolveNextUnknownKey();
            });
        });
    }

    void ContentResolver::HandleSongsLoaded() {
        if (!active || report.downloading)
            return;
        if (!waitingForSongs && !report.checking)
            Evaluate();
    }

    // SongCore can still report a refresh while its loaded event runs, so poll rather than rely on the event
    void ContentResolver::WaitForSongsThenEvaluate() {
        if (waitingForSongs)
            return;
        waitingForSongs = true;
        int waitGeneration = generation;
        BSML::MainThreadScheduler::ScheduleUntil(
            [] { return Hooks::IsMainMenuReady() && SongCore::API::Loading::AreSongsLoaded() && !SongCore::API::Loading::AreSongsRefreshing(); },
            [this, waitGeneration] {
                if (waitGeneration != generation || !waitingForSongs)
                    return;
                waitingForSongs = false;
                Evaluate();
            }
        );
    }

    void ContentResolver::Evaluate() {
        if (!active)
            return;
        if (!Hooks::IsMainMenuReady() || !SongCore::API::Loading::AreSongsLoaded() || SongCore::API::Loading::AreSongsRefreshing()) {
            report.checking = true;
            report.progressText = "Waiting for songs to finish loading...";
            Publish();
            WaitForSongsThenEvaluate();
            return;
        }
        waitingForSongs = false;

        auto levelsModel = GameLevels::GetBeatmapLevelsModel();
        std::set<std::string> requiredMaps;
        std::set<std::string> unplayableMaps;
        std::set<std::string> downloadableKeys;
        std::vector<std::string> unavailable;
        std::vector<std::string> officialLevelIds;

        for (auto const& ident : requiredIdents) {
            requiredMaps.insert(ident.mapIdentifier);

            if (ident.isOfficial) {
                auto level = GameLevels::FindOfficialLevel(ident.officialLevelId);
                if (!level) {
                    unplayableMaps.insert(ident.mapIdentifier);
                    unavailable.push_back("Official song '" + ident.officialLevelId + "' is not in this game version");
                    continue;
                }
                if (!LevelHasDifficulty(level, ident)) {
                    unavailable.push_back(MissingDifficultyMessage(level, ident));
                    continue;
                }
                if (std::find(officialLevelIds.begin(), officialLevelIds.end(), static_cast<std::string>(level->levelID)) == officialLevelIds.end())
                    officialLevelIds.push_back(level->levelID);
                continue;
            }

            if (keysUnknownToBeatSaver.contains(ident.beatSaverKey)) {
                unplayableMaps.insert(ident.mapIdentifier);
                unavailable.push_back("BeatSaver map " + ident.beatSaverKey + " could not be looked up; it will be retried when online");
                continue;
            }
            auto level = GameLevels::FindInstalledCustomLevel(keyCache.HashesForKey(ident.beatSaverKey));
            if (!level) {
                unplayableMaps.insert(ident.mapIdentifier);
                downloadableKeys.insert(ident.beatSaverKey);
                continue;
            }
            if (!LevelHasDifficulty(level, ident)) {
                if (keysAlreadyRedownloaded.contains(ident.beatSaverKey))
                    unavailable.push_back(MissingDifficultyMessage(level, ident));
                else
                    downloadableKeys.insert(ident.beatSaverKey);
            }
        }

        std::sort(unavailable.begin(), unavailable.end());
        unavailable.erase(std::unique(unavailable.begin(), unavailable.end()), unavailable.end());

        report.requiredMapCount = static_cast<int>(requiredMaps.size());
        report.playableMapCount = static_cast<int>(requiredMaps.size() - unplayableMaps.size());
        report.downloadableKeys.assign(downloadableKeys.begin(), downloadableKeys.end());
        report.unavailableDescriptions = std::move(unavailable);
        report.missingPacks.clear();

        if (officialLevelIds.empty() || !levelsModel) {
            report.checking = false;
            report.progressText.clear();
            Publish();
            return;
        }

        auto entitlements = levelsModel->entitlements;
        if (!entitlements) {
            logger.warn("Entitlement model unavailable; skipping the DLC ownership check");
            report.checking = false;
            report.progressText.clear();
            Publish();
            return;
        }

        report.progressText = "Checking DLC ownership...";
        Publish();

        auto pendingChecks = std::make_shared<int>(static_cast<int>(officialLevelIds.size()));
        auto packsByLevel = std::make_shared<std::map<std::string, MissingDlcPack>>();
        int checkGeneration = generation;
        auto finishOneCheck = [this, pendingChecks, packsByLevel, checkGeneration] {
            if (checkGeneration != generation || --*pendingChecks > 0)
                return;
            for (auto& [packId, missingPack] : *packsByLevel)
                report.missingPacks.push_back(missingPack);
            report.checking = false;
            report.progressText.clear();
            Publish();
        };

        for (auto const& levelId : officialLevelIds) {
            // a game exception here must not take the game down with it
            SafePtr<System::Threading::Tasks::Task_1<GlobalNamespace::EntitlementStatus>> task;
            try {
                task.emplace(entitlements->GetLevelEntitlementStatusAsync(levelId, System::Threading::CancellationToken::get_None()));
            } catch (std::exception const& error) {
                logger.warn("Entitlement check failed for {}: {}", levelId, error.what());
                finishOneCheck();
                continue;
            }
            BSML::MainThreadScheduler::ScheduleUntil(
                [task] { return !task || task->IsCompleted; },
                [this, task, levelId, levelsModel, packsByLevel, checkGeneration, finishOneCheck] {
                    if (checkGeneration != generation)
                        return;
                    try {
                        bool notOwned = task && !task->IsFaulted && task->Result == GlobalNamespace::EntitlementStatus::NotOwned;
                        if (notOwned) {
                            auto pack = levelsModel->GetLevelPackForLevelId(levelId);
                            std::string packId = pack ? static_cast<std::string>(pack->packID) : levelId;
                            auto& missingPack = (*packsByLevel)[packId];
                            missingPack.packId = packId;
                            missingPack.packName = pack ? static_cast<std::string>(pack->packName) : levelId;
                            missingPack.requiredDifficulties++;
                        }
                    } catch (std::exception const& error) {
                        logger.warn("Entitlement result unreadable for {}: {}", levelId, error.what());
                    }
                    finishOneCheck();
                }
            );
        }
    }

    void ContentResolver::DownloadMissingSongs() {
        if (!active || report.downloading || report.downloadableKeys.empty())
            return;
        downloadQueue = report.downloadableKeys;
        report.downloading = true;
        report.downloadsFinished = 0;
        report.downloadsTotal = static_cast<int>(downloadQueue.size());
        Publish();
        DownloadNextSong();
    }

    void ContentResolver::DownloadNextSong() {
        if (downloadQueue.empty()) {
            FinishDownloads();
            return;
        }

        std::string key = downloadQueue.back();
        downloadQueue.pop_back();
        keysAlreadyRedownloaded.insert(key);
        int requestGeneration = generation;

        RunInBackground([this, key, requestGeneration] {
            std::vector<std::string> hashes;
            bool succeeded = false;
            auto response = BeatSaver::API::GetBeatmapByKey(key);
            if (response.IsSuccessful() && response.responseData && !response.responseData->Versions.empty()) {
                auto const& beatmap = *response.responseData;
                for (auto const& version : beatmap.Versions)
                    hashes.push_back(version.Hash);
                succeeded = beatmap.Versions.front().DownloadBeatmap(beatmap, nullptr).has_value();
            }
            BSML::MainThreadScheduler::Schedule([this, key, requestGeneration, succeeded, hashes = std::move(hashes)] {
                if (requestGeneration != generation)
                    return;
                if (!hashes.empty())
                    keyCache.Store(key, hashes);
                if (!succeeded)
                    logger.warn("Download failed for {}", key);
                report.downloadsFinished++;
                Publish();
                DownloadNextSong();
            });
        });
    }

    void ContentResolver::FinishDownloads() {
        keyCache.Save();
        report.downloading = false;
        report.checking = true;
        report.progressText = "Loading downloaded songs...";
        Publish();
        SongCore::API::Loading::RefreshSongs(false);
        WaitForSongsThenEvaluate();
    }

    void ContentResolver::OpenStoreForPack(std::string const& packId) {
        auto mainFlowCoordinator = BSML::Helpers::GetMainFlowCoordinator();
        if (!mainFlowCoordinator || !mainFlowCoordinator->_additionalContentModel) {
            logger.warn("Store is not available right now");
            return;
        }
        logger.info("Opening store page for pack {}", packId);
        mainFlowCoordinator->_additionalContentModel->OpenLevelPackProductStoreAsync(packId, System::Threading::CancellationToken::get_None());
    }

    void ContentResolver::Publish() {
        Session::Instance().NotifyChanged();
    }
}
