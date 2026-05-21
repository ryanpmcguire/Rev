module;

#include <string>
#include <vector>
#include <functional>

#include <dbg.hpp>

export module Cam.Gui.LeftPanel;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;

import Cam.App;
import Cam.App.Project;

import Cam.Gui.MaterialStates;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace LeftPanelStyle {

        Shadow subtleShadow = {
            .color = rgba(0, 0, 0, 0.35),
            .size = Px(-8),
            .blur = 16_px
        };

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 220_px, .height = Grow() },
            .margin = { 12_px, 12_px, 12_px, 12_px },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            //.background = { .color = rgba(0, 0, 0, 0.10) },
            .border = { .radius = 6_px },
            .shadow = subtleShadow,
            .zIndex = +1
        };

        Style FileButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct, .height = 34_px },
            .margin = { 0_px, 0_px, 0_px, 8_px },
            .padding = { 10_px, 10_px, 3_px, 2_px },
            .background = { .color = rgba(255, 255, 255, 0.24), .transition = 100_ms },
            .border = {
                .color = rgba(0, 0, 0, 0.22),
                .width = 1_px,
                .radius = 5_px
            },
            .cursor = Cursor::Hand
        };

        Style FileButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(255, 255, 255, 0.42) },
            .border = {
                .color = rgba(0, 0, 0, 0.32)
            }
        };

        Style FileButtonPress = {
            .applies = { .press = true },
            .background = { .color = rgba(255, 255, 255, 0.56) },
            .border = {
                .color = rgba(0, 0, 0, 0.42)
            }
        };

        Style FileButtonLabel = {
            .text = {
                .color = rgba(0, 0, 0, 0.74),
                .size = 13_px
            }
        };

        Style MaterialStateHost = {
            .size = { .width = 100_pct, .height = Grow() }
        };
    }

    struct LeftPanel : public Box {

        Cam::App::AppState* app = nullptr;

        Box* fileButton = nullptr;
        Text* fileButtonLabel = nullptr;

        MaterialStates* materialStates = nullptr;

        std::function<void(Event&)> onSelectFile;
        std::function<void(Event&)> onBeforeSelectFile;
        std::function<void(Event&)> onSelectState;
        std::function<void(Event&)> onDeleteState;

        // Create
        //--------------------------------------------------

        LeftPanel(Element* parent, StyleList styles = {}) : Box(parent, styles, "LeftPanel") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&LeftPanelStyle::Self);

            fileButton = new Box(
                this,
                {
                    &LeftPanelStyle::FileButton,
                    &LeftPanelStyle::FileButtonHover,
                    &LeftPanelStyle::FileButtonPress
                },
                "ProjectFileButton"
            );

            fileButtonLabel = new Text(
                fileButton,
                "Select File",
                { &LeftPanelStyle::FileButtonLabel }
            );

            fileButton->onClick([this](Event& e) {
                selectFile(e);
                e.propagate = false;
            });

            materialStates = new MaterialStates(
                this,
                { &LeftPanelStyle::MaterialStateHost }
            );

            materialStates->onSelectState = [this](Event& e) {
                if (onSelectState) { onSelectState(e); }
            };

            materialStates->onDeleteState = [this](Event& e) {
                if (onDeleteState) { onDeleteState(e); }
            };
        }

        // Project access
        //--------------------------------------------------

        Cam::App::Project* activeProject() {

            if (!app) { return nullptr; }

            return app->activeProject;
        }

        // File display
        //--------------------------------------------------

        static std::string basename(const std::string& path) {

            if (path.empty()) { return ""; }

            size_t slash = path.find_last_of("/\\");

            if (slash == std::string::npos) {
                return path;
            }

            return path.substr(slash + 1);
        }

        static std::string truncate(
            const std::string& value,
            size_t maxChars = 20
        ) {
            if (value.size() <= maxChars) {
                return value;
            }

            if (maxChars <= 3) {
                return value.substr(0, maxChars);
            }

            return value.substr(0, maxChars - 3) + "...";
        }

        std::string fileButtonText() {

            Cam::App::Project* project = activeProject();

            if (!project) { return "Select File"; }

            std::string path = project->file.pathname;

            if (path.empty()) {
                return "Select File";
            }

            return truncate(
                basename(path),
                20
            );
        }

        void selectFile(Event& e) {

            Cam::App::Project* project = activeProject();

            if (!project) { return; }

            if (onBeforeSelectFile) {
                onBeforeSelectFile(e);
            }

            if (!project->selectStepFile()) {

                if (onSelectFile) {
                    onSelectFile(e);
                }

                return;
            }

            if (onSelectFile) {
                onSelectFile(e);
            }
        }

        // Compute
        //--------------------------------------------------

        void computeChildren(Event& e) override {

            if (fileButtonLabel) {
                fileButtonLabel->content = fileButtonText();
            }

            Box::computeChildren(e);
        }
    };
}