#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "archipelago/SlotData.hpp"

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
        Failed,
    };

    struct ReceivedItem {
        int64_t itemId = 0;
        std::string itemName;
        int64_t locationId = 0;
        int sendingPlayer = 0;
    };

    struct DeathLinkEvent {
        std::string source;
        std::string cause;
    };

    // all callbacks run through the dispatcher so the caller decides which thread handles them
    struct ClientCallbacks {
        std::function<void(ConnectionState state, std::string const& detail)> onStateChanged;
        std::function<void(SlotData const& slotData)> onConnected;
        std::function<void(std::vector<ReceivedItem> const& allItems)> onItemsChanged;
        std::function<void(DeathLinkEvent const& deathLink)> onDeathLink;
        std::function<void(std::string const& message)> onServerMessage;
    };

    using Dispatcher = std::function<void(std::function<void()>)>;

    class ArchipelagoClient {
    public:
        ArchipelagoClient(ClientCallbacks callbacks, Dispatcher dispatcher, std::string certificateAuthorityFile);
        ~ArchipelagoClient();

        ArchipelagoClient(ArchipelagoClient const&) = delete;
        ArchipelagoClient& operator=(ArchipelagoClient const&) = delete;

        void Connect(ConnectionSettings const& settings);
        void Disconnect();

        void SendLocationChecks(std::vector<int64_t> const& locationIds);
        void SendGoalComplete();
        void SendDeathLink(std::string const& cause);

        ConnectionState GetState() const;
        std::string GetSlotAlias() const;

        // accepts host, host:port, ws://host:port or wss://host:port
        static std::vector<std::string> CandidateUrls(std::string const& host, int port);

    private:
        void OpenNextCandidate();
        void HandleSocketOpen();
        void HandleSocketClosed(std::string const& reason);
        void HandleSocketError(std::string const& reason);
        void HandlePacket(std::string const& packetText);
        void SendPackets(std::string const& packetArrayJson);
        void SetState(ConnectionState state, std::string const& detail);
        void DispatchCallback(std::function<void()> callback);

        ClientCallbacks callbacks;
        Dispatcher dispatcher;
        std::string certificateAuthorityFile;

        mutable std::recursive_mutex stateMutex;
        std::unique_ptr<ix::WebSocket> socket;
        ConnectionSettings settings;
        std::vector<std::string> candidateUrls;
        size_t candidateIndex = 0;
        bool socketOpened = false;
        bool userRequestedDisconnect = false;
        ConnectionState state = ConnectionState::Disconnected;

        std::map<int64_t, std::string> itemNamesById;
        std::vector<ReceivedItem> receivedItems;
        std::string slotAlias;
        int slotNumber = 0;
        int teamNumber = 0;
        bool deathLinkTagged = false;
        double lastDeathLinkSentTime = 0;
        std::shared_ptr<bool> lifetimeToken;
    };
}
