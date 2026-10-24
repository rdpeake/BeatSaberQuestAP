#include "archipelago/LevelIdentity.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>

namespace Archipelago {

    std::string ToUpper(std::string_view text) {
        std::string upper(text);
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
        return upper;
    }

    std::string ToLower(std::string_view text) {
        std::string lower(text);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        return lower;
    }

    bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) {
        if (prefix.size() > text.size())
            return false;
        return std::equal(prefix.begin(), prefix.end(), text.begin(), [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
    }

    bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
        return left.size() == right.size() && StartsWithIgnoreCase(left, right);
    }

    namespace {
        constexpr char const* difficultyNames[] = {"Easy", "Normal", "Hard", "Expert", "ExpertPlus"};
        constexpr char const* difficultyLabels[] = {"Easy", "Normal", "Hard", "Expert", "Expert+"};

        bool IsKnownDifficulty(int difficulty) {
            return difficulty >= 0 && difficulty < static_cast<int>(std::size(difficultyNames));
        }
    }

    std::string DifficultyName(int difficulty) {
        return IsKnownDifficulty(difficulty) ? difficultyNames[difficulty] : std::to_string(difficulty);
    }

    std::string DescribeDifficulty(MapDifficulty const& mapDifficulty) {
        std::string label = IsKnownDifficulty(mapDifficulty.difficulty) ? difficultyLabels[mapDifficulty.difficulty] : std::to_string(mapDifficulty.difficulty);
        return mapDifficulty.characteristic + " " + label;
    }

    std::string MakeIdent(std::string_view mapIdentifier, std::string_view characteristic, int difficulty) {
        return std::string(mapIdentifier) + "_" + std::string(characteristic) + "_" + std::to_string(difficulty);
    }

    bool IsCustomLevelId(std::string_view levelId) {
        return StartsWithIgnoreCase(levelId, customLevelPrefix);
    }

    std::string HashFromCustomLevelId(std::string_view levelId) {
        std::string_view hash = levelId.substr(customLevelPrefix.size());
        auto suffixStart = hash.find(' ');
        if (suffixStart != std::string_view::npos)
            hash = hash.substr(0, suffixStart);
        return ToUpper(hash);
    }

    std::string OfficialMapIdentifier(std::string_view levelId) {
        constexpr std::string_view legacyPrefix = "level_";
        if (StartsWithIgnoreCase(levelId, legacyPrefix))
            levelId.remove_prefix(legacyPrefix.size());
        return std::string(officialIdentPrefix) + std::string(levelId);
    }

    std::string OfficialSongLevelId(std::string_view levelId) {
        return OfficialMapIdentifier(levelId).substr(officialIdentPrefix.size());
    }

    std::optional<ParsedIdent> ParseIdent(std::string_view ident) {
        auto difficultySeparator = ident.rfind('_');
        if (difficultySeparator == std::string_view::npos || difficultySeparator == 0)
            return std::nullopt;
        auto characteristicSeparator = ident.rfind('_', difficultySeparator - 1);
        if (characteristicSeparator == std::string_view::npos || characteristicSeparator == 0)
            return std::nullopt;

        ParsedIdent parsed;
        std::string_view difficultyText = ident.substr(difficultySeparator + 1);
        if (difficultyText.empty() || !std::all_of(difficultyText.begin(), difficultyText.end(), [](unsigned char c) { return std::isdigit(c); }))
            return std::nullopt;
        parsed.difficulty = std::stoi(std::string(difficultyText));
        parsed.characteristic = std::string(ident.substr(characteristicSeparator + 1, difficultySeparator - characteristicSeparator - 1));
        parsed.mapIdentifier = std::string(ident.substr(0, characteristicSeparator));

        if (StartsWithIgnoreCase(parsed.mapIdentifier, officialIdentPrefix)) {
            parsed.isOfficial = true;
            parsed.officialLevelId = parsed.mapIdentifier.substr(officialIdentPrefix.size());
        } else {
            parsed.beatSaverKey = ToLower(parsed.mapIdentifier);
        }
        return parsed;
    }
}
