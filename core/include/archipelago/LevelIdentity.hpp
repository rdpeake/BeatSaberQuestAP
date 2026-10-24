#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Archipelago {

    inline constexpr std::string_view customLevelPrefix = "custom_level_";
    inline constexpr std::string_view officialIdentPrefix = "OST_";

    struct MapDifficulty {
        std::string characteristic;
        // 0 Easy to 4 Expert+
        int difficulty = 0;

        bool operator==(MapDifficulty const&) const = default;
    };

    // as maps and playlists store it, e.g. "ExpertPlus"
    std::string DifficultyName(int difficulty);
    // for players, e.g. "Standard Expert+"
    std::string DescribeDifficulty(MapDifficulty const& mapDifficulty);

    // ident format shared with the PC client and the apworld: <map>_<characteristic>_<difficulty>
    // where <map> is the BeatSaver key for custom levels or OST_<levelId> for official ones; compared ignoring case
    std::string MakeIdent(std::string_view mapIdentifier, std::string_view characteristic, int difficulty);

    bool IsCustomLevelId(std::string_view levelId);

    // uppercase hash from "custom_level_<hash>[ WIP]"
    std::string HashFromCustomLevelId(std::string_view levelId);

    std::string OfficialMapIdentifier(std::string_view levelId);
    // the form a YAML `songs:` entry uses, e.g. "100Bills"
    std::string OfficialSongLevelId(std::string_view levelId);

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
