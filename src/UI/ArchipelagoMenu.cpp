#include "UI/ArchipelagoMenu.hpp"

#include <charconv>

#include "Content/ContentResolver.hpp"
#include "Content/PlaylistImporter.hpp"
#include "HMUI/CurvedTextMeshPro.hpp"
#include "HMUI/ViewController.hpp"
#include "Logger.hpp"
#include "Session.hpp"
#include "SettingsStore.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML.hpp"

namespace BeatSaberQuestAP::ArchipelagoMenu {

    namespace {
        struct MenuWidgets {
            UnityW<HMUI::CurvedTextMeshPro> connectionText;
            UnityW<HMUI::CurvedTextMeshPro> contentText;
            UnityW<HMUI::CurvedTextMeshPro> importText;
            UnityW<UnityEngine::UI::Button> connectButton;
            UnityW<UnityEngine::UI::Button> reconnectButton;
            UnityW<UnityEngine::UI::Button> leaveButton;
            UnityW<UnityEngine::UI::Button> downloadButton;
            UnityW<UnityEngine::UI::VerticalLayoutGroup> storeButtonList;
        };

        MenuWidgets widgets;
        Archipelago::ConnectionSettings pendingSettings;
        std::vector<std::string> shownStorePackIds;

        std::string ColorFor(Archipelago::ConnectionState state) {
            switch (state) {
                case Archipelago::ConnectionState::Connected:
                    return "#7CFC00";
                case Archipelago::ConnectionState::Failed:
                    return "#FF6060";
                default:
                    return "#FFD24A";
            }
        }

        std::string DescribeContent(ContentReport const& report) {
            std::string description;
            if (!report.progressText.empty())
                description += report.progressText + "\n";
            if (report.downloading)
                description += "Downloading " + std::to_string(report.downloadsFinished) + " of " + std::to_string(report.downloadsTotal) + " songs...\n";
            if (report.requiredMapCount > 0)
                description += std::to_string(report.playableMapCount) + " of " + std::to_string(report.requiredMapCount) + " seed songs are installed\n";
            if (!report.downloadableKeys.empty())
                description += "<color=#FFD24A>" + std::to_string(report.downloadableKeys.size()) + " song(s) need downloading from BeatSaver</color>\n";
            for (auto const& pack : report.missingPacks)
                description += "<color=#FFD24A>DLC not owned: " + pack.packName + "</color>\n";
            for (auto const& problem : report.unavailableDescriptions)
                description += "<color=#FF6060>" + problem + "</color>\n";
            if (!report.checking && !report.downloading && report.requiredMapCount > 0 && !report.HasProblems())
                description += "<color=#7CFC00>Everything this seed needs is installed</color>";
            return description;
        }

        void RebuildStoreButtons(ContentReport const& report) {
            std::vector<std::string> packIds;
            for (auto const& pack : report.missingPacks)
                packIds.push_back(pack.packId);
            if (packIds == shownStorePackIds || !widgets.storeButtonList)
                return;
            shownStorePackIds = packIds;

            auto listTransform = widgets.storeButtonList->transform;
            for (int childIndex = listTransform->childCount - 1; childIndex >= 0; childIndex--)
                UnityEngine::Object::Destroy(listTransform->GetChild(childIndex)->gameObject);

            for (auto const& pack : report.missingPacks) {
                std::string packId = pack.packId;
                BSML::Lite::CreateUIButton(listTransform, "Open store: " + pack.packName, [packId] {
                    ContentResolver::Instance().OpenStoreForPack(packId);
                });
            }
        }

        void Connect() {
            if (pendingSettings.host.empty() || pendingSettings.slotName.empty()) {
                if (widgets.connectionText)
                    widgets.connectionText->text = "<color=#FF6060>Enter a host and slot name first</color>";
                return;
            }
            SettingsStore::SaveConnectionSettings(pendingSettings);
            Session::Instance().Connect(pendingSettings);
        }

