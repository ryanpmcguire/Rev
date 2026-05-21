module;

#include <string>
#include <vector>
#include <functional>

export module Cam.Gui.Tools;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;

import Cam.App;
import Cam.App.Tool;

import Cam.Gui.Tool;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolsStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .height = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .zIndex = +1
        };

        Style Title = {
            .size = { 100_pct },
            .margin = { .bottom = 8_px },
            .text = { .color = rgba(0, 0, 0, 0.65), .size = 13_px }
        };

        Style List = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct },
            .overflow = Overflow::Hide
        };
    };

    using namespace ToolsStyle;

    struct Tools : public Box {

        Cam::App::AppState* app = nullptr;

        Text* title = nullptr;
        Box* list = nullptr;

        std::vector<ToolRow*> rows;

        std::function<void(Event&)> onSelectTool;

        Tools(Element* parent, StyleList styles = {}) : Box(parent, styles, "Tools") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&ToolsStyle::Self);

            title = new Text(this, "Tools", { &ToolsStyle::Title });
            list = new Box(this, { &ToolsStyle::List }, "ToolsList");
        }

        void selectTool(size_t index, Event& e) {

            if (!app) { return; }

            if (app->selectTool(index)) {

                if (onSelectTool) {
                    onSelectTool(e);
                }

                refresh(e);
            }
        }

        void computeChildren(Event& e) override {

            if (!app) {
                Box::computeChildren(e);
                return;
            }

            size_t oldSize = rows.size();
            size_t newSize = app->tools.size();

            for (size_t i = newSize; i < oldSize; i++) {
                delete rows[i];
            }

            rows.resize(newSize);

            for (size_t i = oldSize; i < newSize; i++) {

                rows[i] = new ToolRow(list);

                rows[i]->onSelect = [this](Event& ev, size_t index) {
                    this->selectTool(index, ev);
                };
            }

            for (size_t i = 0; i < newSize; i++) {

                Cam::App::Tool* tool = app->toolAt(i);

                rows[i]->setToolIndex(i);

                if (tool) {
                    rows[i]->setLabel(tool->name);
                }

                else {
                    rows[i]->setLabel("Invalid Tool");
                }
            }

            Box::computeChildren(e);
        }
    };
}
