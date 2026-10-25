#include "Content/ContentResolver.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/LevelCollectionTableView.hpp"
#include "Hooks/Hooks.hpp"
#include "Logger.hpp"
#include "Session.hpp"
#include "System/Collections/Generic/HashSet_1.hpp"
#include "System/Collections/Generic/IReadOnlyCollection_1.hpp"
#include "System/Collections/Generic/IReadOnlyList_1.hpp"
#include "archipelago/ProgressionRules.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"

using namespace GlobalNamespace;
using LevelList = System::Collections::Generic::IReadOnlyList_1<BeatmapLevel*>;
using LevelCollection = System::Collections::Generic::IReadOnlyCollection_1<BeatmapLevel*>;
using FavoriteIds = System::Collections::Generic::HashSet_1<StringW>;

namespace {
    struct LastLevelListRequest {
        UnityW<LevelCollectionTableView> table;
        SafePtr<LevelList> unfilteredLevels;
        SafePtr<FavoriteIds> favoriteLevelIds;
        bool beatmapLevelsAreSorted = false;
        bool sortBeatmapLevels = false;
    };

    LastLevelListRequest lastRequest;
    bool refreshing = false;
    // unlocks that arrive while the list is hidden, such as mid-song, apply when it shows again
    bool refreshWhenShown = false;

    LevelList* FilterToUnlockedLevels(LevelList* beatmapLevels) {
        auto& session = BeatSaberQuestAP::Session::Instance();
        if (!beatmapLevels || !session.IsInArchipelagoMode())
            return beatmapLevels;

        auto const& progression = session.GetProgression();
        auto const& resolver = BeatSaberQuestAP::ContentResolver::Instance();
        auto unlockedLevels = ListW<BeatmapLevel*>::New();

        int levelCount = static_cast<LevelCollection*>(*beatmapLevels)->Count;
        for (int index = 0; index < levelCount; index++) {
            auto level = beatmapLevels->get_Item(index);
            if (!level)
                continue;
            auto mapIdentifier = resolver.MapIdentifierForLevelId(static_cast<std::string>(level->levelID));
            if (mapIdentifier && Archipelago::IsMapVisible(progression, *mapIdentifier))
                unlockedLevels->Add(level);
        }
        return reinterpret_cast<LevelList*>(unlockedLevels.convert());
    }
}

MAKE_HOOK_MATCH(
    LevelCollectionTableView_SetData,
    &LevelCollectionTableView::SetData,
    void,
    LevelCollectionTableView* self,
    LevelList* beatmapLevels,
    FavoriteIds* favoriteLevelIds,
    bool beatmapLevelsAreSorted,
    bool sortBeatmapLevels
) {
    if (!refreshing) {
        lastRequest.table = self;
        if (beatmapLevels)
            lastRequest.unfilteredLevels.emplace(beatmapLevels);
        else
            lastRequest.unfilteredLevels.clear();
        if (favoriteLevelIds)
            lastRequest.favoriteLevelIds.emplace(favoriteLevelIds);
        else
            lastRequest.favoriteLevelIds.clear();
        lastRequest.beatmapLevelsAreSorted = beatmapLevelsAreSorted;
        lastRequest.sortBeatmapLevels = sortBeatmapLevels;
    }
    LevelCollectionTableView_SetData(self, FilterToUnlockedLevels(beatmapLevels), favoriteLevelIds, beatmapLevelsAreSorted, sortBeatmapLevels);
}

MAKE_HOOK_MATCH(LevelCollectionTableView_OnEnable, &LevelCollectionTableView::OnEnable, void, LevelCollectionTableView* self) {
    LevelCollectionTableView_OnEnable(self);
    if (refreshWhenShown && lastRequest.table && static_cast<LevelCollectionTableView*>(lastRequest.table) == self)
        BeatSaberQuestAP::Hooks::RefreshLevelList();
}

namespace BeatSaberQuestAP::Hooks {

    void InstallLevelListFilter() {
        INSTALL_HOOK(logger, LevelCollectionTableView_SetData);
        INSTALL_HOOK(logger, LevelCollectionTableView_OnEnable);
    }

    void RefreshLevelList() {
        if (!lastRequest.table || !lastRequest.unfilteredLevels)
            return;
        if (!lastRequest.table->isActiveAndEnabled) {
            refreshWhenShown = true;
            return;
        }
        refreshWhenShown = false;
        refreshing = true;
        lastRequest.table->SetData(
            lastRequest.unfilteredLevels.ptr(),
            lastRequest.favoriteLevelIds ? lastRequest.favoriteLevelIds.ptr() : nullptr,
            lastRequest.beatmapLevelsAreSorted,
            lastRequest.sortBeatmapLevels
        );
        refreshing = false;
    }
}
