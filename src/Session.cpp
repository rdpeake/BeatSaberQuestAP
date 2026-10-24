#include "Session.hpp"

#include "Content/ContentResolver.hpp"
#include "DeathLink.hpp"
#include "Logger.hpp"
#include "ModPaths.hpp"
#include "archipelago/CertificateStore.hpp"
#include "archipelago/LevelIdentity.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"

namespace BeatSaberQuestAP {

    namespace {
        constexpr char const* deathLinkCause = "too many bloqs :(";
    }

    Session& Session::Instance() {
        static Session instance;
        return instance;
    }

    void Session::EnsureClient() {
        if (client)
            return;

        auto certificateFile = Archipelago::WriteSystemCertificateBundle(ModPaths::CertificateBundleFile());
        if (certificateFile.empty())
            logger.warn("No system certificates found; secure connections will not verify the server");

        Archipelago::ClientCallbacks callbacks;
        callbacks.onStateChanged = [this](Archipelago::ConnectionState state, std::string const& detail) { HandleStateChanged(state, detail); };
        callbacks.onConnected = [this](Archipelago::SlotData const& newSlotData) { HandleConnected(newSlotData); };
        callbacks.onItemsChanged = [this](std::vector<Archipelago::ReceivedItem> const& items) { HandleItemsChanged(items); };
        callbacks.onDeathLink = [this](Archipelago::DeathLinkEvent const& deathLink) { HandleDeathLink(deathLink); };
        callbacks.onServerMessage = [](std::string const& message) { logger.info("Server: {}", message); };

        client = std::make_unique<Archipelago::ArchipelagoClient>(
            std::move(callbacks),
            [](std::function<void()> work) { BSML::MainThreadScheduler::Schedule(std::move(work)); },
            certificateFile
        );
    }

    void Session::Connect(Archipelago::ConnectionSettings const& settings) {
        EnsureClient();
        slotData.reset();
        progression = {};
        goalReported = false;
        ContentResolver::Instance().Reset();
        client->Connect(settings);
    }

    void Session::Disconnect() {
        if (client)
            client->Disconnect();
        slotData.reset();
        progression = {};
        ContentResolver::Instance().Reset();
        NotifyChanged();
    }

    bool Session::IsConnected() const {
        return connectionState == Archipelago::ConnectionState::Connected && slotData.has_value();
    }

    Archipelago::ConnectionState Session::GetConnectionState() const {
        return connectionState;
    }

    std::string const& Session::GetStatusText() const {
        return statusText;
    }

    std::optional<Archipelago::SlotData> const& Session::GetSlotData() const {
        return slotData;
    }

    Archipelago::Progression const& Session::GetProgression() const {
        return progression;
    }

    std::optional<std::string> Session::IdentFor(std::string_view levelId, std::string_view characteristic, int difficulty) const {
        auto mapIdentifier = ContentResolver::Instance().MapIdentifierForLevelId(levelId);
        if (!mapIdentifier)
            return std::nullopt;
        return Archipelago::LevelIdentity{*mapIdentifier, std::string(characteristic), difficulty}.ToIdent();
    }

    bool Session::IsIdentInSeed(std::string const& ident) const {
        return slotData && Archipelago::FindNodeForIdent(*slotData, ident).has_value();
    }

    void Session::HandleLevelFinished(FinishedLevel const& finishedLevel) {
        bool causedByDeathLink = DeathLink::ConsumeFailFromDeathLink();
        if (!IsConnected())
            return;

        auto ident = IdentFor(finishedLevel.levelId, finishedLevel.characteristic, finishedLevel.difficulty);
        if (!ident || !IsIdentInSeed(*ident)) {
            logger.info("Finished level {} is not part of this seed", finishedLevel.levelId);
            return;
        }

        if (finishedLevel.failed && !causedByDeathLink)
            client->SendDeathLink(deathLinkCause);

        if (!finishedLevel.cleared)
            return;
        if (finishedLevel.usedModifiers || finishedLevel.practice) {
            logger.info("Clear of {} ignored: modifiers or practice mode were used", *ident);
            return;
        }
        // the PC client also checked locked difficulties of visible songs; that skips progression
        if (!Archipelago::IsIdentUnlocked(progression, *ident)) {
            logger.info("Clear of {} ignored: difficulty is not unlocked", *ident);
            return;
        }

        auto node = Archipelago::FindNodeForIdent(*slotData, *ident);
        auto locations = Archipelago::LocationsForClear(*slotData, *node, Archipelago::GradeIndexFromRankName(finishedLevel.rankName));
        logger.info("Cleared {} (node {}, rank {}), sending {} location check(s)", *ident, *node, finishedLevel.rankName, locations.size());
        client->SendLocationChecks(locations);
    }

    void Session::AddChangeListener(std::function<void()> listener) {
        changeListeners.push_back(std::move(listener));
    }

    void Session::NotifyChanged() {
        for (auto const& listener : changeListeners)
            listener();
    }

    void Session::HandleStateChanged(Archipelago::ConnectionState state, std::string const& detail) {
        connectionState = state;
        statusText = detail;
        logger.info("Connection: {}", detail);
        if (state != Archipelago::ConnectionState::Connected && state != Archipelago::ConnectionState::Connecting) {
            slotData.reset();
            progression = {};
            ContentResolver::Instance().Reset();
        }
        NotifyChanged();
    }

    void Session::HandleConnected(Archipelago::SlotData const& newSlotData) {
        slotData = newSlotData;
        progression = Archipelago::ComputeProgression(*slotData, {});
        logger.info("Seed has {} songs, game mode {}", slotData->nodeToIdent.size(), slotData->gameModeName);
        ContentResolver::Instance().Begin(*slotData);
        NotifyChanged();
    }

    void Session::HandleItemsChanged(std::vector<Archipelago::ReceivedItem> const& items) {
        if (!slotData)
            return;
        std::vector<std::string> itemNames;
        itemNames.reserve(items.size());
        for (auto const& item : items)
            itemNames.push_back(item.itemName);
        progression = Archipelago::ComputeProgression(*slotData, itemNames);
        logger.info("{} item(s) received, {} song(s) unlocked", items.size(), progression.unlockedIdents.size());

        if (progression.victoryReceived && !goalReported) {
            client->SendGoalComplete();
            goalReported = true;
        }
        NotifyChanged();
    }

    void Session::HandleDeathLink(Archipelago::DeathLinkEvent const& deathLink) {
        logger.info("DeathLink from {}: {}", deathLink.source, deathLink.cause);
        DeathLink::ForceFailCurrentLevel();
    }
}
