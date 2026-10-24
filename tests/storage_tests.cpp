#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "archipelago/ApbsArchive.hpp"
#include "archipelago/BeatSaverKeyCache.hpp"

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
}

int main() {
    auto workDirectory = std::filesystem::temp_directory_path() / "beatsaberquestap-storage-tests";
    std::filesystem::remove_all(workDirectory);

    TestKeyCacheRoundTrip(workDirectory);
    TestApbsExtraction(workDirectory);

    std::filesystem::remove_all(workDirectory);
    if (failures > 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "storage tests passed\n";
    return EXIT_SUCCESS;
}
