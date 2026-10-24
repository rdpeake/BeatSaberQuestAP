#include "Session.hpp"

#include "Content/ContentResolver.hpp"
#include "DeathLink.hpp"
#include "Logger.hpp"
#include "ModPaths.hpp"
#include "archipelago/CertificateStore.hpp"
#include "archipelago/LevelIdentity.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "rapidjson/document.h"

namespace BeatSaberQuestAP {

    namespace {
        constexpr char const* deathLinkCause = "too many bloqs :(";

        std::optional<Archipelago::SlotData> ParseSavedSlotData(std::string const& slotDataJson) {
            rapidjson::Document document;
            document.Parse(slotDataJson.c_str());
            if (document.HasParseError())
                return std::nullopt;
            try {
                return Archipelago::ParseSlotData(document);
            } catch (std::exception const& error) {
                logger.warn("Saved slot data is unusable: {}", error.what());
                return std::nullopt;
            }
        }
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
        callbacks.onConnected = [this](Archipelago::SlotData const& newSlotData, std::string const& slotDataJson) { HandleConnected(newSlotData, slotDataJson); };
        callbacks.onItemsChanged = [this](std::vector<Archipelago::ReceivedItem> const& items) { HandleItemsChanged(items); };
        callbacks.onPendingLocationsChanged = [this](std::vector<int64_t> const& unconfirmedLocations) { HandlePendingLocationsChanged(unconfirmedLocations); };
        callbacks.onDeathLink = [this](Archipelago::DeathLinkEvent const& deathLink) { HandleDeathLink(deathLink); };
        callbacks.onServerMessage = [](std::string const& message) { logger.info("Server: {}", message); };

        client = std::make_unique<Archipelago::ArchipelagoClient>(
            std::move(callbacks),
            [](std::function<void()> work) { BSML::MainThreadScheduler::Schedule(std::move(work)); },
            certificateFile
        );
    }

    void Session::RestoreSavedSession() {
        auto saved = Archipelago::LoadSavedSession(ModPaths::SessionFile());
        if (!saved)
            return;
        auto savedSlotData = ParseSavedSlotData(saved->slotDataJson);
        if (!savedSlotData) {
            Archipelago::DeleteSavedSession(ModPaths::SessionFile());
            return;
        }

        activeSession = std::move(saved);
        slotData = std::move(savedSlotData);
        progression = Archipelago::ComputeProgression(*slotData, activeSession->receivedItemNames);
        currentSettings = {activeSession->host, activeSession->port, activeSession->slotName, ""};
        logger.info("Restored Archipelago session for {} with {} unlocked song(s)", activeSession->slotName, progression.unlockedIdents.size());
        ContentResolver::Instance().Begin(*slotData);

        EnsureClient();
        client->RestorePendingLocationChecks(activeSession->unconfirmedLocations);
        if (activeSession->passwordRequired) {
            passwordNeeded = true;
            statusText = "Offline: enter the room password to reconnect";
            NotifyChanged();
            return;
        }
        client->Resume(currentSettings);
    }

    bool Session::IsSameSlot(Archipelago::ConnectionSettings const& settings) const {
        return activeSession && Archipelago::EqualsIgnoreCase(activeSession->host, settings.host) && activeSession->port == settings.port
            && activeSession->slotName == settings.slotName;
    }

    void Session::Connect(Archipelago::ConnectionSettings const& settings) {
        EnsureClient();
        passwordNeeded = false;
        if (IsSameSlot(settings)) {
            currentSettings = settings;
            client->Resume(settings);
            return;
        }
        if (activeSession)
            LeaveArchipelago();
        currentSettings = settings;
        goalReported = false;
        client->Connect(settings);
    }

    void Session::ReconnectNow() {
        if (client && !passwordNeeded)
            client->ReconnectNow();
    }

    void Session::LeaveArchipelago() {
        if (client)
            client->Disconnect();
        activeSession.reset();
        slotData.reset();
        progression = {};
        passwordNeeded = false;
        goalReported = false;
        statusText = "Not connected";
        Archipelago::DeleteSavedSession(ModPaths::SessionFile());
        ContentResolver::Instance().Reset();
        logger.info("Left Archipelago mode");
        NotifyChanged();
    }

    bool Session::IsInArchipelagoMode() const {
        return activeSession.has_value() && slotData.has_value();
    }

    bool Session::IsOnline() const {
        return connectionState == Archipelago::ConnectionState::Connected;
    }

