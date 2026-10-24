#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "archipelago/LevelIdentity.hpp"
#include "archipelago/ProgressionRules.hpp"
#include "archipelago/SeedPlaylists.hpp"
#include "archipelago/SlotData.hpp"
#include "archipelago/SongListYaml.hpp"
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

    // shaped like fill_slot_data in the v0.1.1 apworld (randomizedAccuracy, threshold A)
    constexpr char const* randomizedSlotDataJson = R"({
        "DeathLink": 1,
        "campaign_name": "AP Campaign, Seed 123",
        "songs": {"0": {"levelid": "aaa", "name": "Speed One"}, "4": {"levelid": "eee", "name": "Tech Two"}},
        "node_to_keystr": {"0": "aaa_Standard_4", "1": "bbb_Standard_3", "2": "ccc_Standard_2",
                           "3": "ddd_Standard_4", "4": "eee_Standard_4",
                           "5": "fff_Standard_2", "6": "ggg_Standard_4",
                           "7": "1a2b_Standard_4", "8": "hhh_Standard_3"},
        "keystr_to_node": {"aaa_Standard_4": 0, "bbb_Standard_3": 1, "ccc_Standard_2": 2,
                           "ddd_Standard_4": 3, "eee_Standard_4": 4,
                           "fff_Standard_2": 5, "ggg_Standard_4": 6,
                           "1a2b_Standard_4": 7, "hhh_Standard_3": 8},
        "start_songs": [0, 3, 5, 7],
        "game_mode": 1,
        "map_type_counts": [3, 2, 2, 2],
        "num_grades": 3,
        "location_name_to_mnemonic": {},
        "lock_goal_map": 1,
        "goal_node_id": 2,
        "bloq_keys_required": 2
    })";

    // presetPass with the default song list: map_type_counts is null and node 0 starts unlocked
    constexpr char const* presetSlotDataJson = R"({
        "DeathLink": 0,
        "campaign_name": "AP Campaign, Seed 456",
        "songs": {},
        "node_to_keystr": {"0": "4e0e1_Standard_4", "1": "OST_100Bills_Standard_2", "2": "42e3a_Standard_4"},
        "keystr_to_node": {"4e0e1_Standard_4": 0, "OST_100Bills_Standard_2": 1, "42e3a_Standard_4": 2},
        "start_songs": [0],
        "game_mode": 4,
        "map_type_counts": null,
        "num_grades": 1,
        "location_name_to_mnemonic": {},
        "lock_goal_map": 0,
        "goal_node_id": 2,
        "bloq_keys_required": 10
    })";

    SlotData ParseJson(char const* json) {
        rapidjson::Document document;
        document.Parse(json);
        return ParseSlotData(document);
    }

    void TestSlotDataParsing() {
        auto slotData = ParseJson(randomizedSlotDataJson);
        Expect(slotData.deathLinkEnabled, "DeathLink 1 enables death link");
        Expect(slotData.gameMode == GameMode::RandomizedAccuracy, "numeric game mode");
        ExpectEqual(slotData.gradeCount, 3, "grade count");
        ExpectEqual(slotData.mapTypeCounts, std::vector<int>({3, 2, 2, 2}), "map type counts");
        Expect(slotData.lockGoalMap && slotData.goalNode == std::optional<uint32_t>(2) && slotData.bloqKeysRequired == 2, "goal lock settings");
        ExpectEqual(slotData.songNames.at(4), std::string("Tech Two"), "song names");

        auto preset = ParseJson(presetSlotDataJson);
        Expect(preset.gameMode == GameMode::PresetPass, "preset pass mode");
        Expect(preset.mapTypeCounts.empty(), "null map type counts accepted in preset modes");

        auto randomizedGroups = GroupSeedNodes(slotData);
        Expect(randomizedGroups.size() == 4 && randomizedGroups[1].fileStem == "Tech", "one playlist per map type");
        ExpectEqual(randomizedGroups[1].nodes, std::vector<uint32_t>({3, 4}), "tech playlist holds the tech nodes in order");
        Expect(randomizedGroups[0].title.starts_with("Speed - "), "titled like the apworld's playlists");
        auto presetGroups = GroupSeedNodes(preset);
        Expect(presetGroups.size() == 1 && presetGroups[0].nodes.size() == preset.nodeToIdent.size(), "preset seeds get one playlist");

        auto embedded = ParseJson(R"({
            "node_to_keystr": "{\"1\": \"AAA_Standard_4\"}",
            "keystr_to_node": "{\"AAA_Standard_4\": 1}",
            "start_songs": "[1]",
            "game_mode": "option_presetAccuracy", "num_grades": "5"
        })");
        Expect(embedded.gameMode == GameMode::PresetAccuracy, "option_ game mode names accepted");
        ExpectEqual(embedded.nodeToIdent.at(1), std::string("AAA_Standard_4"), "JSON-in-string dictionary is decoded");
        ExpectEqual(embedded.gradeCount, 5, "numeric string is decoded");

        bool threw = false;
        try {
            ParseJson(R"({"game_mode": 1, "node_to_keystr": {}, "keystr_to_node": {}, "start_songs": []})");
        } catch (std::exception const&) {
            threw = true;
        }
        Expect(threw, "randomized modes need map type counts");
    }

    void TestRandomizedProgression() {
        auto slotData = ParseJson(randomizedSlotDataJson);

        auto start = ComputeProgression(slotData, {});
        ExpectEqual(start.unlockedNodes, std::set<uint32_t>({0, 3, 5, 7}), "start songs unlocked");
        Expect(!start.goalMapUnlocked, "goal locked without keys");

        auto progressed = ComputeProgression(slotData, {"Progressive Speed Unlock", "Progressive Tech Unlock", "Nothing", "Progressive Speed Unlock"});
        Expect(IsIdentUnlocked(progressed, "bbb_Standard_3"), "first speed unlock opens speed song 1");
        Expect(IsIdentUnlocked(progressed, "ccc_Standard_2"), "second speed unlock opens speed song 2");
        Expect(IsIdentUnlocked(progressed, "eee_Standard_4"), "tech unlock opens tech song 1 after the speed offset");
        Expect(!IsIdentUnlocked(progressed, "ggg_Standard_4"), "midspeed unlock not received");

        auto overflowing = ComputeProgression(slotData, {"Progressive Accuracy Unlock", "Progressive Acc Unlock", "Progressive Accuracy Unlock"});
        Expect(IsIdentUnlocked(overflowing, "hhh_Standard_3"), "accuracy unlock opens acc song 1");
        ExpectEqual(overflowing.unlockedNodes.size(), size_t(5), "unlocks beyond a category do not spill into the next");

        auto keys = ComputeProgression(slotData, {"Bloq Key", "Bloq Key"});
        Expect(keys.goalMapUnlocked && keys.bloqKeys == 2, "enough keys unlock the goal");
        Expect(IsIdentUnlocked(keys, "ccc_Standard_2"), "keys alone make the goal song playable");

        Expect(ComputeProgression(slotData, {"Victory"}).victoryReceived, "victory item detected");
    }

    void TestPresetProgression() {
        auto slotData = ParseJson(presetSlotDataJson);

        auto start = ComputeProgression(slotData, {});
        ExpectEqual(start.unlockedIdents, std::vector<std::string>({"4e0e1_Standard_4"}), "preset starts with node 0");

        auto progressed = ComputeProgression(slotData, {"Progressive Song Unlock", "Progressive Speed Unlock"});
        Expect(IsIdentUnlocked(progressed, "OST_100Bills_Standard_2"), "song unlock opens node 1");
        Expect(!IsIdentUnlocked(progressed, "42e3a_Standard_4"), "category items do nothing in preset modes");
    }

    void TestVisibilityAndMatching() {
        auto slotData = ParseJson(randomizedSlotDataJson);
        auto progression = ComputeProgression(slotData, {});

        Expect(IsMapVisible(progression, "1A2B"), "visibility ignores case");
        Expect(!IsMapVisible(progression, "1A2"), "partial ids do not match");

        ExpectEqual(FindNodeForIdent(slotData, "EEE_STANDARD_4"), std::optional<uint32_t>(4), "node lookup ignores case");
        Expect(!FindNodeForIdent(slotData, "eee_OneSaber_4").has_value(), "other characteristics do not match");
        Expect(!IsIdentUnlocked(progression, "aaa_Standard_3"), "other difficulties of an unlocked song stay locked");
    }

    void TestRandomizedLocations() {
        auto slotData = ParseJson(randomizedSlotDataJson);
        auto progression = ComputeProgression(slotData, {});

        ExpectEqual(LocationsForClear(slotData, progression, 4, GradeIndexFromRankName("A")), std::vector<int64_t>({2007, 2008, 2009}), "tech song 1 at A");
        ExpectEqual(LocationsForNode(slotData, 4), std::vector<int64_t>({2007, 2008, 2009}), "every check of a node");
        ExpectEqual(LocationsForClear(slotData, progression, 0, GradeIndexFromRankName("C")), std::vector<int64_t>({1001}), "speed song 0 at C");
        ExpectEqual(LocationsForClear(slotData, progression, 8, GradeIndexFromRankName("SS")), std::vector<int64_t>({4007, 4008, 4009}), "grades above the threshold are not checks");
        Expect(LocationsForClear(slotData, progression, 4, GradeIndexFromRankName("D")).empty(), "below C sends nothing in accuracy mode");
        Expect(LocationsForClear(slotData, progression, 99, 4).empty(), "nodes outside every category send nothing");

        ExpectEqual(LocationsForClear(slotData, progression, 2, 4), std::vector<int64_t>({1013, 1014}), "goal check held back without keys");
        auto withKeys = ComputeProgression(slotData, {"Bloq Key", "Bloq Key"});
        ExpectEqual(LocationsForClear(slotData, withKeys, 2, 4), std::vector<int64_t>({1013, 1014, 1015}), "goal check sent with keys");

        slotData.gameMode = GameMode::RandomizedPass;
        ExpectEqual(LocationsForClear(slotData, progression, 6, GradeIndexFromRankName("E")), std::vector<int64_t>({3006}), "randomized pass id has no grade offset");
    }

    void TestPresetLocations() {
        auto slotData = ParseJson(presetSlotDataJson);
        auto progression = ComputeProgression(slotData, {});

        ExpectEqual(LocationsForClear(slotData, progression, 1, GradeIndexFromRankName("E")), std::vector<int64_t>({6}), "preset pass id is song index times six");
        ExpectEqual(LocationsForClear(slotData, progression, 0, 0), std::vector<int64_t>({0}), "first preset song");

        slotData.gameMode = GameMode::PresetAccuracy;
        slotData.gradeCount = 5;
        ExpectEqual(LocationsForClear(slotData, progression, 2, GradeIndexFromRankName("S")), std::vector<int64_t>({13, 14, 15, 16}), "preset accuracy ids");
        ExpectEqual(GradeIndexFromRankName("SSS"), 4, "SSS counts as SS");
    }

    void TestIdents() {
        ExpectEqual(CustomMapIdentifier("01a2b"), std::string("1A2B"), "BeatSaver key formatting");
        ExpectEqual(OfficialMapIdentifier("100Bills"), std::string("OST_100Bills"), "official identifier");
        ExpectEqual(OfficialMapIdentifier("level_Escape"), std::string("OST_Escape"), "legacy prefix stripped");
        ExpectEqual(OfficialSongLevelId("level_Escape"), std::string("Escape"), "yaml official level id");
        ExpectEqual(HashFromCustomLevelId("custom_level_abc123 WIP"), std::string("ABC123"), "custom hash extraction");

        auto official = ParseIdent("OST_Some_Level_OneSaber_3");
        Expect(official.has_value() && official->isOfficial, "official ident parsed");
        ExpectEqual(official->officialLevelId, std::string("Some_Level"), "underscores in level id preserved");
        ExpectEqual(official->characteristic, std::string("OneSaber"), "characteristic parsed");
        ExpectEqual(official->difficulty, 3, "difficulty parsed");

        auto custom = ParseIdent("4e0e1_Standard_4");
        Expect(custom.has_value() && !custom->isOfficial, "custom ident parsed");
        ExpectEqual(custom->beatSaverKey, std::string("4e0e1"), "BeatSaver key kept lowercase");

        Expect(!ParseIdent("nonsense").has_value(), "malformed ident rejected");
        Expect(!ParseIdent("A_Standard_X").has_value(), "non-numeric difficulty rejected");

        LevelIdentity identity{"1A2B", "Standard", 4};
        ExpectEqual(identity.ToIdent(), std::string("1A2B_Standard_4"), "ident formatting");
    }
}

