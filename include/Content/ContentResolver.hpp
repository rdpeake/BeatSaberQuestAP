#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "archipelago/BeatSaverKeyCache.hpp"
#include "archipelago/LevelIdentity.hpp"
#include "archipelago/SlotData.hpp"

namespace BeatSaberQuestAP {

    struct MissingDlcPack {
        std::string packId;
        std::string packName;
        int requiredDifficulties = 0;
    };

    struct ContentReport {
        bool checking = false;
        std::string progressText;
        int requiredMapCount = 0;
        int playableMapCount = 0;
        std::vector<std::string> downloadableKeys;
        std::vector<std::string> unavailableDescriptions;
        std::vector<MissingDlcPack> missingPacks;
        bool downloading = false;
        int downloadsFinished = 0;
        int downloadsTotal = 0;

        bool HasProblems() const { return !downloadableKeys.empty() || !unavailableDescriptions.empty() || !missingPacks.empty(); }
    };

    // works out which maps a seed needs, which are missing, and fetches what it can; main thread only
    class ContentResolver {
    public:
        static ContentResolver& Instance();

        void Begin(Archipelago::SlotData const& slotData);
        void Reset();

        std::optional<std::string> MapIdentifierForLevelId(std::string_view levelId) const;
        ContentReport const& GetReport() const;

        void DownloadMissingSongs();
        void OpenStoreForPack(std::string const& packId);

        // re-checks after SongCore finishes loading songs
        void HandleSongsLoaded();

        // retries BeatSaver lookups that failed, typically because the headset was offline
        void RetryFailedLookups();

        std::optional<std::string> BeatSaverKeyForHash(std::string const& hash) const;
        std::vector<std::string> HashesForBeatSaverKey(std::string const& beatSaverKey) const;

        // records hashes known from elsewhere, such as an imported playlist
        void LearnHashes(std::string const& beatSaverKey, std::vector<std::string> const& hashes);

    private:
        ContentResolver();
        void ResolveNextUnknownKey();
        void WaitForSongsThenEvaluate();
        void Evaluate();
        void EvaluateOfficialEntitlements();
        void DownloadNextSong();
        void FinishDownloads();
        void Publish();

        Archipelago::BeatSaverKeyCache keyCache;
        std::vector<Archipelago::ParsedIdent> requiredIdents;
        std::vector<std::string> keysToResolve;
        std::set<std::string> keysUnknownToBeatSaver;
        std::set<std::string> keysAlreadyRedownloaded;
        std::vector<std::string> downloadQueue;
        ContentReport report;
        bool active = false;
        bool resolvingKeys = false;
        bool waitingForSongs = false;
        int generation = 0;
    };
}
