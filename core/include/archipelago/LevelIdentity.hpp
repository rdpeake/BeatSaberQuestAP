#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Archipelago {

    inline constexpr std::string_view customLevelPrefix = "custom_level_";
    inline constexpr std::string_view officialIdentPrefix = "OST_";

    // ident format shared with the PC client and the apworld: <map>_<characteristic>_<difficulty>
    // where <map> is the uppercase BeatSaver id for custom levels or OST_<levelId> for official ones
    struct LevelIdentity {
        std::string mapIdentifier;
        std::string characteristic;
        int difficulty = 0;

        std::string ToIdent() const;
    };

    bool IsCustomLevelId(std::string_view levelId);

    // uppercase hash from "custom_level_<hash>[ WIP]"
    std::string HashFromCustomLevelId(std::string_view levelId);

    std::string OfficialMapIdentifier(std::string_view levelId);
    // the form a YAML `songs:` entry uses, e.g. "100Bills"
    std::string OfficialSongLevelId(std::string_view levelId);

    // BeatSaver keys are lowercase hex; the PC client formats them as uppercase without leading zeros
    std::string CustomMapIdentifier(std::string_view beatSaverKey);

    struct ParsedIdent {
        bool isOfficial = false;
        std::string beatSaverKey;      // lowercase, custom maps only
        std::string officialLevelId;   // official maps only
        std::string mapIdentifier;
        std::string characteristic;
        int difficulty = 0;
    };

    // parses from the right so official level ids containing underscores survive
    std::optional<ParsedIdent> ParseIdent(std::string_view ident);

    bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix);
    bool EqualsIgnoreCase(std::string_view left, std::string_view right);
    std::string ToUpper(std::string_view text);
    std::string ToLower(std::string_view text);
}
