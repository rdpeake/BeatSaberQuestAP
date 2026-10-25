#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "archipelago/SlotData.hpp"
#include "rapidjson/document.h"

namespace ix {
    class WebSocket;
}

namespace Archipelago {

    inline constexpr std::string_view gameName = "Beat Saber";

    struct ConnectionSettings {
        std::string host;
        int port = 38281;
        std::string slotName;
        std::string password;
    };

    enum class ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        // the slot was joined before and the client is trying to get back to it
        Reconnecting,
        Failed,
    };

    struct ReceivedItem {
        int64_t itemId = 0;
        std::string itemName;
        int64_t locationId = 0;
        int sendingPlayer = 0;
    };

    struct ServerMessage {
        std::string type;
        std::string text;
        // item sends and hints only matter to the player when this slot found or receives the item
        bool involvesThisSlot = false;
    };

    struct DeathLinkEvent {
        std::string source;
        std::string cause;
    };

    // all callbacks run through the dispatcher so the caller decides which thread handles them
    struct ClientCallbacks {
        std::function<void(ConnectionState state, std::string const& detail)> onStateChanged;
        std::function<void(SlotData const& slotData, std::string const& slotDataJson)> onConnected;
        std::function<void(std::vector<ReceivedItem> const& allItems)> onItemsChanged;
        std::function<void(std::vector<int64_t> const& unconfirmedLocations)> onPendingLocationsChanged;
        std::function<void(std::vector<int64_t> const& checkedLocations)> onCheckedLocationsChanged;
        std::function<void(DeathLinkEvent const& deathLink)> onDeathLink;
        std::function<void(ServerMessage const& message)> onServerMessage;
    };

    using Dispatcher = std::function<void(std::function<void()>)>;

    class ArchipelagoClient {
    public:
        ArchipelagoClient(ClientCallbacks callbacks, Dispatcher dispatcher, std::string certificateAuthorityFile);
        ~ArchipelagoClient();

        ArchipelagoClient(ArchipelagoClient const&) = delete;
        ArchipelagoClient& operator=(ArchipelagoClient const&) = delete;

        // starts a new session; pending location checks from an earlier session are dropped
        void Connect(ConnectionSettings const& settings);
        // rejoins a slot joined before (for example in an earlier launch): keeps pending checks and retries until it gets back in
        void Resume(ConnectionSettings const& settings);
        // ends the session and stops reconnecting
        void Disconnect();
        // skips the remaining backoff when the session is waiting to reconnect
        void ReconnectNow();

        // location checks are kept until the server confirms them, surviving disconnects
        void SendLocationChecks(std::vector<int64_t> const& locationIds);
        void RestorePendingLocationChecks(std::vector<int64_t> const& locationIds);
        std::vector<int64_t> GetPendingLocationChecks() const;

        void SendGoalComplete();
        // dropped while offline, since a late death is meaningless to other players
        void SendDeathLink(std::string const& cause);

        ConnectionState GetState() const;
        std::string GetSlotAlias() const;
        std::string GetPlayerAlias(int slot) const;
        // the server's name for one of this slot's locations; nullopt until the data package has arrived
        std::optional<std::string> GetOwnLocationName(int64_t locationId) const;

        void SetReconnectDelays(std::vector<std::chrono::milliseconds> delays);

        // accepts host, host:port, ws://host:port or wss://host:port
        static std::vector<std::string> CandidateUrls(std::string const& host, int port);

    private:
        void StartConnecting(ConnectionSettings const& settings, bool resumingSession);
        void OpenSocket();
        void CloseSocket();
        void HandleSocketOpen();
        void HandleConnectionLost(std::string const& reason);
        void ScheduleReconnect(std::string const& reason);
        void CancelReconnectTimer();
        void BeginReconnectAttempt();
        void HandlePacket(std::string const& packetText);
        using NamesByGame = std::map<std::string, std::map<int64_t, std::string>>;
        std::optional<std::string> LookUpName(NamesByGame const& namesByGame, int slot, int64_t id) const;
        ServerMessage RenderPrintJson(rapidjson::Value const& packet) const;
        void SendPackets(std::string const& packetArrayJson);
        void FlushPendingLocationChecks();
        void SetState(ConnectionState state, std::string const& detail);
        void DispatchCallback(std::function<void()> callback);
        void NotifyPendingLocationsChanged();
        void NotifyCheckedLocationsChanged();

        ClientCallbacks callbacks;
        Dispatcher dispatcher;
        std::string certificateAuthorityFile;

        mutable std::recursive_mutex stateMutex;
        std::unique_ptr<ix::WebSocket> socket;
        ConnectionSettings settings;
        std::vector<std::string> candidateUrls;
        std::string workingUrl;
        size_t candidateIndex = 0;
        bool socketOpened = false;
        bool userRequestedDisconnect = false;
        bool sessionEstablished = false;
        ConnectionState state = ConnectionState::Disconnected;

        std::vector<std::chrono::milliseconds> reconnectDelays;
        size_t reconnectAttempt = 0;
        std::thread reconnectTimer;
        std::mutex reconnectTimerMutex;
        std::condition_variable reconnectTimerSignal;
        bool reconnectTimerCancelled = false;

        NamesByGame itemNamesByGame;
        NamesByGame locationNamesByGame;
        std::map<int, std::string> playerAliases;
        std::map<int, std::string> playerGames;
        std::vector<ReceivedItem> receivedItems;
        std::set<int64_t> pendingLocationChecks;
        std::set<int64_t> confirmedLocationChecks;
        bool goalPending = false;
        std::string slotAlias;
        int slotNumber = 0;
        int teamNumber = 0;
        bool deathLinkTagged = false;
        double lastDeathLinkSentTime = 0;
        std::shared_ptr<bool> lifetimeToken;
    };
}
