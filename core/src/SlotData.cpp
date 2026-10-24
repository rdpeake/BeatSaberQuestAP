#include "archipelago/SlotData.hpp"

#include <functional>
#include <stdexcept>

namespace Archipelago {

    namespace {
        // slot data values may arrive as JSON or as JSON encoded inside a string
        void WithDecodedValue(rapidjson::Value const& slotDataJson, char const* key, std::function<void(rapidjson::Value const&)> const& consume) {
            auto member = slotDataJson.FindMember(key);
            if (member == slotDataJson.MemberEnd())
                throw std::runtime_error(std::string("slot data is missing '") + key + "'");

            rapidjson::Value const& value = member->value;
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

        int64_t ReadInteger(rapidjson::Value const& value, char const* key) {
            if (value.IsInt64())
                return value.GetInt64();
            if (value.IsBool())
                return value.GetBool() ? 1 : 0;
            if (value.IsDouble())
                return static_cast<int64_t>(value.GetDouble());
            if (value.IsString())
                return std::stoll(value.GetString());
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
    }

    GameMode GameModeFromName(std::string_view gameModeName) {
        if (gameModeName == "option_presetPass")
            return GameMode::PresetPass;
        if (gameModeName == "option_presetAccuracy")
            return GameMode::PresetAccuracy;
        return GameMode::Categories;
    }

    SlotData ParseSlotData(rapidjson::Value const& slotDataJson) {
        if (!slotDataJson.IsObject())
            throw std::runtime_error("slot data is not a dictionary");

        SlotData slotData;

        WithDecodedValue(slotDataJson, "DeathLink", [&](auto const& value) {
            slotData.deathLinkEnabled = ReadInteger(value, "DeathLink") == 1;
        });
        WithDecodedValue(slotDataJson, "campaign_name", [&](auto const& value) {
            slotData.campaignName = ReadString(value, "campaign_name");
        });
        WithDecodedValue(slotDataJson, "node_to_keystr", [&](auto const& value) {
            RequireObject(value, "node_to_keystr");
            for (auto const& entry : value.GetObject())
                slotData.nodeToIdent[static_cast<uint32_t>(std::stoul(entry.name.GetString()))] = ReadString(entry.value, "node_to_keystr");
        });
        WithDecodedValue(slotDataJson, "keystr_to_node", [&](auto const& value) {
            RequireObject(value, "keystr_to_node");
            for (auto const& entry : value.GetObject())
                slotData.identToNode[entry.name.GetString()] = static_cast<uint32_t>(ReadInteger(entry.value, "keystr_to_node"));
        });
        WithDecodedValue(slotDataJson, "start_songs", [&](auto const& value) {
            slotData.startNodes = ReadIntegerList(value, "start_songs");
        });
        WithDecodedValue(slotDataJson, "map_type_counts", [&](auto const& value) {
            slotData.mapTypeCounts = ReadIntegerList(value, "map_type_counts");
        });
        WithDecodedValue(slotDataJson, "game_mode", [&](auto const& value) {
            slotData.gameModeName = ReadString(value, "game_mode");
        });
        WithDecodedValue(slotDataJson, "num_grades", [&](auto const& value) {
            slotData.gradeCount = static_cast<int>(ReadInteger(value, "num_grades"));
        });
        WithDecodedValue(slotDataJson, "location_name_to_mnemonic", [&](auto const& value) {
            RequireObject(value, "location_name_to_mnemonic");
            for (auto const& entry : value.GetObject())
                slotData.locationNameToMnemonic[entry.name.GetString()] = ReadString(entry.value, "location_name_to_mnemonic");
        });

        slotData.gameMode = GameModeFromName(slotData.gameModeName);
        if (slotData.gameMode == GameMode::Categories && slotData.mapTypeCounts.size() < categoryCount)
            throw std::runtime_error("slot data 'map_type_counts' has fewer than 4 categories");

        return slotData;
    }
}
