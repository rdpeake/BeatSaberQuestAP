#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "archipelago/LevelIdentity.hpp"
#include "archipelago/ProgressionRules.hpp"
#include "archipelago/SlotData.hpp"
#include "rapidjson/document.h"

using namespace Archipelago;

namespace {
    int failures = 0;

    void Expect(bool condition, std::string const& description) {
        if (!condition) {
            std::cerr << "FAIL: " << description << "\n";
            failures++;
        }
    }

    template <typename T>
    void ExpectEqual(T const& actual, T const& expected, std::string const& description) {
        Expect(actual == expected, description);
    }

    constexpr char const* categorySlotDataJson = R"({
        "DeathLink": 1,
        "campaign_name": "Test Campaign",
        "node_to_keystr": {"0": "AAA_Standard_4", "1": "BBB_Standard_3", "2": "CCC_Standard_2",
                           "3": "DDD_Standard_4", "4": "EEE_OneSaber_1",
                           "5": "OST_100Bills_Standard_2", "6": "FFF_Standard_4",
                           "7": "1A2B_Standard_4", "8": "HHH_Standard_3"},
        "keystr_to_node": {"AAA_Standard_4": 0, "BBB_Standard_3": 1, "CCC_Standard_2": 2,
                           "DDD_Standard_4": 3, "EEE_OneSaber_1": 4,
                           "OST_100Bills_Standard_2": 5, "FFF_Standard_4": 6,
                           "1A2B_Standard_4": 7, "HHH_Standard_3": 8},
        "start_songs": [0, 3, 5, 7],
        "map_type_counts": [3, 2, 2, 2],
        "game_mode": "option_categories",
        "num_grades": 5,
        "location_name_to_mnemonic": {}
    })";

    SlotData ParseJson(char const* json) {
        rapidjson::Document document;
        document.Parse(json);
        return ParseSlotData(document);
    }

    void TestSlotDataParsing() {
        auto slotData = ParseJson(categorySlotDataJson);
        Expect(slotData.deathLinkEnabled, "DeathLink 1 enables death link");
        ExpectEqual(slotData.campaignName, std::string("Test Campaign"), "campaign name");
        ExpectEqual(slotData.nodeToIdent.size(), size_t(9), "node count");
        ExpectEqual(slotData.mapTypeCounts, std::vector<int>({3, 2, 2, 2}), "map type counts");
        Expect(slotData.gameMode == GameMode::Categories, "unknown mode falls back to categories");

        auto embedded = ParseJson(R"({
            "DeathLink": false, "campaign_name": "C",
            "node_to_keystr": "{\"1\": \"AAA_Standard_4\"}",
            "keystr_to_node": "{\"AAA_Standard_4\": 1}",
            "start_songs": "[]", "map_type_counts": "[1, 0, 0, 0]",
            "game_mode": "\"option_presetPass\"", "num_grades": "5",
            "location_name_to_mnemonic": "{}"
        })");
        Expect(!embedded.deathLinkEnabled, "false disables death link");
        Expect(embedded.gameMode == GameMode::PresetPass, "JSON-quoted game mode is decoded");
        ExpectEqual(embedded.nodeToIdent.at(1), std::string("AAA_Standard_4"), "JSON-in-string dictionary is decoded");
        ExpectEqual(embedded.gradeCount, 5, "numeric string is decoded");

        bool threw = false;
        try {
            ParseJson(R"({"DeathLink": 0})");
        } catch (std::exception const&) {
            threw = true;
        }
        Expect(threw, "missing keys are reported");
    }

    void TestCategoryProgression() {
        auto slotData = ParseJson(categorySlotDataJson);

        auto start = ComputeProgression(slotData, {});
        ExpectEqual(start.unlockedIdents, std::vector<std::string>({"AAA_Standard_4", "DDD_Standard_4", "OST_100Bills_Standard_2", "1A2B_Standard_4"}), "start songs unlocked");

        auto progressed = ComputeProgression(slotData, {"Progressive Speed Unlock", "Progressive Tech Unlock", "Nothing", "Progressive Speed Unlock"});
        Expect(IsIdentUnlocked(progressed, "BBB_Standard_3"), "first speed unlock opens node 1");
        Expect(IsIdentUnlocked(progressed, "CCC_Standard_2"), "second speed unlock opens node 2");
        Expect(IsIdentUnlocked(progressed, "EEE_OneSaber_1"), "first tech unlock opens node offset+1");
        Expect(!IsIdentUnlocked(progressed, "FFF_Standard_4"), "midspeed unlock not received");
        Expect(!progressed.victoryReceived, "no victory yet");

        auto overflowing = ComputeProgression(slotData, {"Progressive Acc Unlock", "Progressive Acc Unlock", "Progressive Acc Unlock"});
        Expect(IsIdentUnlocked(overflowing, "HHH_Standard_3"), "acc unlock opens node 8");
        ExpectEqual(overflowing.unlockedIdents.size(), size_t(5), "unlocks beyond category size are ignored");

        Expect(ComputeProgression(slotData, {"Victory"}).victoryReceived, "victory item detected");
    }

    void TestPresetProgression() {
        auto slotData = ParseJson(categorySlotDataJson);
        slotData.gameMode = GameMode::PresetPass;

        Expect(ComputeProgression(slotData, {}).unlockedIdents.empty(), "preset modes start with nothing");
        auto progressed = ComputeProgression(slotData, {"Progressive Song Unlock", "Progressive Song Unlock"});
        ExpectEqual(progressed.unlockedIdents, std::vector<std::string>({"BBB_Standard_3", "CCC_Standard_2"}), "song unlocks open nodes in order");
    }

    void TestVisibilityAndMatching() {
        auto slotData = ParseJson(categorySlotDataJson);
        auto progression = ComputeProgression(slotData, {});

        Expect(IsMapVisible(progression, "OST_100Bills"), "official map visible by prefix");
        Expect(IsMapVisible(progression, "1a2b"), "visibility is case-insensitive");
        Expect(!IsMapVisible(progression, "1A2"), "partial ids do not match");

        ExpectEqual(FindNodeForIdent(slotData, "EEE_OneSaber_1"), std::optional<uint32_t>(4), "node lookup");
        ExpectEqual(FindNodeForIdent(slotData, "eee_onesaber_1"), std::optional<uint32_t>(4), "node lookup ignores case");
        Expect(!FindNodeForIdent(slotData, "EEE_Standard_1").has_value(), "other characteristics do not match");
    }

    void TestLocations() {
        auto slotData = ParseJson(categorySlotDataJson);

        ExpectEqual(LocationsForClear(slotData, 4, GradeIndexFromRankName("A")), std::vector<int64_t>({2007, 2008, 2009}), "category location ids");
        ExpectEqual(LocationsForClear(slotData, 0, GradeIndexFromRankName("C")), std::vector<int64_t>({1001}), "first category, first node");
        ExpectEqual(LocationsForClear(slotData, 8, GradeIndexFromRankName("SS")), std::vector<int64_t>({4007, 4008, 4009, 4010, 4011}), "acc category location ids");
        Expect(LocationsForClear(slotData, 4, GradeIndexFromRankName("D")).empty(), "below C sends nothing");
        Expect(LocationsForClear(slotData, 99, 4).empty(), "nodes outside every category send nothing");

        slotData.gameMode = GameMode::PresetAccuracy;
        ExpectEqual(LocationsForClear(slotData, 5, GradeIndexFromRankName("S")), std::vector<int64_t>({31, 32, 33, 34}), "accuracy location ids");

        slotData.gameMode = GameMode::PresetPass;
        ExpectEqual(LocationsForClear(slotData, 5, GradeIndexFromRankName("E")), std::vector<int64_t>({5}), "pass location id is the node");

        ExpectEqual(GradeIndexFromRankName("SSS"), 4, "SSS counts as SS");
    }

    void TestIdents() {
        ExpectEqual(CustomMapIdentifier("01a2b"), std::string("1A2B"), "BeatSaver key formatting");
        ExpectEqual(OfficialMapIdentifier("100Bills"), std::string("OST_100Bills"), "official identifier");
        ExpectEqual(OfficialMapIdentifier("level_Escape"), std::string("OST_Escape"), "legacy prefix stripped");
        ExpectEqual(HashFromCustomLevelId("custom_level_abc123 WIP"), std::string("ABC123"), "custom hash extraction");

        auto official = ParseIdent("OST_Some_Level_OneSaber_3");
        Expect(official.has_value() && official->isOfficial, "official ident parsed");
        ExpectEqual(official->officialLevelId, std::string("Some_Level"), "underscores in level id preserved");
        ExpectEqual(official->characteristic, std::string("OneSaber"), "characteristic parsed");
        ExpectEqual(official->difficulty, 3, "difficulty parsed");

        auto custom = ParseIdent("1A2B_Standard_4");
        Expect(custom.has_value() && !custom->isOfficial, "custom ident parsed");
        ExpectEqual(custom->beatSaverKey, std::string("1a2b"), "BeatSaver key lowercased");

        Expect(!ParseIdent("nonsense").has_value(), "malformed ident rejected");
        Expect(!ParseIdent("A_Standard_X").has_value(), "non-numeric difficulty rejected");

        LevelIdentity identity{"1A2B", "Standard", 4};
        ExpectEqual(identity.ToIdent(), std::string("1A2B_Standard_4"), "ident formatting");
    }
}

int main() {
    TestSlotDataParsing();
    TestCategoryProgression();
    TestPresetProgression();
    TestVisibilityAndMatching();
    TestLocations();
    TestIdents();

    if (failures > 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "progression tests passed\n";
    return EXIT_SUCCESS;
}