    bool Session::NeedsPasswordToReconnect() const {
        return passwordNeeded;
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

    std::optional<Archipelago::SavedSession> const& Session::GetActiveSession() const {
        return activeSession;
    }

    size_t Session::GetUnsentCheckCount() const {
        return activeSession ? activeSession->unconfirmedLocations.size() : 0;
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
        if (!IsInArchipelagoMode())
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
        if (!Archipelago::IsIdentUnlocked(progression, *ident)) {
            logger.info("Clear of {} ignored: difficulty is not unlocked", *ident);
            return;
        }

        auto node = Archipelago::FindNodeForIdent(*slotData, *ident);
        auto locations = Archipelago::LocationsForClear(*slotData, progression, *node, Archipelago::GradeIndexFromRankName(finishedLevel.rankName));
        logger.info("Cleared {} (node {}, rank {}): {} check(s){}", *ident, *node, finishedLevel.rankName, locations.size(), IsOnline() ? "" : ", held until reconnected");
        // checks made offline are held by the client and saved until the server confirms them
        client->SendLocationChecks(locations);
    }

    void Session::AddChangeListener(std::function<void()> listener) {
        changeListeners.push_back(std::move(listener));
    }

    void Session::NotifyChanged() {
        for (auto const& listener : changeListeners)
            listener();
    }

    void Session::EnterArchipelagoMode(Archipelago::SlotData const& newSlotData, std::string const& slotDataJson) {
        Archipelago::SavedSession session;
        session.host = currentSettings.host;
        session.port = currentSettings.port;
        session.slotName = currentSettings.slotName;
        session.slotDataJson = slotDataJson;
        activeSession = std::move(session);
        slotData = newSlotData;
        progression = Archipelago::ComputeProgression(*slotData, {});
        logger.info("Entered Archipelago mode: {} songs, game mode {}", slotData->nodeToIdent.size(), static_cast<int>(slotData->gameMode));
        ContentResolver::Instance().Begin(*slotData);
    }

    void Session::SaveActiveSession() {
        if (activeSession && !Archipelago::WriteSavedSession(ModPaths::SessionFile(), *activeSession))
            logger.warn("Could not save the Archipelago session");
    }

    void Session::HandleStateChanged(Archipelago::ConnectionState state, std::string const& detail) {
        connectionState = state;
        statusText = detail;
        logger.info("Connection: {}", detail);
        if (state == Archipelago::ConnectionState::Failed && activeSession)
            passwordNeeded = detail.find("wrong password") != std::string::npos;
        NotifyChanged();
    }

    void Session::HandleConnected(Archipelago::SlotData const& newSlotData, std::string const& slotDataJson) {
        passwordNeeded = false;
        if (!activeSession) {
            EnterArchipelagoMode(newSlotData, slotDataJson);
        } else if (slotDataJson != activeSession->slotDataJson) {
            // the room was regenerated under the same slot; follow the server
            activeSession->slotDataJson = slotDataJson;
            slotData = newSlotData;
            progression = Archipelago::ComputeProgression(*slotData, activeSession->receivedItemNames);
            ContentResolver::Instance().Begin(*slotData);
        }
        activeSession->passwordRequired = !currentSettings.password.empty();
        ContentResolver::Instance().RetryFailedLookups();
        SaveActiveSession();
        NotifyChanged();
    }

    void Session::HandleItemsChanged(std::vector<Archipelago::ReceivedItem> const& items) {
        if (!slotData || !activeSession)
            return;
        std::vector<std::string> itemNames;
        itemNames.reserve(items.size());
        for (auto const& item : items)
            itemNames.push_back(item.itemName);
        activeSession->receivedItemNames = itemNames;
        progression = Archipelago::ComputeProgression(*slotData, itemNames);
        logger.info("{} item(s) received, {} song(s) unlocked", items.size(), progression.unlockedIdents.size());

        if (progression.victoryReceived && !goalReported) {
            client->SendGoalComplete();
            goalReported = true;
        }
        SaveActiveSession();
        NotifyChanged();
    }

    void Session::HandlePendingLocationsChanged(std::vector<int64_t> const& unconfirmedLocations) {
        if (!activeSession)
            return;
        activeSession->unconfirmedLocations = unconfirmedLocations;
        SaveActiveSession();
        NotifyChanged();
    }

    void Session::HandleDeathLink(Archipelago::DeathLinkEvent const& deathLink) {
        logger.info("DeathLink from {}: {}", deathLink.source, deathLink.cause);
        DeathLink::ForceFailCurrentLevel();
    }
}
