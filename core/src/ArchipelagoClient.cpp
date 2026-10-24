#include "archipelago/ArchipelagoClient.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <random>

#include "ixwebsocket/IXWebSocket.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Archipelago {

    namespace {
        constexpr int itemsHandlingAll = 0b111;
        constexpr int clientStatusGoal = 30;
        constexpr int handshakeTimeoutSeconds = 10;
        constexpr int pingIntervalSeconds = 15;
        constexpr char const* deathLinkTag = "DeathLink";

        using JsonWriter = rapidjson::Writer<rapidjson::StringBuffer>;

        std::vector<std::chrono::milliseconds> const defaultReconnectDelays = {
            std::chrono::seconds(2), std::chrono::seconds(5), std::chrono::seconds(10), std::chrono::seconds(20), std::chrono::seconds(30),
        };

        std::string RandomUuid() {
            std::random_device randomDevice;
            std::mt19937_64 generator(randomDevice());
            std::uniform_int_distribution<int> hexDigit(0, 15);
            constexpr char digits[] = "0123456789abcdef";
            std::string uuid;
            for (int i = 0; i < 32; i++)
                uuid += digits[hexDigit(generator)];
            return uuid;
        }

        double UnixTimeSeconds() {
            using namespace std::chrono;
            return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
        }

        void WriteString(JsonWriter& writer, std::string_view text) {
            writer.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
        }

        // wraps a single command object in the packet array the protocol expects
        template <typename WriteFields>
        std::string BuildPacket(char const* command, WriteFields writeFields) {
            rapidjson::StringBuffer buffer;
            JsonWriter writer(buffer);
            writer.StartArray();
            writer.StartObject();
            writer.Key("cmd");
            writer.String(command);
            writeFields(writer);
            writer.EndObject();
            writer.EndArray();
            return buffer.GetString();
        }

        std::string SerializeJson(rapidjson::Value const& value) {
            rapidjson::StringBuffer buffer;
            JsonWriter writer(buffer);
            value.Accept(writer);
            return buffer.GetString();
        }

        std::string DescribeRefusal(rapidjson::Value const& packet) {
            std::string description = "Connection refused";
            auto errors = packet.FindMember("errors");
            if (errors == packet.MemberEnd() || !errors->value.IsArray())
                return description;
            for (auto const& error : errors->value.GetArray()) {
                std::string_view code = error.IsString() ? error.GetString() : "";
                if (code == "InvalidSlot")
                    description += ": unknown slot name";
                else if (code == "InvalidGame")
                    description += ": slot is not a Beat Saber slot";
                else if (code == "InvalidPassword")
                    description += ": wrong password";
                else if (code == "IncompatibleVersion")
                    description += ": server version is incompatible";
                else if (!code.empty())
                    description += ": " + std::string(code);
            }
            return description;
        }

        std::vector<int64_t> ReadLocationList(rapidjson::Value const& packet, char const* key) {
            std::vector<int64_t> locations;
            auto member = packet.FindMember(key);
            if (member == packet.MemberEnd() || !member->value.IsArray())
                return locations;
            for (auto const& location : member->value.GetArray()) {
                if (location.IsInt64())
                    locations.push_back(location.GetInt64());
            }
            return locations;
        }

        std::string DescribeDelay(std::chrono::milliseconds delay) {
            auto seconds = std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::seconds>(delay + std::chrono::milliseconds(500)).count());
            return std::to_string(seconds) + "s";
        }
    }

    ArchipelagoClient::ArchipelagoClient(ClientCallbacks callbacks, Dispatcher dispatcher, std::string certificateAuthorityFile)
        : callbacks(std::move(callbacks)),
          dispatcher(std::move(dispatcher)),
          certificateAuthorityFile(std::move(certificateAuthorityFile)),
          reconnectDelays(defaultReconnectDelays),
          lifetimeToken(std::make_shared<bool>(true)) {}

    ArchipelagoClient::~ArchipelagoClient() {
        lifetimeToken.reset();
        CancelReconnectTimer();
        CloseSocket();
    }

    std::vector<std::string> ArchipelagoClient::CandidateUrls(std::string const& host, int port) {
        std::string address = host;
        while (!address.empty() && (address.back() == '/' || address.back() == ' '))
            address.pop_back();
        while (!address.empty() && address.front() == ' ')
            address.erase(address.begin());

        std::string scheme;
        if (auto schemeEnd = address.find("://"); schemeEnd != std::string::npos) {
            scheme = address.substr(0, schemeEnd);
            address = address.substr(schemeEnd + 3);
        }

        bool hasPort = address.find(':') != std::string::npos;
        std::string hostAndPort = hasPort ? address : address + ":" + std::to_string(port);

        if (scheme == "ws" || scheme == "wss")
            return {scheme + "://" + hostAndPort};

        // self-hosted servers on a LAN rarely have TLS; trying it first makes them log a bad request
        std::string hostOnly = address.substr(0, address.find(':'));
        bool isNumericAddress = !hostOnly.empty() && hostOnly.find_first_not_of("0123456789.") == std::string::npos;
        bool isLocalName = hostOnly == "localhost" || (hostOnly.size() > 6 && hostOnly.ends_with(".local"));
        if (isNumericAddress || isLocalName)
            return {"ws://" + hostAndPort, "wss://" + hostAndPort};
        return {"wss://" + hostAndPort, "ws://" + hostAndPort};
    }

    void ArchipelagoClient::SetReconnectDelays(std::vector<std::chrono::milliseconds> delays) {
        std::lock_guard lock(stateMutex);
        reconnectDelays = delays.empty() ? defaultReconnectDelays : std::move(delays);
    }

    void ArchipelagoClient::Connect(ConnectionSettings const& newSettings) {
        StartConnecting(newSettings, false);
    }

    void ArchipelagoClient::Resume(ConnectionSettings const& newSettings) {
        StartConnecting(newSettings, true);
    }

    void ArchipelagoClient::StartConnecting(ConnectionSettings const& newSettings, bool resumingSession) {
        CancelReconnectTimer();
        CloseSocket();
        {
            std::lock_guard lock(stateMutex);
            settings = newSettings;
            candidateUrls = CandidateUrls(settings.host, settings.port);
            candidateIndex = 0;
            userRequestedDisconnect = false;
            sessionEstablished = resumingSession;
            reconnectAttempt = 0;
            receivedItems.clear();
            if (!resumingSession) {
                pendingLocationChecks.clear();
                confirmedLocationChecks.clear();
                goalPending = false;
            }
            deathLinkTagged = false;
            slotAlias.clear();
        }
        SetState(resumingSession ? ConnectionState::Reconnecting : ConnectionState::Connecting, "Connecting to " + newSettings.host);
        OpenSocket();
    }

    void ArchipelagoClient::Disconnect() {
        {
            std::lock_guard lock(stateMutex);
            userRequestedDisconnect = true;
            sessionEstablished = false;
        }
        CancelReconnectTimer();
        CloseSocket();
        if (GetState() != ConnectionState::Disconnected)
            SetState(ConnectionState::Disconnected, "Disconnected");
    }

    void ArchipelagoClient::ReconnectNow() {
        {
            std::lock_guard lock(stateMutex);
            bool waitingToRetry = state == ConnectionState::Reconnecting && !socket;
            bool refusedAfterJoining = state == ConnectionState::Failed && sessionEstablished;
            if (!waitingToRetry && !refusedAfterJoining)
                return;
            state = ConnectionState::Reconnecting;
        }
        CancelReconnectTimer();
        BeginReconnectAttempt();
    }

    void ArchipelagoClient::CloseSocket() {
        std::unique_ptr<ix::WebSocket> closingSocket;
        {
            std::lock_guard lock(stateMutex);
            closingSocket = std::move(socket);
            deathLinkTagged = false;
        }
        if (closingSocket)
            closingSocket->stop();
    }

    void ArchipelagoClient::OpenSocket() {
        std::lock_guard lock(stateMutex);
        if (candidateIndex >= candidateUrls.size())
            return;

        socketOpened = false;
        socket = std::make_unique<ix::WebSocket>();
        socket->setUrl(candidateUrls[candidateIndex]);
        socket->disableAutomaticReconnection();
        socket->setHandshakeTimeout(handshakeTimeoutSeconds);
        socket->setPingInterval(pingIntervalSeconds);
        // the Archipelago server warns uncompressed clients they may stop working
        socket->enablePerMessageDeflate();

        ix::SocketTLSOptions tlsOptions;
        tlsOptions.caFile = certificateAuthorityFile.empty() ? "NONE" : certificateAuthorityFile;
        socket->setTLSOptions(tlsOptions);

        std::weak_ptr<bool> alive = lifetimeToken;
        ix::WebSocket* socketIdentity = socket.get();
        socket->setOnMessageCallback([this, alive, socketIdentity](ix::WebSocketMessagePtr const& message) {
            std::string text = message->str;
            std::string reason = message->type == ix::WebSocketMessageType::Error ? message->errorInfo.reason : message->closeInfo.reason;
            auto type = message->type;
            DispatchCallback([this, alive, socketIdentity, type, text = std::move(text), reason = std::move(reason)] {
                if (alive.expired())
                    return;
                {
                    std::lock_guard lock(stateMutex);
                    if (socket.get() != socketIdentity)
                        return;
                }
                switch (type) {
                    case ix::WebSocketMessageType::Open:
                        HandleSocketOpen();
                        break;
                    case ix::WebSocketMessageType::Message:
                        HandlePacket(text);
                        break;
                    case ix::WebSocketMessageType::Error:
                    case ix::WebSocketMessageType::Close:
                        HandleConnectionLost(reason);
                        break;
                    default:
                        break;
                }
            });
        });
        socket->start();
    }

    void ArchipelagoClient::DispatchCallback(std::function<void()> callback) {
        if (dispatcher)
            dispatcher(std::move(callback));
        else
            callback();
    }

    void ArchipelagoClient::HandleSocketOpen() {
        std::lock_guard lock(stateMutex);
        socketOpened = true;
    }

    void ArchipelagoClient::HandleConnectionLost(std::string const& reason) {
        bool tryAnotherCandidate = false;
        bool reconnect = false;
        {
            std::lock_guard lock(stateMutex);
            if (userRequestedDisconnect)
                return;
            tryAnotherCandidate = !socketOpened && candidateIndex + 1 < candidateUrls.size();
            if (tryAnotherCandidate)
                candidateIndex++;
            reconnect = sessionEstablished;
        }
        CloseSocket();

        if (tryAnotherCandidate) {
            OpenSocket();
            return;
        }
        if (reconnect) {
            ScheduleReconnect(reason);
            return;
        }
        SetState(ConnectionState::Failed, reason.empty() ? "Could not reach the server" : reason);
    }

    void ArchipelagoClient::ScheduleReconnect(std::string const& reason) {
        std::chrono::milliseconds delay;
        {
            std::lock_guard lock(stateMutex);
            delay = reconnectDelays[std::min(reconnectAttempt, reconnectDelays.size() - 1)];
            reconnectAttempt++;
        }
        std::string detail = "Connection lost";
        if (!reason.empty())
            detail += " (" + reason + ")";
        SetState(ConnectionState::Reconnecting, detail + ", retrying in " + DescribeDelay(delay));

        CancelReconnectTimer();
        std::weak_ptr<bool> alive = lifetimeToken;
        std::lock_guard timerLock(reconnectTimerMutex);
        reconnectTimerCancelled = false;
        reconnectTimer = std::thread([this, alive, delay] {
            {
                std::unique_lock waitLock(reconnectTimerMutex);
                if (reconnectTimerSignal.wait_for(waitLock, delay, [this] { return reconnectTimerCancelled; }))
                    return;
            }
            DispatchCallback([this, alive] {
                if (!alive.expired())
                    BeginReconnectAttempt();
            });
        });
    }

    void ArchipelagoClient::CancelReconnectTimer() {
        std::thread finishedTimer;
        {
            std::lock_guard timerLock(reconnectTimerMutex);
            reconnectTimerCancelled = true;
            finishedTimer = std::move(reconnectTimer);
        }
        reconnectTimerSignal.notify_all();
        if (!finishedTimer.joinable())
            return;
        // without a dispatcher the timer thread itself can end up here
        if (finishedTimer.get_id() == std::this_thread::get_id())
            finishedTimer.detach();
        else
            finishedTimer.join();
    }

    void ArchipelagoClient::BeginReconnectAttempt() {
        std::string host;
        {
            std::lock_guard lock(stateMutex);
            if (userRequestedDisconnect || state != ConnectionState::Reconnecting || socket)
                return;
            // retry with whichever scheme worked last
            auto working = std::find(candidateUrls.begin(), candidateUrls.end(), workingUrl);
            if (working != candidateUrls.end())
                std::rotate(candidateUrls.begin(), working, working + 1);
            candidateIndex = 0;
            host = settings.host;
        }
        SetState(ConnectionState::Reconnecting, "Reconnecting to " + host + "...");
        OpenSocket();
    }

    void ArchipelagoClient::SendPackets(std::string const& packetArrayJson) {
        std::lock_guard lock(stateMutex);
        if (socket)
            socket->sendText(packetArrayJson);
    }

    void ArchipelagoClient::SetState(ConnectionState newState, std::string const& detail) {
        {
            std::lock_guard lock(stateMutex);
            state = newState;
        }
        if (callbacks.onStateChanged)
            callbacks.onStateChanged(newState, detail);
    }

    ConnectionState ArchipelagoClient::GetState() const {
        std::lock_guard lock(stateMutex);
        return state;
    }

    std::string ArchipelagoClient::GetSlotAlias() const {
        std::lock_guard lock(stateMutex);
        return slotAlias.empty() ? settings.slotName : slotAlias;
    }

    std::string ArchipelagoClient::GetPlayerAlias(int slot) const {
        std::lock_guard lock(stateMutex);
        auto alias = playerAliases.find(slot);
        if (alias != playerAliases.end())
            return alias->second;
        return slot == 0 ? "Server" : "Player " + std::to_string(slot);
    }

    std::optional<std::string> ArchipelagoClient::LookUpName(NamesByGame const& namesByGame, int slot, int64_t id) const {
        std::lock_guard lock(stateMutex);
        auto game = playerGames.find(slot);
        if (game == playerGames.end())
            return std::nullopt;
        auto names = namesByGame.find(game->second);
        if (names == namesByGame.end())
            return std::nullopt;
        auto name = names->second.find(id);
        if (name == names->second.end())
            return std::nullopt;
        return name->second;
    }

    std::optional<std::string> ArchipelagoClient::GetOwnLocationName(int64_t locationId) const {
        std::lock_guard lock(stateMutex);
        return LookUpName(locationNamesByGame, slotNumber, locationId);
    }

    ServerMessage ArchipelagoClient::RenderPrintJson(rapidjson::Value const& packet) const {
        ServerMessage message;
        if (packet.HasMember("type") && packet["type"].IsString())
            message.type = packet["type"].GetString();

        std::lock_guard lock(stateMutex);
        auto readInt = [](rapidjson::Value const& value, char const* key) -> std::optional<int64_t> {
            if (!value.HasMember(key))
                return std::nullopt;
            auto const& member = value[key];
            if (member.IsInt64())
                return member.GetInt64();
            if (member.IsString())
                return std::atoll(member.GetString());
            return std::nullopt;
        };

        if (packet.HasMember("data") && packet["data"].IsArray()) {
            for (auto const& part : packet["data"].GetArray()) {
                if (!part.IsObject() || !part.HasMember("text") || !part["text"].IsString())
                    continue;
                std::string text = part["text"].GetString();
                std::string partType = part.HasMember("type") && part["type"].IsString() ? part["type"].GetString() : "text";
                int owner = static_cast<int>(readInt(part, "player").value_or(0));
                if (partType == "player_id") {
                    auto alias = playerAliases.find(std::atoi(text.c_str()));
                    if (alias != playerAliases.end())
                        text = alias->second;
                } else if (partType == "item_id") {
                    if (auto name = LookUpName(itemNamesByGame, owner, std::atoll(text.c_str())))
                        text = *name;
                } else if (partType == "location_id") {
                    if (auto name = LookUpName(locationNamesByGame, owner, std::atoll(text.c_str())))
                        text = *name;
                }
                message.text += text;
            }
        }

        bool receivesItem = readInt(packet, "receiving") == slotNumber;
        bool foundItem = packet.HasMember("item") && packet["item"].IsObject() && readInt(packet["item"], "player") == slotNumber;
        message.involvesThisSlot = receivesItem || foundItem;
        return message;
    }

    void ArchipelagoClient::NotifyPendingLocationsChanged() {
        if (callbacks.onPendingLocationsChanged)
            callbacks.onPendingLocationsChanged(GetPendingLocationChecks());
    }

    void ArchipelagoClient::NotifyCheckedLocationsChanged() {
        if (!callbacks.onCheckedLocationsChanged)
            return;
        std::vector<int64_t> checkedLocations;
        {
            std::lock_guard lock(stateMutex);
            checkedLocations.assign(confirmedLocationChecks.begin(), confirmedLocationChecks.end());
        }
        callbacks.onCheckedLocationsChanged(checkedLocations);
    }

    void ArchipelagoClient::HandlePacket(std::string const& packetText) {
        rapidjson::Document packets;
        packets.Parse(packetText.c_str(), packetText.size());
        if (packets.HasParseError() || !packets.IsArray())
            return;

        for (auto const& packet : packets.GetArray()) {
            if (!packet.IsObject() || !packet.HasMember("cmd") || !packet["cmd"].IsString())
                continue;
            std::string_view command = packet["cmd"].GetString();

            if (command == "RoomInfo") {
                // every game in the room, so item and location names in messages can be shown
                std::set<std::string> roomGames = {std::string(gameName)};
                if (packet.HasMember("games") && packet["games"].IsArray()) {
                    for (auto const& game : packet["games"].GetArray()) {
                        if (game.IsString())
                            roomGames.insert(game.GetString());
                    }
                }
                SendPackets(BuildPacket("GetDataPackage", [&](JsonWriter& writer) {
                    writer.Key("games");
                    writer.StartArray();
                    for (auto const& game : roomGames)
                        WriteString(writer, game);
                    writer.EndArray();
                }));
            } else if (command == "DataPackage") {
                ConnectionSettings connectSettings;
                {
                    std::lock_guard lock(stateMutex);
                    auto data = packet.FindMember("data");
                    if (data != packet.MemberEnd() && data->value.HasMember("games") && data->value["games"].IsObject()) {
                        for (auto const& game : data->value["games"].GetObject()) {
                            std::string name = game.name.GetString();
                            if (game.value.HasMember("item_name_to_id") && game.value["item_name_to_id"].IsObject()) {
                                for (auto const& entry : game.value["item_name_to_id"].GetObject()) {
                                    if (entry.value.IsInt64())
                                        itemNamesByGame[name][entry.value.GetInt64()] = entry.name.GetString();
                                }
                            }
                            if (game.value.HasMember("location_name_to_id") && game.value["location_name_to_id"].IsObject()) {
                                for (auto const& entry : game.value["location_name_to_id"].GetObject()) {
                                    if (entry.value.IsInt64())
                                        locationNamesByGame[name][entry.value.GetInt64()] = entry.name.GetString();
                                }
                            }
                        }
                    }
                    connectSettings = settings;
                }
                SendPackets(BuildPacket("Connect", [&](JsonWriter& writer) {
                    writer.Key("password");
                    WriteString(writer, connectSettings.password);
                    writer.Key("game");
                    WriteString(writer, gameName);
                    writer.Key("name");
                    WriteString(writer, connectSettings.slotName);
                    writer.Key("uuid");
                    WriteString(writer, RandomUuid());
                    writer.Key("version");
                    writer.StartObject();
                    writer.Key("major");
                    writer.Int(0);
                    writer.Key("minor");
                    writer.Int(6);
                    writer.Key("build");
                    writer.Int(2);
                    writer.Key("class");
                    writer.String("Version");
                    writer.EndObject();
                    writer.Key("items_handling");
                    writer.Int(itemsHandlingAll);
                    writer.Key("tags");
                    writer.StartArray();
                    writer.EndArray();
                    writer.Key("slot_data");
                    writer.Bool(true);
                }));
            } else if (command == "Connected") {
                SlotData slotData;
                std::string slotDataJson;
                try {
                    if (!packet.HasMember("slot_data"))
                        throw std::runtime_error("server sent no slot data");
                    slotData = ParseSlotData(packet["slot_data"]);
                    slotDataJson = SerializeJson(packet["slot_data"]);
                } catch (std::exception const& error) {
                    Disconnect();
                    SetState(ConnectionState::Failed, std::string("Unexpected slot data: ") + error.what());
                    return;
                }

                {
                    std::lock_guard lock(stateMutex);
                    sessionEstablished = true;
                    reconnectAttempt = 0;
                    if (candidateIndex < candidateUrls.size())
                        workingUrl = candidateUrls[candidateIndex];
                    teamNumber = packet.HasMember("team") && packet["team"].IsInt() ? packet["team"].GetInt() : 0;
                    slotNumber = packet.HasMember("slot") && packet["slot"].IsInt() ? packet["slot"].GetInt() : 0;
                    if (packet.HasMember("slot_info") && packet["slot_info"].IsObject()) {
                        for (auto const& entry : packet["slot_info"].GetObject()) {
                            if (entry.value.IsObject() && entry.value.HasMember("game") && entry.value["game"].IsString())
                                playerGames[std::atoi(entry.name.GetString())] = entry.value["game"].GetString();
                        }
                    }
                    if (packet.HasMember("players") && packet["players"].IsArray()) {
                        for (auto const& player : packet["players"].GetArray()) {
                            if (player.HasMember("team") && player["team"].IsInt() && player["team"].GetInt() == teamNumber
                                && player.HasMember("slot") && player["slot"].IsInt() && player.HasMember("alias") && player["alias"].IsString())
                                playerAliases[player["slot"].GetInt()] = player["alias"].GetString();
                            bool isThisSlot = player.HasMember("team") && player["team"].IsInt() && player["team"].GetInt() == teamNumber
                                && player.HasMember("slot") && player["slot"].IsInt() && player["slot"].GetInt() == slotNumber;
                            if (isThisSlot && player.HasMember("alias") && player["alias"].IsString())
                                slotAlias = player["alias"].GetString();
                        }
                    }
                    auto checkedLocations = ReadLocationList(packet, "checked_locations");
                    confirmedLocationChecks = std::set<int64_t>(checkedLocations.begin(), checkedLocations.end());
                    for (auto checkedLocation : checkedLocations)
                        pendingLocationChecks.erase(checkedLocation);
                }

                if (slotData.deathLinkEnabled) {
                    SendPackets(BuildPacket("ConnectUpdate", [](JsonWriter& writer) {
                        writer.Key("tags");
                        writer.StartArray();
                        writer.String(deathLinkTag);
                        writer.EndArray();
                    }));
                    std::lock_guard lock(stateMutex);
                    deathLinkTagged = true;
                }

                SetState(ConnectionState::Connected, "Connected as " + GetSlotAlias());
                FlushPendingLocationChecks();
                NotifyPendingLocationsChanged();
                NotifyCheckedLocationsChanged();
                bool sendGoal = false;
                {
                    std::lock_guard lock(stateMutex);
                    sendGoal = goalPending;
                }
                if (sendGoal)
                    SendGoalComplete();
                if (callbacks.onConnected)
                    callbacks.onConnected(slotData, slotDataJson);
            } else if (command == "ConnectionRefused") {
                std::string description = DescribeRefusal(packet);
                CancelReconnectTimer();
                CloseSocket();
                SetState(ConnectionState::Failed, description);
                return;
            } else if (command == "RoomUpdate") {
                auto confirmed = ReadLocationList(packet, "checked_locations");
                if (confirmed.empty())
                    continue;
                {
                    std::lock_guard lock(stateMutex);
                    for (auto checkedLocation : confirmed) {
                        pendingLocationChecks.erase(checkedLocation);
                        confirmedLocationChecks.insert(checkedLocation);
                    }
                }
                NotifyPendingLocationsChanged();
                NotifyCheckedLocationsChanged();
            } else if (command == "ReceivedItems") {
                std::vector<ReceivedItem> itemsSnapshot;
                bool needsResync = false;
                {
                    std::lock_guard lock(stateMutex);
                    size_t startIndex = packet.HasMember("index") && packet["index"].IsUint64() ? packet["index"].GetUint64() : 0;
                    if (startIndex == 0)
                        receivedItems.clear();
                    if (startIndex != receivedItems.size() || !packet.HasMember("items") || !packet["items"].IsArray()) {
                        needsResync = true;
                    } else {
                        for (auto const& networkItem : packet["items"].GetArray()) {
                            ReceivedItem item;
                            item.itemId = networkItem["item"].GetInt64();
                            item.locationId = networkItem["location"].GetInt64();
                            item.sendingPlayer = networkItem["player"].GetInt();
                            auto const& ownItemNames = itemNamesByGame[std::string(gameName)];
                            auto name = ownItemNames.find(item.itemId);
                            item.itemName = name != ownItemNames.end() ? name->second : "";
                            receivedItems.push_back(std::move(item));
                        }
                        itemsSnapshot = receivedItems;
                    }
                }
                if (needsResync) {
                    SendPackets(R"([{"cmd":"Sync"}])");
                    continue;
                }
                if (callbacks.onItemsChanged)
                    callbacks.onItemsChanged(itemsSnapshot);
            } else if (command == "Bounced") {
                bool isDeathLink = false;
                if (packet.HasMember("tags") && packet["tags"].IsArray()) {
                    for (auto const& tag : packet["tags"].GetArray())
                        isDeathLink |= tag.IsString() && std::string_view(tag.GetString()) == deathLinkTag;
                }
                if (!isDeathLink || !packet.HasMember("data") || !packet["data"].IsObject())
                    continue;
                auto const& data = packet["data"];
                double sentTime = data.HasMember("time") && data["time"].IsNumber() ? data["time"].GetDouble() : 0;
                {
                    std::lock_guard lock(stateMutex);
                    // rapidjson's default number parsing can be one ulp off, so match the echo by tolerance
                    if (!deathLinkTagged || std::abs(sentTime - lastDeathLinkSentTime) < 0.001)
                        continue;
                }
                DeathLinkEvent deathLink;
                deathLink.source = data.HasMember("source") && data["source"].IsString() ? data["source"].GetString() : "";
                deathLink.cause = data.HasMember("cause") && data["cause"].IsString() ? data["cause"].GetString() : "";
                if (callbacks.onDeathLink)
                    callbacks.onDeathLink(deathLink);
            } else if (command == "PrintJSON") {
                ServerMessage message = RenderPrintJson(packet);
                if (!message.text.empty() && callbacks.onServerMessage)
                    callbacks.onServerMessage(message);
            }
        }
    }

    void ArchipelagoClient::FlushPendingLocationChecks() {
        std::vector<int64_t> unconfirmed = GetPendingLocationChecks();
        if (unconfirmed.empty() || GetState() != ConnectionState::Connected)
            return;
        SendPackets(BuildPacket("LocationChecks", [&](JsonWriter& writer) {
            writer.Key("locations");
            writer.StartArray();
            for (auto locationId : unconfirmed)
                writer.Int64(locationId);
            writer.EndArray();
        }));
    }

    void ArchipelagoClient::SendLocationChecks(std::vector<int64_t> const& locationIds) {
        if (locationIds.empty())
            return;
        {
            std::lock_guard lock(stateMutex);
            pendingLocationChecks.insert(locationIds.begin(), locationIds.end());
        }
        NotifyPendingLocationsChanged();
        FlushPendingLocationChecks();
    }

    void ArchipelagoClient::RestorePendingLocationChecks(std::vector<int64_t> const& locationIds) {
        {
            std::lock_guard lock(stateMutex);
            pendingLocationChecks.insert(locationIds.begin(), locationIds.end());
        }
        FlushPendingLocationChecks();
    }

    std::vector<int64_t> ArchipelagoClient::GetPendingLocationChecks() const {
        std::lock_guard lock(stateMutex);
        return {pendingLocationChecks.begin(), pendingLocationChecks.end()};
    }

    void ArchipelagoClient::SendGoalComplete() {
        {
            std::lock_guard lock(stateMutex);
            goalPending = state != ConnectionState::Connected;
            if (goalPending)
                return;
        }
        SendPackets(BuildPacket("StatusUpdate", [](JsonWriter& writer) {
            writer.Key("status");
            writer.Int(clientStatusGoal);
        }));
    }

    void ArchipelagoClient::SendDeathLink(std::string const& cause) {
        double now = UnixTimeSeconds();
        {
            std::lock_guard lock(stateMutex);
            if (!deathLinkTagged || state != ConnectionState::Connected)
                return;
            lastDeathLinkSentTime = now;
        }
        std::string source = GetSlotAlias();
        SendPackets(BuildPacket("Bounce", [&](JsonWriter& writer) {
            writer.Key("tags");
            writer.StartArray();
            writer.String(deathLinkTag);
            writer.EndArray();
            writer.Key("data");
            writer.StartObject();
            writer.Key("time");
            writer.Double(now);
            writer.Key("source");
            WriteString(writer, source);
            writer.Key("cause");
            WriteString(writer, cause);
            writer.EndObject();
        }));
    }
}