namespace {
    void TestSongListExport() {
        std::vector<ExportedSong> songs = {
            {"Zebra \"Remix\" ", "1a2b", false, {{"Standard", 3}, {"Standard", 4}, {"OneSaber", 4}}},
            {"$100 Bills", "100Bills", true, {{"Standard", 2}, {"Standard", 4}}},
            {"Lawless Only", "3c3c", false, {{"Lawless", 1}, {"Lawless", 3}}},
            {"Zebra \"Remix\"", "4d4d", false, {{"Standard", 1}}},
            {"No Usable Difficulties", "5e5e", false, {{"MissingCharacteristic", 2}}},
        };
        std::string yaml = BuildSongsYaml(songs, "Exported test list\nsecond line");

        Expect(yaml.starts_with("# Exported test list\n# second line\nBeat Saber:\n"), "header comments and section");
        Expect(yaml.find("    \"$100 Bills\":\n      levelid: \"100Bills\"\n      difficulty: 4\n      characteristic: Standard\n      is_official: true\n") != std::string::npos, "official song uses hardest Standard difficulty");
        Expect(yaml.find("$100 Bills") < yaml.find("Lawless Only"), "official songs listed first");
        Expect(yaml.find("    \"Zebra \\\"Remix\\\"\":\n      levelid: \"1a2b\"") != std::string::npos, "quotes in names are escaped");
        Expect(yaml.find("# also available: Standard Expert (3), OneSaber ExpertPlus (4)") != std::string::npos, "other difficulties listed as a comment");
        Expect(yaml.find("    \"Zebra \\\"Remix\\\" [4d4d]\":") != std::string::npos, "duplicate titles get their key appended");
        Expect(yaml.find("levelid: \"3c3c\"\n      difficulty: 3\n      characteristic: Lawless") != std::string::npos, "falls back to another characteristic");
        Expect(yaml.find("5e5e") == std::string::npos, "songs with no supported difficulty are skipped");
    }
}

int main() {
    TestSongListExport();
    TestSlotDataParsing();
    TestRandomizedProgression();
    TestPresetProgression();
    TestVisibilityAndMatching();
    TestRandomizedLocations();
    TestPresetLocations();
    TestIdents();

    if (failures > 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "progression tests passed\n";
    return EXIT_SUCCESS;
}
