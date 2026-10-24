#include "archipelago/ArchipelagoClient.hpp"

#include <chrono>
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
        constexpr int pingIntervalSeconds = 30;
        constexpr char const* deathLinkTag = "DeathLink";

        using JsonWriter = rapidjson::Writer<rapidjson::StringBuffer>;

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

        std::string FlattenPrintJson(rapidjson::Value const& packet) {
            std::string text;
            auto data = packet.FindMember("data");
            if (data == packet.MemberEnd() || !data->value.IsArray())
                return text;
            for (auto const& part : data->value.GetArray()) {
                auto partText = part.FindMember("text");
                if (partText != part.MemberEnd() && partText->value.IsString())
                    text += partText->value.GetString();
            }
            return text;
        }
    }

    ArchipelagoClient::ArchipelagoClient(ClientCallbacks callbacks, Dispatcher dispatcher, std::string certificateAuthorityFile)
        : callbacks(std::move(callbacks)),
          dispatcher(std::move(dispatcher)),
          certificateAuthorityFile(std::move(certificateAuthorityFile)),
          lifetimeToken(std::make_shared<bool>(true)) {}

    ArchipelagoClient::~ArchipelagoClient() {
        lifetimeToken.reset();
        std::unique_ptr<ix::WebSocket> closingSocket;
        {
            std::lock_guard lock(stateMutex);
            closingSocket = std::move(socket);
        }
        if (closingSocket)
            closingSocket->stop();
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
        return {"wss://" + hostAndPort, "ws://" + hostAndPort};
    }

    void ArchipelagoClient::Connect(ConnectionSettings const& newSettings) {
        Disconnect();
        {
            std::lock_guard lock(stateMutex);
            settings = newSettings;
            candidateUrls = CandidateUrls(settings.host, settings.port);
            candidateIndex = 0;
            userRequestedDisconnect = false;
            receivedItems.clear();
            itemNamesById.clear();
            deathLinkTagged = false;
        }
        SetState(ConnectionState::Connecting, "Connecting to " + newSettings.host);
        OpenNextCandidate();
    }

    void ArchipelagoClient::Disconnect() {
        std::unique_ptr<ix::WebSocket> closingSocket;
        {
            std::lock_guard lock(stateMutex);
            userRequestedDisconnect = true;
            closingSocket = std::move(socket);
        }
        if (closingSocket) {
            closingSocket->stop();
            SetState(ConnectionState::Disconnected, "Disconnected");
        }
    }

    void ArchipelagoClient::OpenNextCandidate() {
        std::lock_guard lock(stateMutex);
        if (candidateIndex >= candidateUrls.size())
            return;

        auto url = candidateUrls[candidateIndex];
        socketOpened = false;
        socket = std::make_unique<ix::WebSocket>();
        socket->setUrl(url);
        socket->disableAutomaticReconnection();
        socket->setHandshakeTimeout(handshakeTimeoutSeconds);
        socket->setPingInterval(pingIntervalSeconds);

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
                        HandleSocketError(reason);
                        break;
                    case ix::WebSocketMessageType::Close:
                        HandleSocketClosed(reason);
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

    void ArchipelagoClient::HandleSocketError(std::string const& reason) {
        std::unique_ptr<ix::WebSocket> failedSocket;
        bool tryAnotherCandidate = false;
        {
            std::lock_guard lock(stateMutex);
            if (userRequestedDisconnect)
                return;
            tryAnotherCandidate = !socketOpened && candidateIndex + 1 < candidateUrls.size();
            failedSocket = std::move(socket);
            if (tryAnotherCandidate)
                candidateIndex++;
        }
        if (failedSocket)
            failedSocket->stop();

        if (tryAnotherCandidate) {
            OpenNextCandidate();
            return;
        }
        SetState(ConnectionState::Failed, reason.empty() ? "Could not reach the server" : reason);
    }

    void ArchipelagoClient::HandleSocketClosed(std::string const& reason) {
        std::unique_ptr<ix::WebSocket> closedSocket;
        {
            std::lock_guard lock(stateMutex);
            if (userRequestedDisconnect)
                return;
            closedSocket = std::move(socket);
        }
        if (closedSocket)
            closedSocket->stop();
        SetState(ConnectionState::Failed, reason.empty() ? "Server closed the connection" : "Server closed the connection: " + reason);
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
                rapidjson::StringBuffer buffer;
                JsonWriter writer(buffer);
                writer.StartArray();
                writer.StartObject();
                writer.Key("cmd");
                writer.String("GetDataPackage");
                writer.Key("games");
                writer.StartArray();
                WriteString(writer, gameName);
                writer.EndArray();
                writer.EndObject();
                writer.EndArray();
                SendPackets(buffer.GetString());
            } else if (command == "DataPackage") {
                {
                    std::lock_guard lock(stateMutex);
                    auto const& games = packet["data"]["games"];
                    auto game = games.FindMember(gameName.data());
                    if (game != games.MemberEnd() && game->value.HasMember("item_name_to_id")) {
                        for (auto const& entry : game->value["item_name_to_id"].GetObject())
                            itemNamesById[entry.value.GetInt64()] = entry.name.GetString();
                    }
                }

                ConnectionSettings connectSettings;
                {
                    std::lock_guard lock(stateMutex);
                    connectSettings = settings;
                }
                rapidjson::StringBuffer buffer;
                JsonWriter writer(buffer);
                writer.StartArray();
                writer.StartObject();
                writer.Key("cmd");
                writer.String("Connect");
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
                writer.EndObject();
                writer.EndArray();
                SendPackets(buffer.GetString());
            } else if (command == "Connected") {
                SlotData slotData;
                try {
                    slotData = ParseSlotData(packet["slot_data"]);
                } catch (std::exception const& error) {
                    SetState(ConnectionState::Failed, std::string("Unexpected slot data: ") + error.what());
                    Disconnect();
                    return;
                }

                {
                    std::lock_guard lock(stateMutex);
                    teamNumber = packet["team"].GetInt();
                    slotNumber = packet["slot"].GetInt();
                    if (packet.HasMember("players") && packet["players"].IsArray()) {
                        for (auto const& player : packet["players"].GetArray()) {
                            if (player["team"].GetInt() == teamNumber && player["slot"].GetInt() == slotNumber)
                                slotAlias = player["alias"].GetString();
                        }
                    }
                }

                if (slotData.deathLinkEnabled) {
                    rapidjson::StringBuffer buffer;
                    JsonWriter writer(buffer);
                    writer.StartArray();
                    writer.StartObject();
                    writer.Key("cmd");
                    writer.String("ConnectUpdate");
                    writer.Key("tags");
                    writer.StartArray();
                    writer.String(deathLinkTag);
                    writer.EndArray();
                    writer.EndObject();
                    writer.EndArray();
                    SendPackets(buffer.GetString());
                    std::lock_guard lock(stateMutex);
                    deathLinkTagged = true;
                }

                SetState(ConnectionState::Connected, "Connected as " + GetSlotAlias());
                if (callbacks.onConnected)
                    callbacks.onConnected(slotData);
            } else if (command == "ConnectionRefused") {
                std::string description = DescribeRefusal(packet);
                Disconnect();
                SetState(ConnectionState::Failed, description);
                return;
            } else if (command == "ReceivedItems") {
                std::vector<ReceivedItem> itemsSnapshot;
                bool needsResync = false;
                {
                    std::lock_guard lock(stateMutex);
                    size_t startIndex = packet["index"].GetUint64();
                    if (startIndex == 0)
                        receivedItems.clear();
                    if (startIndex != receivedItems.size()) {
                        needsResync = true;
                    } else {
                        for (auto const& networkItem : packet["items"].GetArray()) {
                            ReceivedItem item;
                            item.itemId = networkItem["item"].GetInt64();
                            item.locationId = networkItem["location"].GetInt64();
                            item.sendingPlayer = networkItem["player"].GetInt();
                            auto name = itemNamesById.find(item.itemId);
                            item.itemName = name != itemNamesById.end() ? name->second : "";
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
                if (!isDeathLink || !packet.HasMember("data"))
                    continue;
                auto const& data = packet["data"];
                double sentTime = data.HasMember("time") && data["time"].IsNumber() ? data["time"].GetDouble() : 0;
                {
                    std::lock_guard lock(stateMutex);
                    if (!deathLinkTagged || sentTime == lastDeathLinkSentTime)
                        continue;
                }
                DeathLinkEvent deathLink;
                deathLink.source = data.HasMember("source") && data["source"].IsString() ? data["source"].GetString() : "";
                deathLink.cause = data.HasMember("cause") && data["cause"].IsString() ? data["cause"].GetString() : "";
                if (callbacks.onDeathLink)
                    callbacks.onDeathLink(deathLink);
            } else if (command == "PrintJSON") {
                std::string message = FlattenPrintJson(packet);
                if (!message.empty() && callbacks.onServerMessage)
                    callbacks.onServerMessage(message);
            }
        }
    }

    void ArchipelagoClient::SendLocationChecks(std::vector<int64_t> const& locationIds) {
        if (locationIds.empty())
            return;
        rapidjson::StringBuffer buffer;
        JsonWriter writer(buffer);
        writer.StartArray();
        writer.StartObject();
        writer.Key("cmd");
        writer.String("LocationChecks");
        writer.Key("locations");
        writer.StartArray();
        for (auto locationId : locationIds)
            writer.Int64(locationId);
        writer.EndArray();
        writer.EndObject();
        writer.EndArray();
        SendPackets(buffer.GetString());
    }

    void ArchipelagoClient::SendGoalComplete() {
        rapidjson::StringBuffer buffer;
        JsonWriter writer(buffer);
        writer.StartArray();
        writer.StartObject();
        writer.Key("cmd");
        writer.String("StatusUpdate");
        writer.Key("status");
        writer.Int(clientStatusGoal);
        writer.EndObject();
        writer.EndArray();
        SendPackets(buffer.GetString());
    }

    void ArchipelagoClient::SendDeathLink(std::string const& cause) {
        double now = UnixTimeSeconds();
        {
            std::lock_guard lock(stateMutex);
            if (!deathLinkTagged)
                return;
            lastDeathLinkSentTime = now;
        }
        rapidjson::StringBuffer buffer;
        JsonWriter writer(buffer);
        writer.StartArray();
        writer.StartObject();
        writer.Key("cmd");
        writer.String("Bounce");
        writer.Key("tags");
        writer.StartArray();
        writer.String(deathLinkTag);
        writer.EndArray();
        writer.Key("data");
        writer.StartObject();
        writer.Key("time");
        writer.Double(now);
        writer.Key("source");
        WriteString(writer, GetSlotAlias());
        writer.Key("cause");
        WriteString(writer, cause);
        writer.EndObject();
        writer.EndObject();
        writer.EndArray();
        SendPackets(buffer.GetString());
    }
}
