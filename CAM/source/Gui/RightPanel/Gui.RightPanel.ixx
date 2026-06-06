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

        // The panel "frame": fully transparent (no background, no border). Its
        // padding is the floating gap around the content host below — same inset,
        // "floating" treatment as the left panel, without any margins.
        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 220_px, .height = Grow() },
            .padding = { 12_px, 12_px, 12_px, 12_px },
            .zIndex = +1
        };

        // The actual visible card: carries the panel background + border, fills
        // the frame (100% x 100%), and owns the scroll/clip plus the inner content
        // padding. All panel content lives in here.
        Style ContentHost = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct, .height = 100_pct, .max = { .height = 100_pct } },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .border = { .radius = 6_px },
            .overflow = Overflow::Hide,
            .scroll = Scroll::Vertical
        };

        // Mirrors the left panel's file button: full-width, fixed-height row with
        // a hairline border, so both panels read as one consistent toolset.
        Style FolderButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct, .height = 34_px },
            .margin = { .bottom = 8_px },
            .padding = { .left = 10_px, .right = 10_px, .top = 10_px, .bottom = 3_px },
            .border = {
                .width = 1_px,
                .radius = 5_px
            },
            .cursor = Cursor::Hand
        };

        Style FolderButtonLabel = {
            .text = { .size = 13_px }
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

        Box* contentHost = nullptr;

        Box* folderButton = nullptr;
        Svg* folderButtonIcon = nullptr;
        Text* folderButtonLabel = nullptr;

        Tools* tools = nullptr;

        std::function<void(Event&)> onSelectTool;
        std::function<void(Event&)> onSelectFolder;

        RightPanel(Element* parent, StyleList styles = {}) : Box(parent, styles, "RightPanel") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&RightPanelStyle::Self);

            // The visible, scrollable card. The panel background/border live here
            // (not on the transparent frame), so the frame's padding reads as a
            // floating inset around it.
            contentHost = new Box(this, {}, "RightPanelContent");
            contentHost->styles.add(&RightPanelStyle::ContentHost);
            contentHost->styles.add(&Theme::Styles::Panel);
            contentHost->styles.add(&Theme::Styles::PanelBorder);

            folderButton = new Box(
                contentHost,
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
                contentHost,
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
