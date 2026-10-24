#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "archipelago/ArchipelagoClient.hpp"
#include "archipelago/ProgressionRules.hpp"
#include "archipelago/SavedSession.hpp"
#include "archipelago/SlotData.hpp"

namespace BeatSaberQuestAP {

    enum class LogLevel {
        // what a player needs during a normal game
        Player,
        // explanations useful while testing
        Debug,
    };

    struct LogMessage {
        LogLevel level = LogLevel::Player;
        std::string text;
    };

    struct CheckProgress {
        int completed = 0;
        int total = 0;
    };

    struct FinishedLevel {
        std::string levelId;
        std::string songName;
        std::string characteristic;
        int difficulty = 0;
        bool cleared = false;
        bool failed = false;
        bool usedModifiers = false;
        bool practice = false;
        std::string rankName;
    };

    // owns the Archipelago connection; every method must be called on the main thread.
    // Archipelago mode starts with the first successful join and lasts until the player leaves,
    // so song availability follows the last known state of the slot even while offline
    class Session {
    public:
        static Session& Instance();

        // re-enters Archipelago mode from the previous launch, if any
        void RestoreSavedSession();

        void Connect(Archipelago::ConnectionSettings const& settings);
        void ReconnectNow();
        void LeaveArchipelago();

        bool IsInArchipelagoMode() const;
        bool IsOnline() const;
        bool NeedsPasswordToReconnect() const;
        Archipelago::ConnectionState GetConnectionState() const;
        std::string const& GetStatusText() const;
        std::optional<Archipelago::SlotData> const& GetSlotData() const;
        Archipelago::Progression const& GetProgression() const;
        std::optional<Archipelago::SavedSession> const& GetActiveSession() const;
        size_t GetUnsentCheckCount() const;

        // nullopt when the level cannot be matched to an ident yet (unknown custom hash)
        std::optional<std::string> IdentFor(std::string_view levelId, std::string_view characteristic, int difficulty) const;
        bool IsIdentInSeed(std::string const& ident) const;
        // checks sent or confirmed out of all the level's checks; nullopt when the song is not in the seed
        std::optional<CheckProgress> GetCheckProgress(std::string_view levelId) const;
        // the seed's characteristic and difficulty pairs for this level, e.g. "Standard Expert+"; empty when the song is not in the seed
        std::vector<std::string> SeedDifficultiesFor(std::string_view levelId) const;

        void HandleLevelFinished(FinishedLevel const& finishedLevel);

        void AddChangeListener(std::function<void()> listener);
        void NotifyChanged();

        // shown in the Archipelago menu, newest last
        void AddLogMessage(std::string message, LogLevel level = LogLevel::Player);
        std::deque<LogMessage> const& GetLogMessages() const;

    private:
        Session() = default;
        void EnsureClient();
        bool IsSameSlot(Archipelago::ConnectionSettings const& settings) const;
        void EnterArchipelagoMode(Archipelago::SlotData const& slotData, std::string const& slotDataJson);
        void SaveActiveSession();
        void HandleStateChanged(Archipelago::ConnectionState state, std::string const& detail);
        void HandleConnected(Archipelago::SlotData const& slotData, std::string const& slotDataJson);
        void HandleItemsChanged(std::vector<Archipelago::ReceivedItem> const& items);
        void HandlePendingLocationsChanged(std::vector<int64_t> const& unconfirmedLocations);
        void HandleCheckedLocationsChanged(std::vector<int64_t> const& checkedLocations);
        void VerifyLocationLayout();
        std::string DescribeCheck(int64_t locationId) const;
        void HandleDeathLink(Archipelago::DeathLinkEvent const& deathLink);

        std::unique_ptr<Archipelago::ArchipelagoClient> client;
        Archipelago::ConnectionSettings currentSettings;
        Archipelago::ConnectionState connectionState = Archipelago::ConnectionState::Disconnected;
        std::string statusText = "Not connected";
        std::optional<Archipelago::SavedSession> activeSession;
        std::optional<Archipelago::SlotData> slotData;
        Archipelago::Progression progression;
        bool passwordNeeded = false;
        bool goalReported = false;
        std::vector<std::function<void()>> changeListeners;
        std::deque<LogMessage> logMessages;
        size_t knownItemCount = 0;
    };
}
