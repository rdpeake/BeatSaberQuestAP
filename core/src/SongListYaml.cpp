#include "archipelago/SongListYaml.hpp"

#include <algorithm>
#include <set>
#include <sstream>

#include "archipelago/LevelIdentity.hpp"

namespace Archipelago {

    namespace {
        constexpr char const* difficultyNames[] = {"Easy", "Normal", "Hard", "Expert", "ExpertPlus"};
        // characteristics the apworld's Songs schema accepts
        constexpr char const* supportedCharacteristics[] = {"Standard", "OneSaber", "NoArrows", "90Degree", "360Degree", "Lightshow", "Lawless"};

        bool IsSupported(ExportedDifficulty const& difficulty) {
            bool knownCharacteristic = std::any_of(std::begin(supportedCharacteristics), std::end(supportedCharacteristics), [&](char const* name) {
                return difficulty.characteristic == name;
            });
            return knownCharacteristic && difficulty.difficulty >= 0 && difficulty.difficulty <= 4;
        }

        std::string QuoteYaml(std::string const& text) {
            std::string quoted = "\"";
            for (char character : text) {
                if (character == '"' || character == '\\')
                    quoted += '\\';
                if (character == '\n' || character == '\r' || character == '\t')
                    character = ' ';
                quoted += character;
            }
            return quoted + "\"";
        }

        std::string DescribeDifficulty(ExportedDifficulty const& difficulty) {
            return difficulty.characteristic + " " + difficultyNames[difficulty.difficulty] + " (" + std::to_string(difficulty.difficulty) + ")";
        }
    }

    ExportedDifficulty ChooseExportDifficulty(std::vector<ExportedDifficulty> const& difficulties) {
        ExportedDifficulty chosen;
        bool found = false;
        for (auto const& candidate : difficulties) {
            if (!IsSupported(candidate) || candidate.characteristic != "Standard")
                continue;
            if (!found || candidate.difficulty > chosen.difficulty)
                chosen = candidate;
            found = true;
        }
        if (found)
            return chosen;
        for (auto const& candidate : difficulties) {
            if (!IsSupported(candidate))
                continue;
            if (!found)
                chosen = candidate;
            else if (candidate.characteristic == chosen.characteristic && candidate.difficulty > chosen.difficulty)
                chosen = candidate;
            found = true;
        }
        return chosen;
    }

    std::string BuildSongsYaml(std::vector<ExportedSong> songs, std::string const& headerComment) {
        std::sort(songs.begin(), songs.end(), [](ExportedSong const& left, ExportedSong const& right) {
            if (left.isOfficial != right.isOfficial)
                return left.isOfficial;
            return ToLower(left.name) < ToLower(right.name);
        });

        std::ostringstream yaml;
        std::istringstream header(headerComment);
        for (std::string line; std::getline(header, line);)
            yaml << "# " << line << "\n";
        yaml << "Beat Saber:\n";
        yaml << "  # songs are only used by the presetPass and presetAccuracy game modes\n";
        yaml << "  songs:\n";

        std::set<std::string> usedNames;
        for (auto const& song : songs) {
            std::vector<ExportedDifficulty> usable;
            std::copy_if(song.difficulties.begin(), song.difficulties.end(), std::back_inserter(usable), IsSupported);
            if (usable.empty() || song.levelId.empty())
                continue;
            auto chosen = ChooseExportDifficulty(usable);

            // the songs option is a dictionary, so repeated titles need telling apart
            std::string name = song.name.empty() ? song.levelId : song.name;
            if (!usedNames.insert(ToLower(name)).second) {
                name += " [" + song.levelId + "]";
                usedNames.insert(ToLower(name));
            }

            yaml << "    " << QuoteYaml(name) << ":\n";
            yaml << "      levelid: " << QuoteYaml(song.levelId) << "\n";
            yaml << "      difficulty: " << chosen.difficulty << "\n";
            yaml << "      characteristic: " << chosen.characteristic << "\n";
            yaml << "      is_official: " << (song.isOfficial ? "true" : "false") << "\n";
            if (usable.size() > 1) {
                yaml << "      # also available:";
                bool first = true;
                for (auto const& difficulty : usable) {
                    if (difficulty.characteristic == chosen.characteristic && difficulty.difficulty == chosen.difficulty)
                        continue;
                    yaml << (first ? " " : ", ") << DescribeDifficulty(difficulty);
                    first = false;
                }
                yaml << "\n";
            }
        }
        return yaml.str();
    }
}
