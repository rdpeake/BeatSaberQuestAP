#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "archipelago/ArchipelagoClient.hpp"
#include "archipelago/CertificateStore.hpp"
#include "archipelago/ProgressionRules.hpp"
#include "ixwebsocket/IXWebSocketServer.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using namespace Archipelago;

namespace {
    int failures = 0;

    void Expect(bool condition, std::string const& description) {
        if (!condition) {
            std::cerr << "FAIL: " << description << "\n";
            failures++;
        }
    }

    constexpr int mockServerPort = 38291;

    constexpr char const* slotDataJson = R"({
        "DeathLink": 1, "campaign_name": "Mock",
        "node_to_keystr": {"0": "AAA_Standard_4", "1": "BBB_Standard_3", "2": "CCC_Standard_2", "3": "DDD_Standard_1"},
        "keystr_to_node": {"AAA_Standard_4": 0, "BBB_Standard_3": 1, "CCC_Standard_2": 2, "DDD_Standard_1": 3},
        "start_songs": [0], "map_type_counts": [4, 0, 0, 0],
        "game_mode": "option_categories", "num_grades": 5, "location_name_to_mnemonic": {}
    })";

    // minimal stand-in for MultiServer covering the packets the client uses
    class MockArchipelagoServer {
    public:
        std::mutex recordMutex;
        std::set<int64_t> checkedLocations;
        std::vector<std::string> bouncedCauses;
        std::vector<std::string> connectTags;
        int goalStatus = 0;

        MockArchipelagoServer() : server(mockServerPort, "127.0.0.1") {
            server.setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState>, ix::WebSocket& client, ix::WebSocketMessagePtr const& message) {
                if (message->type == ix::WebSocketMessageType::Open) {
                    client.sendText(R"([{"cmd":"RoomInfo","version":{"major":0,"minor":6,"build":2,"class":"Version"},"games":["Beat Saber"],"password":true}])");
                    return;
                }
                if (message->type != ix::WebSocketMessageType::Message)
                    return;
                rapidjson::Document packets;
                packets.Parse(message->str.c_str());
                for (auto const& packet : packets.GetArray())
                    HandlePacket(client, packet);
            });
        }

        bool Start() {
            auto [listening, error] = server.listen();
            if (!listening) {
                std::cerr << "mock server failed: " << error << "\n";
                return false;
            }
            server.start();
            return true;
        }

        void Stop() { server.stop(); }

    private:
        ix::WebSocketServer server;

        void HandlePacket(ix::WebSocket& client, rapidjson::Value const& packet) {
            std::string command = packet["cmd"].GetString();
            if (command == "GetDataPackage") {
                client.sendText(R"([{"cmd":"DataPackage","data":{"games":{"Beat Saber":{"item_name_to_id":{
                    "Progressive Speed Unlock": 101, "Victory": 999}, "location_name_to_id": {}, "checksum": "x"}}}}])");
            } else if (command == "Connect") {
                if (std::string(packet["password"].GetString()) != "secret") {
                    client.sendText(R"([{"cmd":"ConnectionRefused","errors":["InvalidPassword"]}])");
                    return;
                }
                std::string connected = std::string(R"([{"cmd":"Connected","team":0,"slot":3,"players":[{"team":0,"slot":3,"alias":"Saber Player","name":"Player3"}],"missing_locations":[],"checked_locations":[],"slot_data":)") + slotDataJson + "}]";
                client.sendText(connected);
                client.sendText(R"([{"cmd":"ReceivedItems","index":0,"items":[{"item":101,"location":5,"player":1,"flags":1}]}])");
            } else if (command == "ConnectUpdate") {
                {
                    std::lock_guard lock(recordMutex);
                    for (auto const& tag : packet["tags"].GetArray())
                        connectTags.emplace_back(tag.GetString());
                }
                client.sendText(R"([{"cmd":"Bounced","tags":["DeathLink"],"data":{"time":1.5,"source":"Other Player","cause":"fell over"}}])");
            } else if (command == "LocationChecks") {
                {
                    std::lock_guard lock(recordMutex);
                    for (auto const& location : packet["locations"].GetArray())
                        checkedLocations.insert(location.GetInt64());
                }
                client.sendText(R"([{"cmd":"ReceivedItems","index":1,"items":[{"item":101,"location":6,"player":1,"flags":1},{"item":999,"location":7,"player":2,"flags":1}]}])");
                client.sendText(R"([{"cmd":"ReceivedItems","index":9,"items":[]}])");
            } else if (command == "Sync") {
                client.sendText(R"([{"cmd":"ReceivedItems","index":0,"items":[{"item":101,"location":5,"player":1,"flags":1},{"item":101,"location":6,"player":1,"flags":1},{"item":999,"location":7,"player":2,"flags":1}]}])");
            } else if (command == "Bounce") {
                {
                    std::lock_guard lock(recordMutex);
                    bouncedCauses.emplace_back(packet["data"]["cause"].GetString());
                }
                // the real server echoes bounces back to the sender too
                rapidjson::StringBuffer timeText;
                rapidjson::Writer<rapidjson::StringBuffer> timeWriter(timeText);
                timeWriter.Double(packet["data"]["time"].GetDouble());
                client.sendText(std::string(R"([{"cmd":"Bounced","tags":["DeathLink"],"data":{"time":)") + timeText.GetString() + R"(,"source":"Saber Player","cause":"echo"}}])");
            } else if (command == "StatusUpdate") {
                std::lock_guard lock(recordMutex);
                goalStatus = packet["status"].GetInt();
            }
        }
    };

    struct DispatchQueue {
        std::mutex queueMutex;
        std::deque<std::function<void()>> pending;

        void Post(std::function<void()> work) {
            std::lock_guard lock(queueMutex);
            pending.push_back(std::move(work));
        }

        // runs queued callbacks on the calling thread, like the game's main thread would
        template <typename Condition>
        bool PumpUntil(Condition condition, std::chrono::seconds timeout = std::chrono::seconds(10)) {
            auto deadline = std::chrono::steady_clock::now() + timeout;
            while (std::chrono::steady_clock::now() < deadline) {
                std::deque<std::function<void()>> ready;
                {
                    std::lock_guard lock(queueMutex);
                    ready.swap(pending);
                }
                for (auto& work : ready)
                    work();
                if (condition())
                    return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return false;
        }
    };
}

