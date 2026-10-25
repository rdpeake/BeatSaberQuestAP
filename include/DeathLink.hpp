#pragma once

namespace BeatSaberQuestAP::DeathLink {
    // fails the level being played, ignoring No Fail; does nothing in the menu
    void ForceFailCurrentLevel();

    // true once after a level failed because of a received DeathLink, so it is not echoed back
    bool ConsumeFailFromDeathLink();
}
