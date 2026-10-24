#include "SettingsStore.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "Logger.hpp"
#include "ModPaths.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace BeatSaberQuestAP::SettingsStore {

    Archipelago::ConnectionSettings LoadConnectionSettings() {
        Archipelago::ConnectionSettings settings;
        settings.host = "archipelago.gg";

        std::ifstream stream(ModPaths::ConnectionSettingsFile());
        if (!stream)
            return settings;
        std::stringstream contents;
        contents << stream.rdbuf();

        rapidjson::Document document;
        document.Parse(contents.str().c_str());
        if (document.HasParseError() || !document.IsObject()) {
            logger.warn("Ignoring unreadable connection settings");
            return settings;
        }
        if (document.HasMember("host") && document["host"].IsString())
            settings.host = document["host"].GetString();
        if (document.HasMember("port") && document["port"].IsInt())
            settings.port = document["port"].GetInt();
        if (document.HasMember("slot") && document["slot"].IsString())
            settings.slotName = document["slot"].GetString();
        return settings;
    }

    void SaveConnectionSettings(Archipelago::ConnectionSettings const& settings) {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("host");
        writer.String(settings.host.c_str());
        writer.Key("port");
        writer.Int(settings.port);
        writer.Key("slot");
        writer.String(settings.slotName.c_str());
        writer.EndObject();

        std::error_code ignoredError;
        std::filesystem::create_directories(ModPaths::DataDirectory(), ignoredError);
        std::ofstream stream(ModPaths::ConnectionSettingsFile(), std::ios::trunc);
        stream << buffer.GetString();
        if (!stream)
            logger.warn("Could not save connection settings");
    }
}