int main() {
    MockArchipelagoServer server;
    if (!server.Start())
        return EXIT_FAILURE;

    std::string certificateFile = WriteSystemCertificateBundle("/tmp/beatsaberquestap-test/cacert.pem");
    Expect(!certificateFile.empty(), "host certificates collected");

    DispatchQueue queue;
    ConnectionState lastState = ConnectionState::Disconnected;
    std::string lastDetail;
    std::optional<SlotData> receivedSlotData;
    std::vector<ReceivedItem> latestItems;
    std::vector<DeathLinkEvent> deathLinks;
    int itemUpdates = 0;

    ClientCallbacks callbacks;
    callbacks.onStateChanged = [&](ConnectionState state, std::string const& detail) {
        lastState = state;
        lastDetail = detail;
        std::cout << "state: " << detail << "\n";
    };
    callbacks.onConnected = [&](SlotData const& slotData) { receivedSlotData = slotData; };
    callbacks.onItemsChanged = [&](std::vector<ReceivedItem> const& items) {
        latestItems = items;
        itemUpdates++;
    };
    callbacks.onDeathLink = [&](DeathLinkEvent const& deathLink) { deathLinks.push_back(deathLink); };

    ArchipelagoClient client(callbacks, [&](std::function<void()> work) { queue.Post(std::move(work)); }, certificateFile);

    client.Connect({"127.0.0.1", mockServerPort, "Player3", "wrong"});
    Expect(queue.PumpUntil([&] { return lastState == ConnectionState::Failed; }), "wrong password fails");
    Expect(lastDetail.find("wrong password") != std::string::npos, "refusal reason reported");

    client.Connect({"127.0.0.1", mockServerPort, "Player3", "secret"});
    Expect(queue.PumpUntil([&] { return lastState == ConnectionState::Connected && itemUpdates > 0; }), "falls back from wss to ws and connects");
    Expect(receivedSlotData.has_value() && receivedSlotData->deathLinkEnabled, "slot data delivered");
    Expect(client.GetSlotAlias() == "Saber Player", "alias read from players");
    Expect(latestItems.size() == 1 && latestItems[0].itemName == "Progressive Speed Unlock", "initial items named via data package");

    Expect(queue.PumpUntil([&] { return deathLinks.size() == 1; }), "death link from another player received");
    Expect(!deathLinks.empty() && deathLinks[0].cause == "fell over", "death link cause delivered");

    auto progression = ComputeProgression(*receivedSlotData, {latestItems[0].itemName});
    auto node = FindNodeForIdent(*receivedSlotData, "BBB_Standard_3");
    Expect(IsIdentUnlocked(progression, "BBB_Standard_3") && node.has_value(), "first unlock playable");
    client.SendLocationChecks(LocationsForClear(*receivedSlotData, *node, GradeIndexFromRankName("B")));

    Expect(queue.PumpUntil([&] { return latestItems.size() == 3; }), "out-of-order index triggers resync");
    Expect(!latestItems.empty() && latestItems.back().itemName == "Victory", "victory received");
    {
        std::lock_guard lock(server.recordMutex);
        Expect(server.checkedLocations == std::set<int64_t>({1007, 1008}), "location ids sent");
        Expect(server.connectTags == std::vector<std::string>({"DeathLink"}), "death link tag registered");
    }

    client.SendGoalComplete();
    client.SendDeathLink("too many bloqs :(");
    queue.PumpUntil([&] {
        std::lock_guard lock(server.recordMutex);
        return server.goalStatus == 30 && !server.bouncedCauses.empty();
    });
    queue.PumpUntil([] { return false; }, std::chrono::seconds(1));
    {
        std::lock_guard lock(server.recordMutex);
        Expect(server.goalStatus == 30, "goal status sent");
        Expect(server.bouncedCauses == std::vector<std::string>({"too many bloqs :("}), "death link sent");
    }
    Expect(deathLinks.size() == 1, "own death link echo ignored");

    client.Disconnect();
    Expect(client.GetState() == ConnectionState::Disconnected, "disconnects cleanly");

    server.Stop();

    if (failures > 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "client tests passed\n";
    return EXIT_SUCCESS;
}
