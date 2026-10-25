#include "GlobalNamespace/MainMenuViewController.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"

using namespace GlobalNamespace;

namespace {
    bool mainMenuReady = false;
    std::vector<std::function<void()>> pendingUntilMainMenu;
}

// game services such as Zenject bindings and the content model only exist once the main menu has shown
MAKE_HOOK_MATCH(
    MainMenuViewController_DidActivate,
    &MainMenuViewController::DidActivate,
    void,
    MainMenuViewController* self,
    bool firstActivation,
    bool addedToHierarchy,
    bool screenSystemEnabling
) {
    MainMenuViewController_DidActivate(self, firstActivation, addedToHierarchy, screenSystemEnabling);
    if (mainMenuReady)
        return;
    mainMenuReady = true;
    BeatSaberQuestAP::logger.info("Main menu ready");
    auto pending = std::move(pendingUntilMainMenu);
    pendingUntilMainMenu.clear();
    for (auto& work : pending)
        work();
}

namespace BeatSaberQuestAP::Hooks {
    void InstallMainMenuReady() {
        INSTALL_HOOK(logger, MainMenuViewController_DidActivate);
    }

    bool IsMainMenuReady() {
        return mainMenuReady;
    }

    void RunWhenMainMenuReady(std::function<void()> work) {
        if (mainMenuReady)
            work();
        else
            pendingUntilMainMenu.push_back(std::move(work));
    }
}
