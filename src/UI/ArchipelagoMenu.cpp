#include "UI/ArchipelagoMenu.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <memory>

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
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/Components/Keyboard/ModalKeyboard.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"

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

        constexpr float textWidth = 90.0f;

        // TMP text in a layout group reports no height of its own, so size it from its line count
        UnityW<HMUI::CurvedTextMeshPro> CreateMessageText(UnityEngine::GameObject* container, float fontSize) {
            auto text = BSML::Lite::CreateText(container, "", fontSize);
            text->enableWordWrapping = true;
            text->alignment = TMPro::TextAlignmentOptions::Center;
            auto layout = text->gameObject->AddComponent<UnityEngine::UI::LayoutElement*>();
            layout->preferredWidth = textWidth;
            text->gameObject->SetActive(false);
            return text;
        }

        int CountVisibleCharacters(std::string_view line) {
            int count = 0;
            bool insideTag = false;
            for (char character : line) {
                if (character == '<')
                    insideTag = true;
                else if (character == '>')
                    insideTag = false;
                else if (!insideTag)
                    count++;
            }
            return count;
        }

        void SetMessageText(UnityW<HMUI::CurvedTextMeshPro> const& text, std::string const& message) {
            if (!text)
                return;
            HMUI::CurvedTextMeshPro* textComponent = text;
            textComponent->text = message;
            textComponent->gameObject->SetActive(!message.empty());
            if (message.empty())
                return;

            // average glyph width is a little over half the font size
            float fontSize = textComponent->fontSize;
            int charactersPerLine = std::max(1, static_cast<int>(textWidth / (fontSize * 0.55f)));
            int lineCount = 0;
            size_t lineStart = 0;
            while (lineStart <= message.size()) {
                size_t lineEnd = message.find('\n', lineStart);
                if (lineEnd == std::string::npos)
                    lineEnd = message.size();
                int visibleCharacters = CountVisibleCharacters(std::string_view(message).substr(lineStart, lineEnd - lineStart));
                lineCount += std::max(1, (visibleCharacters + charactersPerLine - 1) / charactersPerLine);
                lineStart = lineEnd + 1;
            }
            // the settings container sizes children by their rect, so set it as well as the layout hint
            float height = lineCount * fontSize * 1.25f;
            textComponent->rectTransform->sizeDelta = {textWidth, height};
            if (auto layout = textComponent->GetComponent<UnityEngine::UI::LayoutElement*>()) {
                layout->minHeight = height;
                layout->preferredHeight = height;
            }
        }

        // the game's own keyboard has no symbols, so host names and IPs need BSML's full keyboard
        struct TextField {
            std::string label;
            bool masked = false;
            std::function<std::string()> read;
            std::function<bool(std::string const&)> write;
            UnityW<UnityEngine::UI::Button> button;
        };

        MenuWidgets widgets;
        Archipelago::ConnectionSettings pendingSettings;
        std::vector<std::string> shownStorePackIds;
        std::vector<std::shared_ptr<TextField>> textFields;
        UnityW<BSML::ModalKeyboard> keyboard;
        std::shared_ptr<TextField> editingField;

        void ShowFieldValue(TextField& field) {
            if (!field.button)
                return;
            std::string value = field.read();
            std::string shown = field.masked ? std::string(value.size(), '*') : value;
            if (shown.empty())
                shown = "<color=#888888>(empty)</color>";
            if (auto text = field.button->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
                text->text = field.label + ": " + shown;
        }

        void CreateKeyboard(HMUI::ViewController* viewController) {
            BSML::parse_and_construct("<modal-keyboard clear-on-open='true'/>", viewController->transform, nullptr);
            auto keyboards = viewController->GetComponentsInChildren<BSML::ModalKeyboard*>(true);
            if (keyboards.size() == 0) {
                logger.error("Could not create the connection keyboard");
                return;
            }
            keyboard = keyboards[keyboards.size() - 1];
            keyboard->onEnter = [](StringW value) {
                if (!editingField)
                    return;
                if (!editingField->write(static_cast<std::string>(value)))
                    SetMessageText(widgets.connectionText, "<color=#FF6060>" + editingField->label + " was not changed: invalid value</color>");
                ShowFieldValue(*editingField);
                editingField.reset();
            };
        }

        void EditField(std::shared_ptr<TextField> const& field) {
            if (!keyboard)
                return;
            editingField = field;
            keyboard->modalView->Show();
            // clear-on-open runs as the modal enables, so the current value goes in afterwards
            keyboard->SetText(field->masked ? "" : field->read());
        }

        void AddTextField(UnityEngine::GameObject* container, std::string label, bool masked, std::function<std::string()> read, std::function<bool(std::string const&)> write) {
            auto field = std::make_shared<TextField>();
            field->label = std::move(label);
            field->masked = masked;
            field->read = std::move(read);
            field->write = std::move(write);
            field->button = BSML::Lite::CreateUIButton(container, field->label, [field] { EditField(field); });
            ShowFieldValue(*field);
            textFields.push_back(field);
        }

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
                SetMessageText(widgets.connectionText, "<color=#FF6060>Enter a host and slot name first</color>");
                return;
            }
            SettingsStore::SaveConnectionSettings(pendingSettings);
            Session::Instance().Connect(pendingSettings);
        }

        void BuildMenu(HMUI::ViewController* viewController) {
            pendingSettings = SettingsStore::LoadConnectionSettings();
            shownStorePackIds.clear();

            auto container = BSML::Lite::CreateScrollableSettingsContainer(viewController->transform);

            widgets.connectionText = CreateMessageText(container, 4.0f);
            textFields.clear();
            AddTextField(container, "Host", false, [] { return pendingSettings.host; }, [](std::string const& value) {
                if (value.empty())
                    return false;
                pendingSettings.host = value;
                return true;
            });
            AddTextField(container, "Port", false, [] { return std::to_string(pendingSettings.port); }, [](std::string const& value) {
                int port = 0;
                auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
                if (error != std::errc() || end != value.data() + value.size() || port <= 0 || port > 65535)
                    return false;
                pendingSettings.port = port;
                return true;
            });
            AddTextField(container, "Slot", false, [] { return pendingSettings.slotName; }, [](std::string const& value) {
                pendingSettings.slotName = value;
                return true;
            });
            AddTextField(container, "Password", true, [] { return pendingSettings.password; }, [](std::string const& value) {
                pendingSettings.password = value;
                return true;
            });
            CreateKeyboard(viewController);

            widgets.connectButton = BSML::Lite::CreateUIButton(container, "Connect", [] { Connect(); });
            widgets.reconnectButton = BSML::Lite::CreateUIButton(container, "Reconnect now", [] { Session::Instance().ReconnectNow(); });
            widgets.leaveButton = BSML::Lite::CreateUIButton(container, "Leave Archipelago", [] { Session::Instance().LeaveArchipelago(); });

            widgets.contentText = CreateMessageText(container, 3.5f);
            widgets.downloadButton = BSML::Lite::CreateUIButton(container, "Download missing songs", [] {
                ContentResolver::Instance().DownloadMissingSongs();
            });
            widgets.storeButtonList = BSML::Lite::CreateVerticalLayoutGroup(container);

            widgets.importText = CreateMessageText(container, 3.0f);
            BSML::Lite::CreateUIButton(container, "Import .apbs playlist", [] {
                auto summary = PlaylistImporter::ImportPendingPatches();
                SetMessageText(widgets.importText, summary.empty() ? "No .apbs files found in ModData/.../Mods/" MOD_ID " or the Playlists folder" : summary);
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
        SetMessageText(widgets.connectionText, status);

        if (widgets.connectButton) {
            widgets.connectButton->interactable = !connecting;
            widgets.connectButton->gameObject->SetActive(!inArchipelagoMode || session.NeedsPasswordToReconnect() || !online);
        }
        if (widgets.reconnectButton)
            widgets.reconnectButton->gameObject->SetActive(inArchipelagoMode && !online && !session.NeedsPasswordToReconnect());
        if (widgets.leaveButton)
            widgets.leaveButton->gameObject->SetActive(inArchipelagoMode || connecting);

        auto const& report = ContentResolver::Instance().GetReport();
        SetMessageText(widgets.contentText, inArchipelagoMode ? DescribeContent(report) : "");
        if (widgets.downloadButton) {
            widgets.downloadButton->gameObject->SetActive(inArchipelagoMode && online && !report.downloadableKeys.empty());
            widgets.downloadButton->interactable = !report.downloading && !report.checking;
        }
        RebuildStoreButtons(inArchipelagoMode ? report : ContentReport{});
    }
}
