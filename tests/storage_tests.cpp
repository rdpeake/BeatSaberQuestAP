#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "archipelago/ApbsArchive.hpp"
#include "archipelago/BeatSaverKeyCache.hpp"
#include "archipelago/SavedSession.hpp"

using namespace Archipelago;

namespace {
    int failures = 0;

    void Expect(bool condition, std::string const& description) {
        if (!condition) {
            std::cerr << "FAIL: " << description << "\n";
            failures++;
        }
    }

    std::string ReadFile(std::filesystem::path const& path) {
        std::ifstream stream(path);
        std::stringstream contents;
        contents << stream.rdbuf();
        return contents.str();
    }

    void TestKeyCacheRoundTrip(std::filesystem::path const& workDirectory) {
        auto cachePath = (workDirectory / "nested" / "keys.json").string();
        {
            BeatSaverKeyCache cache(cachePath);
            cache.Store("1A2B", {"abc123", "DEF456"});
            cache.Store("1a2b", {"ABC123"});
            Expect(cache.Save(), "cache saved into a new folder");
        }

        BeatSaverKeyCache reloaded(cachePath);
        reloaded.Load();
        Expect(reloaded.Contains("1a2b"), "key survives reload");
        Expect(reloaded.HashesForKey("1A2B") == std::vector<std::string>({"ABC123", "DEF456"}), "hashes normalized and deduplicated");
        Expect(reloaded.KeyForHash("def456") == std::optional<std::string>("1a2b"), "reverse lookup ignores case");
        Expect(!reloaded.KeyForHash("000000").has_value(), "unknown hash");
    }

    void TestKeyCacheDropsSharedHashes(std::filesystem::path const& workDirectory) {
        auto cachePath = (workDirectory / "shared" / "keys.json").string();
        {
            BeatSaverKeyCache cache(cachePath);
            cache.Store("1a2b", {"SAME", "AAA111"});
            cache.Store("3c4d", {"SAME"});
            cache.Save();
        }
        BeatSaverKeyCache reloaded(cachePath);
        reloaded.Load();
        Expect(!reloaded.KeyForHash("SAME").has_value(), "hash claimed by two keys is dropped");
        Expect(reloaded.HashesForKey("1a2b") == std::vector<std::string>({"AAA111"}), "other hashes of the key are kept");
        Expect(!reloaded.Contains("3c4d"), "key left without hashes is looked up again");
    }

    void TestApbsExtraction(std::filesystem::path const& workDirectory) {
        auto outputDirectory = workDirectory / "Playlists";
        auto written = ExtractPlaylistsFromApbs(FIXTURE_DIRECTORY "/sample.apbs", outputDirectory.string());
        Expect(written.size() == 2, "both playlists extracted, readme skipped");
        Expect(ReadFile(outputDirectory / "AP_Campaign.bplist").find("\"key\":\"1a2b\"") != std::string::npos, "deflated playlist content intact");
        Expect(ReadFile(outputDirectory / "Stored.bplist") == R"({"playlistTitle":"Stored"})", "stored playlist content intact");
        Expect(!std::filesystem::exists(outputDirectory / "nested"), "archive folders are flattened");

        bool threw = false;
        try {
            ExtractPlaylistsFromApbs(FIXTURE_DIRECTORY "/not-a-zip.apbs", outputDirectory.string());
        } catch (std::exception const&) {
            threw = true;
        }
        Expect(threw, "invalid archive reported");
    }

    void TestSavedSessionRoundTrip(std::filesystem::path const& workDirectory) {
        auto sessionPath = (workDirectory / "session.json").string();
        Expect(!LoadSavedSession(sessionPath).has_value(), "no session before saving");

        SavedSession session;
        session.host = "archipelago.gg";
        session.port = 38281;
        session.slotName = "Saber \"Player\"";
        session.passwordRequired = true;
        session.slotDataJson = R"({"game_mode":1,"node_to_keystr":{}})";
        session.receivedItemNames = {"Progressive Speed Unlock", "Bloq Key"};
        session.unconfirmedLocations = {1013, 4007};
        session.checkedLocations = {1001, 1002};
        Expect(WriteSavedSession(sessionPath, session), "session saved");

        auto loaded = LoadSavedSession(sessionPath);
        Expect(loaded.has_value(), "session loads");
        Expect(loaded && loaded->slotName == session.slotName && loaded->port == 38281 && loaded->passwordRequired, "connection details survive");
        Expect(loaded && loaded->slotDataJson == session.slotDataJson, "slot data survives as text");
        Expect(loaded && loaded->receivedItemNames == session.receivedItemNames, "items survive");
        Expect(loaded && loaded->unconfirmedLocations == session.unconfirmedLocations, "unsent checks survive");
        Expect(loaded && loaded->checkedLocations == session.checkedLocations, "confirmed checks survive");
        Expect(ReadFile(sessionPath).find("password\"") == std::string::npos, "no password field is written");

        DeleteSavedSession(sessionPath);
        Expect(!LoadSavedSession(sessionPath).has_value(), "session deleted");
    }
}

int main() {
    auto workDirectory = std::filesystem::temp_directory_path() / "beatsaberquestap-storage-tests";
    std::filesystem::remove_all(workDirectory);

    TestKeyCacheRoundTrip(workDirectory);
    TestKeyCacheDropsSharedHashes(workDirectory);
    TestApbsExtraction(workDirectory);
    TestSavedSessionRoundTrip(workDirectory);

    std::filesystem::remove_all(workDirectory);
    if (failures > 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "storage tests passed\n";
    return EXIT_SUCCESS;
}
