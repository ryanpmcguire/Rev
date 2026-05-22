module;

#include <string>
#include <vector>
#include <functional>

#include <dbg.hpp>

export module Cam.Gui.ToolSettingsWindow;

import Rev.Window;
import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.TextInput;
import Rev.Element.NumberInput;
import Rev.Element.Dropdown;
import Rev.Element.Button;

import Cam.App;
import Cam.App.Tool;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolSettingsStyle {

        Shadow panelShadow = {
            .color = rgba(15, 23, 42, 0.14),
            .size = Px(-8),
            .blur = 28_px
        };

        Style Root = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct, 100_pct },
            .background = { .color = rgba(246, 247, 251, 1.0) },
            .border = { .radius = 10_px },
            .shadow = panelShadow
        };

        Style Header = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct },
            .padding = { 22_px, 22_px, 18_px, 18_px },
            .background = { .color = rgba(28, 34, 48, 1.0) }
        };

        Style HeaderEyebrow = {
            .text = { .color = rgba(148, 163, 184, 1.0), .size = 11_px }
        };

        Style HeaderTitle = {
            .margin = { 6_px, 0_px, 0_px, 0_px },
            .text = { .color = rgba(248, 250, 252, 1.0), .size = 21_px }
        };

        Style Body = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { Grow() },
            .padding = { 20_px, 22_px, 8_px, 22_px }
        };

        Style SectionLabel = {
            .margin = { 4_px, 0_px, 10_px, 0_px },
            .text = { .color = rgba(100, 116, 139, 1.0), .size = 11_px }
        };

        Style Footer = {
            .layout = { Axis::Horizontal, Align::End, Align::Center, Wrap::False },
            .size = { 100_pct },
            .padding = { 14_px, 22_px, 20_px, 22_px },
            .background = { .color = rgba(255, 255, 255, 1.0) },
            .border = {
                .top = {
                    .color = rgba(226, 232, 240, 1.0),
                    .width = 1_px
                }
            }
        };

        Style FooterButton = {
            .margin = { 0_px, 0_px, 0_px, 10_px }
        };

        Style FooterButtonPrimary = {
            .size = { .width = 128_px, .height = 40_px }
        };

        Style FooterButtonSecondary = {
            .size = { .width = 96_px, .height = 40_px }
        };
    }

    struct ToolSettingsWindow : public Rev::Window {

        Cam::App::AppState* app = nullptr;
        std::string toolName;

        Text* headerEyebrow = nullptr;
        Text* headerTitle = nullptr;
        TextInput* nameInput = nullptr;
        Dropdown* typeDropdown = nullptr;
        NumberInput* diameterInput = nullptr;
        NumberInput* lengthInput = nullptr;

        std::function<void(Event&)> onSaved;

         static std::string windowTitleFor(const std::string& toolName) {
            return toolName + " - Settings";
        }

        static Rev::Window* rootWindow(Element* from) {

            Element* node = from;

            while (node && node->parent && node->parent != node) {
                node = node->parent;
            }

            return static_cast<Rev::Window*>(node);
        }

        static Cam::App::Tool::Type typeFromDropdownValue(
            const std::string& value
        ) {
            return Cam::App::Tool::typeFromKindString(value);
        }

        void updateHeaderEyebrow(Cam::App::Tool::Type type) {

            if (!headerEyebrow) { return; }

            headerEyebrow->content =
                Cam::App::Tool::typeEyebrow(type);
        }

        void applyWindowTitle() {

            std::string title = windowTitleFor(toolName);
            setTitle(title);
        }

        ToolSettingsWindow(
            std::vector<void*>& windowGroup,
            Rev::Window* owner,
            const std::string& initialToolName
        ) : Rev::Window(
            windowGroup,
            {
                .name = initialToolName + " - Settings",
                .size = { .width = 400, .height = 480 }
            }
        ) {
            toolName = initialToolName;

            if (owner && owner->shared) {
                shared->state = owner->shared->state;
            }

            app = Cam::App::AppState::Get(shared->state);

            style->layout = {
                Axis::Vertical, Align::Start, Align::Start, Wrap::False
            };
            style->size = { .width = 100_pct, .height = 100_pct };
            styles.add(&ToolSettingsStyle::Root);

            buildUi();

            setTitle(toolName + " - Settings");

            if (owner) {
                setPos(owner->details.x + 280, owner->details.y + 96);
            } else {
                setPos(280, 96);
            }

            show();
            refresh(event);
        }

        void buildUi() {
            Cam::App::Tool* tool = app
                ? app->toolLibrary()->find(toolName)
                : nullptr;

            std::string displayName = tool ? tool->name : toolName;
            Cam::App::Tool::Type selectedType = tool
                ? tool->type
                : Cam::App::Tool::Type::EndMill;

            Box* header = new Box(
                this,
                { &ToolSettingsStyle::Header },
                "Header"
            );

            headerEyebrow = new Text(
                header,
                Cam::App::Tool::typeEyebrow(selectedType),
                { &ToolSettingsStyle::HeaderEyebrow }
            );

            headerTitle = new Text(
                header,
                displayName,
                { &ToolSettingsStyle::HeaderTitle }
            );

            Box* body = new Box(
                this,
                { &ToolSettingsStyle::Body },
                "Body"
            );

            nameInput = new TextInput(
                body,
                {
                    .label = "Name",
                    .placeholder = "Tool name",
                    .maxLength = 64,
                    .selectAllOnFocus = true
                }
            );

            typeDropdown = new Dropdown(
                body,
                {
                    .label = "Type",
                    .options = {
                        {
                            Cam::App::Tool::typeDisplayName(
                                Cam::App::Tool::Type::EndMill
                            ),
                            "EndMill"
                        },
                        {
                            Cam::App::Tool::typeDisplayName(
                                Cam::App::Tool::Type::ThreadMill
                            ),
                            "ThreadMill"
                        },
                        {
                            Cam::App::Tool::typeDisplayName(
                                Cam::App::Tool::Type::Chamfer
                            ),
                            "Chamfer"
                        }
                    },
                    .placeholder = "Select type",
                    .value = Cam::App::Tool::typeToKindString(selectedType)
                }
            );

            typeDropdown->onChange = [this](Event& e) {
                const auto type = Cam::App::Tool::typeFromKindString(
                    typeDropdown->params.value
                );

                headerEyebrow->content = Cam::App::Tool::typeEyebrow(type);
                refresh(e);
            };

            new Text(
                body,
                "GEOMETRY",
                { &ToolSettingsStyle::SectionLabel }
            );

            NumberInput::Params diameterParams;
            diameterParams.label = "Diameter";
            diameterParams.placeholder = "1.0";
            diameterParams.maxLength = 32;
            diameterParams.selectAllOnFocus = true;
            diameterParams.allowNegative = false;
            diameterParams.allowDecimal = true;
            diameterParams.allowEmpty = false;
            diameterParams.maxDecimalPlaces = 4;

            diameterInput = new NumberInput(body, diameterParams);

            NumberInput::Params lengthParams;
            lengthParams.label = "Stickout length";
            lengthParams.placeholder = "100";
            lengthParams.maxLength = 32;
            lengthParams.selectAllOnFocus = true;
            lengthParams.allowNegative = false;
            lengthParams.allowDecimal = true;
            lengthParams.allowEmpty = false;
            lengthParams.maxDecimalPlaces = 4;

            lengthInput = new NumberInput(body, lengthParams);

            if (tool) {
                nameInput->text->content = tool->name;
                diameterInput->setValue(tool->diameter);
                lengthInput->setValue(tool->length);
            }

            Box* footer = new Box(
                this,
                { &ToolSettingsStyle::Footer },
                "Footer"
            );

            Button* cancelButton = new Button(
                footer,
                Button::Params::Secondary("Cancel"),
                {
                    &ToolSettingsStyle::FooterButton,
                    &ToolSettingsStyle::FooterButtonSecondary
                }
            );

            cancelButton->onClick([this](Event& e) {
                close();
                e.propagate = false;
            });

            Button* saveButton = new Button(
                footer,
                Button::Params::Primary("Save changes"),
                {
                    &ToolSettingsStyle::FooterButton,
                    &ToolSettingsStyle::FooterButtonPrimary
                }
            );

            saveButton->onClick([this](Event& e) {
                save(e);
                e.propagate = false;
            });
        }

        void save(Event& e) {
            if (!app) {
                dbg("[ToolSettings] Missing app state");
                return;
            }

            const std::string newName = nameInput->text->content.get();

            if (newName.empty()) {
                dbg("[ToolSettings] Name is required");
                return;
            }

            diameterInput->commit(e);
            lengthInput->commit(e);

            double diameter = 0.0;
            double length = 0.0;

            if (!diameterInput->tryGetValue(diameter) || diameter <= 0.0) {
                dbg("[ToolSettings] Invalid diameter");
                return;
            }

            if (!lengthInput->tryGetValue(length) || length <= 0.0) {
                dbg("[ToolSettings] Invalid length");
                return;
            }

            const auto type = Cam::App::Tool::typeFromKindString(
                typeDropdown->params.value
            );

            if (!app->saveTool(toolName, newName, type, diameter, length)) {
                dbg("[ToolSettings] Failed to save tool");
                return;
            }

            toolName = newName;
            headerTitle->content = newName;
            headerEyebrow->content = Cam::App::Tool::typeEyebrow(type);
            setTitle(toolName + " - Settings");

            dbg("[ToolSettings] Saved \"%s\"", toolName.c_str());

            if (onSaved) {
                onSaved(e);
            }

            refresh(e);
        }

        void close() {
            shouldClose = true;
        }

        void onClose(bool& rejectClose) override {
            rejectClose = false;
            shouldClose = true;
        }
    };
}
