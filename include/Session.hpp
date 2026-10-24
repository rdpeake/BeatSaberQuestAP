#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "archipelago/ArchipelagoClient.hpp"
#include "archipelago/ProgressionRules.hpp"
#include "archipelago/SlotData.hpp"

namespace BeatSaberQuestAP {

    struct FinishedLevel {
        std::string levelId;
        std::string characteristic;
        int difficulty = 0;
        bool cleared = false;
        bool failed = false;
        bool usedModifiers = false;
        bool practice = false;
        std::string rankName;
    };

    // owns the Archipelago connection; every method must be called on the main thread
    class Session {
    public:
        static Session& Instance();

        void Connect(Archipelago::ConnectionSettings const& settings);
        void Disconnect();

        bool IsConnected() const;
        Archipelago::ConnectionState GetConnectionState() const;
        std::string const& GetStatusText() const;
        std::optional<Archipelago::SlotData> const& GetSlotData() const;
        Archipelago::Progression const& GetProgression() const;

        // nullopt when the level cannot be matched to an ident yet (unknown custom hash)
        std::optional<std::string> IdentFor(std::string_view levelId, std::string_view characteristic, int difficulty) const;
        bool IsIdentInSeed(std::string const& ident) const;

        void HandleLevelFinished(FinishedLevel const& finishedLevel);

        void AddChangeListener(std::function<void()> listener);
        void NotifyChanged();

    private:
        Session() = default;
        void EnsureClient();
        void HandleStateChanged(Archipelago::ConnectionState state, std::string const& detail);
        void HandleConnected(Archipelago::SlotData const& slotData);
        void HandleItemsChanged(std::vector<Archipelago::ReceivedItem> const& items);
        void HandleDeathLink(Archipelago::DeathLinkEvent const& deathLink);

        std::unique_ptr<Archipelago::ArchipelagoClient> client;
        Archipelago::ConnectionState connectionState = Archipelago::ConnectionState::Disconnected;
        std::string statusText = "Not connected";
        std::optional<Archipelago::SlotData> slotData;
        Archipelago::Progression progression;
        bool goalReported = false;
        std::vector<std::function<void()>> changeListeners;
    };
}