        void BuildMenu(HMUI::ViewController* viewController) {
            pendingSettings = SettingsStore::LoadConnectionSettings();
            shownStorePackIds.clear();

            auto container = BSML::Lite::CreateScrollableSettingsContainer(viewController->transform);

            widgets.connectionText = BSML::Lite::CreateText(container, "", 4.0f);
            BSML::Lite::CreateStringSetting(container, "Host", pendingSettings.host, [](StringW value) {
                pendingSettings.host = static_cast<std::string>(value);
            });
            BSML::Lite::CreateStringSetting(container, "Port", std::to_string(pendingSettings.port), [](StringW value) {
                std::string text = value;
                int port = 0;
                auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
                if (error == std::errc() && port > 0 && port < 65536)
                    pendingSettings.port = port;
            });
            BSML::Lite::CreateStringSetting(container, "Slot", pendingSettings.slotName, [](StringW value) {
                pendingSettings.slotName = static_cast<std::string>(value);
            });
            BSML::Lite::CreateStringSetting(container, "Password", "", [](StringW value) {
                pendingSettings.password = static_cast<std::string>(value);
            });

            widgets.connectButton = BSML::Lite::CreateUIButton(container, "Connect", [] { Connect(); });
            widgets.reconnectButton = BSML::Lite::CreateUIButton(container, "Reconnect now", [] { Session::Instance().ReconnectNow(); });
            widgets.leaveButton = BSML::Lite::CreateUIButton(container, "Leave Archipelago", [] { Session::Instance().LeaveArchipelago(); });

            widgets.contentText = BSML::Lite::CreateText(container, "", 3.5f);
            widgets.downloadButton = BSML::Lite::CreateUIButton(container, "Download missing songs", [] {
                ContentResolver::Instance().DownloadMissingSongs();
            });
            widgets.storeButtonList = BSML::Lite::CreateVerticalLayoutGroup(container);

            widgets.importText = BSML::Lite::CreateText(container, "", 3.0f);
            BSML::Lite::CreateUIButton(container, "Import .apbs playlist", [] {
                auto summary = PlaylistImporter::ImportPendingPatches();
                if (widgets.importText)
                    widgets.importText->text = summary.empty() ? "No .apbs files found in ModData/.../Mods/" MOD_ID " or the Playlists folder" : summary;
            });
        }

        void HandleDidActivate(HMUI::ViewController* viewController, bool firstActivation, bool, bool) {
            if (firstActivation)
                BuildMenu(viewController);
            Refresh();
        }
    }

    void Register() {
        BSML::Register::RegisterMainMenu("Archipelago", "Archipelago", "Connect to an Archipelago multiworld", HandleDidActivate);
    }

    void Refresh() {
        if (!widgets.connectionText)
            return;
        auto& session = Session::Instance();
        auto state = session.GetConnectionState();
        bool inArchipelagoMode = session.IsInArchipelagoMode();
        bool online = session.IsOnline();
        bool connecting = state == Archipelago::ConnectionState::Connecting;

        std::string status = "<color=" + ColorFor(state) + ">" + session.GetStatusText() + "</color>";
        if (inArchipelagoMode && !online) {
            status = "<color=#FFD24A>Offline: songs stay locked to slot " + session.GetActiveSession()->slotName + "</color>\n" + status;
            if (session.NeedsPasswordToReconnect())
                status += "\nEnter the room password above and press Reconnect";
        }
        if (auto unsentChecks = session.GetUnsentCheckCount(); unsentChecks > 0)
            status += "\n" + std::to_string(unsentChecks) + " check(s) waiting to be sent";
        widgets.connectionText->text = status;

        if (widgets.connectButton) {
            widgets.connectButton->interactable = !connecting;
            widgets.connectButton->gameObject->SetActive(!inArchipelagoMode || session.NeedsPasswordToReconnect() || !online);
        }
        if (widgets.reconnectButton)
            widgets.reconnectButton->gameObject->SetActive(inArchipelagoMode && !online && !session.NeedsPasswordToReconnect());
        if (widgets.leaveButton)
            widgets.leaveButton->gameObject->SetActive(inArchipelagoMode || connecting);

        auto const& report = ContentResolver::Instance().GetReport();
        if (widgets.contentText)
            widgets.contentText->text = inArchipelagoMode ? DescribeContent(report) : "";
        if (widgets.downloadButton) {
            widgets.downloadButton->gameObject->SetActive(inArchipelagoMode && online && !report.downloadableKeys.empty());
            widgets.downloadButton->interactable = !report.downloading && !report.checking;
        }
        RebuildStoreButtons(inArchipelagoMode ? report : ContentReport{});
    }
}
