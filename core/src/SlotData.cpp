#include "archipelago/SlotData.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace Archipelago {

    namespace {
        rapidjson::Value const* FindValue(rapidjson::Value const& slotDataJson, char const* key) {
            auto member = slotDataJson.FindMember(key);
            if (member == slotDataJson.MemberEnd() || member->value.IsNull())
                return nullptr;
            return &member->value;
        }

        // slot data values may arrive as JSON or as JSON encoded inside a string
        void WithDecodedValue(rapidjson::Value const& value, std::function<void(rapidjson::Value const&)> const& consume) {
            if (value.IsString()) {
                rapidjson::Document embedded;
                embedded.Parse(value.GetString(), value.GetStringLength());
                bool isEmbeddedJson = !embedded.HasParseError() && (embedded.IsObject() || embedded.IsArray() || embedded.IsString());
                if (isEmbeddedJson) {
                    consume(embedded);
                    return;
                }
            }
            consume(value);
        }

        void WithRequiredValue(rapidjson::Value const& slotDataJson, char const* key, std::function<void(rapidjson::Value const&)> const& consume) {
            auto value = FindValue(slotDataJson, key);
            if (!value)
                throw std::runtime_error(std::string("slot data is missing '") + key + "'");
            WithDecodedValue(*value, consume);
        }

        void WithOptionalValue(rapidjson::Value const& slotDataJson, char const* key, std::function<void(rapidjson::Value const&)> const& consume) {
            if (auto value = FindValue(slotDataJson, key))
                WithDecodedValue(*value, consume);
        }

        int64_t ReadInteger(rapidjson::Value const& value, char const* key) {
            if (value.IsInt64())
                return value.GetInt64();
            if (value.IsBool())
                return value.GetBool() ? 1 : 0;
            if (value.IsDouble())
                return static_cast<int64_t>(value.GetDouble());
            if (value.IsString()) {
                try {
                    return std::stoll(value.GetString());
                } catch (std::exception const&) {
                }
            }
            throw std::runtime_error(std::string("slot data '") + key + "' is not a number");
        }

        std::string ReadString(rapidjson::Value const& value, char const* key) {
            if (value.IsString())
                return {value.GetString(), value.GetStringLength()};
            throw std::runtime_error(std::string("slot data '") + key + "' is not a string");
        }

        std::vector<int> ReadIntegerList(rapidjson::Value const& value, char const* key) {
            if (!value.IsArray())
                throw std::runtime_error(std::string("slot data '") + key + "' is not a list");
            std::vector<int> integers;
            for (auto const& element : value.GetArray())
                integers.push_back(static_cast<int>(ReadInteger(element, key)));
            return integers;
        }

        void RequireObject(rapidjson::Value const& value, char const* key) {
            if (!value.IsObject())
                throw std::runtime_error(std::string("slot data '") + key + "' is not a dictionary");
        }

        uint32_t ReadNodeKey(rapidjson::Value const& name, char const* key) {
            try {
                return static_cast<uint32_t>(std::stoul(name.GetString()));
            } catch (std::exception const&) {
                throw std::runtime_error(std::string("slot data '") + key + "' has a non-numeric node id");
            }
        }
    }

    bool IsRandomizedMode(GameMode gameMode) {
        return gameMode == GameMode::RandomizedPP || gameMode == GameMode::RandomizedAccuracy || gameMode == GameMode::RandomizedPass;
    }

    bool IsAccuracyMode(GameMode gameMode) {
        return gameMode == GameMode::RandomizedAccuracy || gameMode == GameMode::PresetAccuracy;
    }

    GameMode ParseGameMode(rapidjson::Value const& value) {
        if (value.IsString()) {
            std::string_view name = value.GetString();
            constexpr std::pair<std::string_view, GameMode> names[] = {
                {"option_randomizedPP", GameMode::RandomizedPP},
                {"option_randomizedAccuracy", GameMode::RandomizedAccuracy},
                {"option_randomizedPass", GameMode::RandomizedPass},
                {"option_presetAccuracy", GameMode::PresetAccuracy},
                {"option_presetPass", GameMode::PresetPass},
            };
            for (auto const& [optionName, gameMode] : names) {
                if (name == optionName || name == optionName.substr(7))
                    return gameMode;
            }
        }
        int64_t choice = ReadInteger(value, "game_mode");
        if (choice < 0 || choice > static_cast<int64_t>(GameMode::PresetPass))
            throw std::runtime_error("slot data 'game_mode' has unknown value " + std::to_string(choice));
        return static_cast<GameMode>(choice);
    }

    SlotData ParseSlotData(rapidjson::Value const& slotDataJson) {
        if (!slotDataJson.IsObject())
            throw std::runtime_error("slot data is not a dictionary");

        SlotData slotData;

        WithRequiredValue(slotDataJson, "game_mode", [&](auto const& value) {
            slotData.gameMode = ParseGameMode(value);
        });
        WithRequiredValue(slotDataJson, "node_to_keystr", [&](auto const& value) {
            RequireObject(value, "node_to_keystr");
            for (auto const& entry : value.GetObject())
                slotData.nodeToIdent[ReadNodeKey(entry.name, "node_to_keystr")] = ReadString(entry.value, "node_to_keystr");
        });
        WithRequiredValue(slotDataJson, "keystr_to_node", [&](auto const& value) {
            RequireObject(value, "keystr_to_node");
            for (auto const& entry : value.GetObject())
                slotData.identToNode[entry.name.GetString()] = static_cast<uint32_t>(ReadInteger(entry.value, "keystr_to_node"));
        });
        WithRequiredValue(slotDataJson, "start_songs", [&](auto const& value) {
            slotData.startNodes = ReadIntegerList(value, "start_songs");
        });

        WithOptionalValue(slotDataJson, "DeathLink", [&](auto const& value) {
            slotData.deathLinkEnabled = ReadInteger(value, "DeathLink") == 1;
        });
        WithOptionalValue(slotDataJson, "campaign_name", [&](auto const& value) {
            slotData.campaignName = ReadString(value, "campaign_name");
        });
        WithOptionalValue(slotDataJson, "num_grades", [&](auto const& value) {
            slotData.gradeCount = std::max<int>(1, static_cast<int>(ReadInteger(value, "num_grades")));
        });
        WithOptionalValue(slotDataJson, "map_type_counts", [&](auto const& value) {
            slotData.mapTypeCounts = ReadIntegerList(value, "map_type_counts");
        });
        WithOptionalValue(slotDataJson, "lock_goal_map", [&](auto const& value) {
            slotData.lockGoalMap = ReadInteger(value, "lock_goal_map") != 0;
        });
        WithOptionalValue(slotDataJson, "goal_node_id", [&](auto const& value) {
            slotData.goalNode = static_cast<uint32_t>(ReadInteger(value, "goal_node_id"));
        });
        WithOptionalValue(slotDataJson, "bloq_keys_required", [&](auto const& value) {
            slotData.bloqKeysRequired = static_cast<int>(ReadInteger(value, "bloq_keys_required"));
        });
        WithOptionalValue(slotDataJson, "songs", [&](auto const& value) {
            if (!value.IsObject())
                return;
            for (auto const& entry : value.GetObject()) {
                if (entry.value.IsObject() && entry.value.HasMember("name") && entry.value["name"].IsString())
                    slotData.songNames[ReadNodeKey(entry.name, "songs")] = entry.value["name"].GetString();
            }
        });
        WithOptionalValue(slotDataJson, "location_name_to_mnemonic", [&](auto const& value) {
            RequireObject(value, "location_name_to_mnemonic");
            for (auto const& entry : value.GetObject()) {
                if (entry.value.IsString())
                    slotData.locationNameToMnemonic[entry.name.GetString()] = entry.value.GetString();
            }
        });

        if (IsRandomizedMode(slotData.gameMode) && slotData.mapTypeCounts.size() < categoryCount)
            throw std::runtime_error("slot data 'map_type_counts' needs 4 categories in randomized modes");

        return slotData;
    }
}
