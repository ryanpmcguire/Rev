module;

#include <string>
#include <functional>

#include <managed.hpp>

#include <dbg.hpp>

export module Cam.Gui.RightPanel;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

import Cam.App;

import Cam.Gui.Tools;
import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace RightPanelStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 220_px, .height = Grow() },
            .margin = { 12_px, 12_px, 12_px, 12_px },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .border = { .radius = 6_px },
            .zIndex = +1
        };

        Style FolderButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { 0_px, 0_px, 0_px, 4_px },
            .padding = { 6_px, 10_px, 6_px, 10_px },
            .border = {
                .radius = 5_px,
                .width = 1_px
            },
            .cursor = Cursor::Hand
        };

        Style FolderButtonLabel = {
            .text = { .size = 12_px }
        };

        Style FolderButtonIcon = {
            .size = { 13_px, 13_px },
            .margin = { 0_px, 5_px, 0_px, 0_px }
        };

        Style ToolsHost = {
            .size = { .width = 100_pct, .height = Grow() }
        };
    }

    struct RightPanel : public Box {

        Cam::App::AppState* app = nullptr;

        Box* folderButton = nullptr;
        Svg* folderButtonIcon = nullptr;
        Text* folderButtonLabel = nullptr;

        Tools* tools = nullptr;

        std::function<void(Event&)> onSelectTool;
        std::function<void(Event&)> onSelectFolder;

        RightPanel(Element* parent, StyleList styles = {}) : Box(parent, styles, "RightPanel") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&RightPanelStyle::Self);
            this->styles.add(&Theme::Styles::Panel);
            this->styles.add(&Theme::Styles::PanelBorder);

            folderButton = new Box(
                this,
                Theme::withButton({
                    &RightPanelStyle::FolderButton,
                    &Theme::Styles::ButtonHover,
                    &Theme::Styles::ButtonPress
                }),
                "ToolFolderButton"
            );

            folderButtonLabel = new Text(
                folderButton,
                "Select Tool Folder",
                Theme::layer(
                    { &RightPanelStyle::FolderButtonLabel },
                    { &Theme::Styles::ButtonLabel }
                )
            );

            folderButtonIcon = new Svg(
                folderButton,
                File("./Choose-Folder.svg"),
                Theme::layer({
                    &RightPanelStyle::FolderButtonIcon,
                    &Theme::Styles::ChromeIconHover
                }, {
                    &Theme::Styles::ChromeIcon
                }),
                "ToolFolderButtonIcon"
            );

            folderButton->moveChild(
                folderButtonIcon,
                folderButtonLabel,
                true
            );

            folderButton->onClick([this](Event& e) {
                selectFolder(e);
                e.propagate = false;
            });

            tools = new Tools(
                this,
                { &RightPanelStyle::ToolsHost }
            );

            tools->onSelectTool = [this](Event& e) {
                if (onSelectTool) { onSelectTool(e); }
            };
        }

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

        std::string folderButtonText() {

            if (!app) { return "Select Tool Folder"; }

            std::string folder = app->toolFolderPath;

            if (folder.empty()) {
                return "General";
            }

            return truncate(
                basename(folder),
                20
            );
        }

        void selectFolder(Event& e) {

            if (!app) { return; }

            if (!app->selectToolFolder()) {
                return;
            }

            dbg("[RightPanel] tool folder: %s", app->toolFolderPath.c_str());

            if (onSelectFolder) {
                onSelectFolder(e);
            }

            refresh(e);
        }

        void computeChildren(Event& e) override {

            if (folderButtonLabel) {
                folderButtonLabel->content = folderButtonText();
            }

            Box::computeChildren(e);
        }
    };
}
