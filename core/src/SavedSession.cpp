#include "archipelago/SavedSession.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Archipelago {

    std::optional<SavedSession> LoadSavedSession(std::string const& filePath) {
        std::ifstream stream(filePath);
        if (!stream)
            return std::nullopt;
        std::stringstream contents;
        contents << stream.rdbuf();

        rapidjson::Document document;
        document.Parse(contents.str().c_str());
        if (document.HasParseError() || !document.IsObject())
            return std::nullopt;

        auto readString = [&](char const* key) -> std::optional<std::string> {
            if (!document.HasMember(key) || !document[key].IsString())
                return std::nullopt;
            return std::string(document[key].GetString(), document[key].GetStringLength());
        };

        SavedSession session;
        auto host = readString("host");
        auto slotName = readString("slot");
        auto slotDataJson = readString("slotData");
        if (!host || !slotName || !slotDataJson || !document.HasMember("port") || !document["port"].IsInt())
            return std::nullopt;
        session.host = *host;
        session.slotName = *slotName;
        session.slotDataJson = *slotDataJson;
        session.port = document["port"].GetInt();
        session.passwordRequired = document.HasMember("passwordRequired") && document["passwordRequired"].IsBool() && document["passwordRequired"].GetBool();

        if (document.HasMember("receivedItems") && document["receivedItems"].IsArray()) {
            for (auto const& itemName : document["receivedItems"].GetArray()) {
                if (itemName.IsString())
                    session.receivedItemNames.emplace_back(itemName.GetString());
            }
        }
        if (document.HasMember("unconfirmedLocations") && document["unconfirmedLocations"].IsArray()) {
            for (auto const& location : document["unconfirmedLocations"].GetArray()) {
                if (location.IsInt64())
                    session.unconfirmedLocations.push_back(location.GetInt64());
            }
        }
        return session;
    }

    bool WriteSavedSession(std::string const& filePath, SavedSession const& session) {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("host");
        writer.String(session.host.c_str(), static_cast<rapidjson::SizeType>(session.host.size()));
        writer.Key("port");
        writer.Int(session.port);
        writer.Key("slot");
        writer.String(session.slotName.c_str(), static_cast<rapidjson::SizeType>(session.slotName.size()));
        writer.Key("passwordRequired");
        writer.Bool(session.passwordRequired);
        writer.Key("slotData");
        writer.String(session.slotDataJson.c_str(), static_cast<rapidjson::SizeType>(session.slotDataJson.size()));
        writer.Key("receivedItems");
        writer.StartArray();
        for (auto const& itemName : session.receivedItemNames)
            writer.String(itemName.c_str(), static_cast<rapidjson::SizeType>(itemName.size()));
        writer.EndArray();
        writer.Key("unconfirmedLocations");
        writer.StartArray();
        for (auto location : session.unconfirmedLocations)
            writer.Int64(location);
        writer.EndArray();
        writer.EndObject();

        std::error_code ignoredError;
        std::filesystem::create_directories(std::filesystem::path(filePath).parent_path(), ignoredError);
        // write then rename so a crash mid-save cannot leave a half-written session
        std::string temporaryPath = filePath + ".tmp";
        {
            std::ofstream stream(temporaryPath, std::ios::trunc);
            stream << buffer.GetString();
            if (!stream)
                return false;
        }
        std::filesystem::rename(temporaryPath, filePath, ignoredError);
        return !ignoredError;
    }

    void DeleteSavedSession(std::string const& filePath) {
        std::error_code ignoredError;
        std::filesystem::remove(filePath, ignoredError);
    }
}
