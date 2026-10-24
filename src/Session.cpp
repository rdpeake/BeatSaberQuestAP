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
        constexpr size_t maximumLogMessages = 80;
        constexpr char const* difficultyNames[] = {"Easy", "Normal", "Hard", "Expert", "Expert+"};

        std::string DescribeDifficulty(int difficulty) {
            return difficulty >= 0 && difficulty < 5 ? difficultyNames[difficulty] : std::to_string(difficulty);
        }

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
        callbacks.onServerMessage = [this](Archipelago::ServerMessage const& message) {
            logger.info("Server [{}]: {}", message.type, message.text);
            // other players' item sends and hints would drown out what matters to this player
            bool itemTraffic = message.type == "ItemSend" || message.type == "ItemCheat" || message.type == "Hint";
            bool chat = message.type == "Chat" || message.type == "ServerChat";
            if (itemTraffic && !message.involvesThisSlot)
                return;
            AddLogMessage(message.text, itemTraffic || chat ? LogLevel::Player : LogLevel::Debug);
        };

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
        knownItemCount = activeSession->receivedItemNames.size();
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
        knownItemCount = 0;
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

    std::vector<std::string> Session::SeedDifficultiesFor(std::string_view levelId) const {
        std::vector<std::string> seedDifficulties;
        auto mapIdentifier = ContentResolver::Instance().MapIdentifierForLevelId(levelId);
        if (!slotData || !mapIdentifier)
            return seedDifficulties;
        for (auto const& [node, ident] : slotData->nodeToIdent) {
            auto parsed = Archipelago::ParseIdent(ident);
            if (parsed && Archipelago::EqualsIgnoreCase(parsed->mapIdentifier, *mapIdentifier))
                seedDifficulties.push_back(parsed->characteristic + " " + DescribeDifficulty(parsed->difficulty));
        }
        return seedDifficulties;
    }

    bool Session::IsIdentInSeed(std::string const& ident) const {
        return slotData && Archipelago::FindNodeForIdent(*slotData, ident).has_value();
    }

    void Session::HandleLevelFinished(FinishedLevel const& finishedLevel) {
        bool causedByDeathLink = DeathLink::ConsumeFailFromDeathLink();
        if (!IsInArchipelagoMode())
            return;

        auto ident = IdentFor(finishedLevel.levelId, finishedLevel.characteristic, finishedLevel.difficulty);
        auto node = ident ? Archipelago::FindNodeForIdent(*slotData, *ident) : std::nullopt;
        std::string songName = finishedLevel.songName.empty() ? finishedLevel.levelId : finishedLevel.songName;
        std::string played = songName + " (" + finishedLevel.characteristic + " " + DescribeDifficulty(finishedLevel.difficulty) + ")";

        if (!node) {
            auto seedDifficulties = SeedDifficultiesFor(finishedLevel.levelId);
            if (seedDifficulties.empty()) {
                AddLogMessage(played + " is not a song in this seed", LogLevel::Debug);
            } else {
                std::string expected;
                for (auto const& seedDifficulty : seedDifficulties)
                    expected += (expected.empty() ? "" : " or ") + seedDifficulty;
                AddLogMessage(played + ": no checks, this seed uses " + expected, LogLevel::Debug);
            }
            return;
        }

        if (finishedLevel.failed && !causedByDeathLink) {
            client->SendDeathLink(deathLinkCause);
            if (slotData->deathLinkEnabled)
                AddLogMessage("Failed " + played + (IsOnline() ? ": DeathLink sent" : ": offline, DeathLink not sent"), LogLevel::Debug);
        }

        if (!finishedLevel.cleared)
            return;
        if (finishedLevel.usedModifiers) {
            AddLogMessage("Cleared " + played + " with modifiers: no checks (turn modifiers off)", LogLevel::Debug);
            return;
        }
        if (finishedLevel.practice) {
            AddLogMessage("Cleared " + played + " in practice mode: no checks", LogLevel::Debug);
            return;
        }
        if (!Archipelago::IsIdentUnlocked(progression, *ident)) {
            AddLogMessage("Cleared " + played + ", but that difficulty is still locked: no checks", LogLevel::Debug);
            return;
        }

        auto locations = Archipelago::LocationsForClear(*slotData, progression, *node, Archipelago::GradeIndexFromRankName(finishedLevel.rankName));
        if (locations.empty()) {
            std::string reason = Archipelago::IsAccuracyMode(slotData->gameMode) ? "rank " + finishedLevel.rankName + " is below C" : "the goal needs more Bloq Keys";
            AddLogMessage("Cleared " + played + ": no checks, " + reason, LogLevel::Debug);
            return;
        }
        // checks made offline are held by the client and saved until the server confirms them
        client->SendLocationChecks(locations);
        AddLogMessage("Cleared " + played + " at " + finishedLevel.rankName + ": " + std::to_string(locations.size()) + " check(s) " + (IsOnline() ? "sent" : "saved until reconnected"));
    }

    void Session::AddChangeListener(std::function<void()> listener) {
        changeListeners.push_back(std::move(listener));
    }

    void Session::NotifyChanged() {
        for (auto const& listener : changeListeners)
            listener();
    }

    void Session::AddLogMessage(std::string message, LogLevel level) {
        logger.info("Log: {}", message);
        logMessages.push_back({level, std::move(message)});
        while (logMessages.size() > maximumLogMessages)
            logMessages.pop_front();
        NotifyChanged();
    }

    std::deque<LogMessage> const& Session::GetLogMessages() const {
        return logMessages;
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
        bool stateChanged = state != connectionState;
        connectionState = state;
        statusText = detail;
        logger.info("Connection: {}", detail);
        if (stateChanged && state != Archipelago::ConnectionState::Connecting)
            logMessages.push_back({LogLevel::Debug, detail});
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
        // a full resync resends everything, so only items past the known count are new
        if (items.size() > knownItemCount && knownItemCount > 0) {
            for (size_t index = knownItemCount; index < items.size(); index++) {
                auto const& item = items[index];
                std::string itemName = item.itemName.empty() ? "item " + std::to_string(item.itemId) : item.itemName;
                AddLogMessage("Received " + itemName + " from " + client->GetPlayerAlias(item.sendingPlayer), LogLevel::Debug);
            }
        } else if (knownItemCount == 0 && !items.empty()) {
            AddLogMessage(std::to_string(items.size()) + " item(s) received so far", LogLevel::Debug);
        }
        knownItemCount = items.size();
        activeSession->receivedItemNames = itemNames;
        progression = Archipelago::ComputeProgression(*slotData, itemNames);
        logger.info("{} item(s) received, {} song(s) unlocked", items.size(), progression.unlockedIdents.size());

        if (progression.victoryReceived && !goalReported) {
            client->SendGoalComplete();
            goalReported = true;
            AddLogMessage("Goal complete!");
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
        AddLogMessage("DeathLink from " + deathLink.source + (deathLink.cause.empty() ? "" : ": " + deathLink.cause));
        DeathLink::ForceFailCurrentLevel();
    }
}
