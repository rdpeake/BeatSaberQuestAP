#include "Content/ContentResolver.hpp"
#include "Content/PlaylistImporter.hpp"
#include "Content/SeedPlaylists.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "Session.hpp"
#include "UI/ArchipelagoMenu.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "scotland2/shared/modloader.h"
#include "songcore/shared/SongCore.hpp"

#define MOD_EXPORT_FUNC extern "C" __attribute__((visibility("default")))

using namespace BeatSaberQuestAP;

namespace {
    modloader::ModInfo modInfo{MOD_ID, VERSION, 0};
    bool importedInitialPatches = false;
}

MOD_EXPORT_FUNC void setup(CModInfo* info) {
    info->id = MOD_ID;
    info->version = VERSION;
    info->version_long = 0;
    modInfo.assign(*info);

    Paper::Logger::RegisterFileContextId(MOD_ID);
    logger.info("Setup complete");
}

MOD_EXPORT_FUNC void late_load() {
    il2cpp_functions::Init();

    Hooks::InstallLevelListFilter();
    Hooks::InstallLevelResults();
    Hooks::InstallDifficultyLock();
    Hooks::InstallMainMenuReady();
    Hooks::InstallCheckMarkers();

    ArchipelagoMenu::Register();

    Session::Instance().AddChangeListener([] {
        ArchipelagoMenu::Refresh();
        Hooks::RefreshLevelList();
        Hooks::RefreshDifficultyLock();
        SeedPlaylists::Sync();
    });

    Hooks::RunWhenMainMenuReady([] { Session::Instance().RestoreSavedSession(); });

    SongCore::API::Loading::GetSongsLoadedEvent().addCallback([](auto) {
        BSML::MainThreadScheduler::Schedule([] {
            if (!importedInitialPatches) {
                importedInitialPatches = true;
                PlaylistImporter::RemoveImportedPlaylists();
                PlaylistImporter::ImportPendingPatches();
            }
            ContentResolver::Instance().HandleSongsLoaded();
            SeedPlaylists::Sync();
        });
    });

    logger.info("Late load complete");
}
