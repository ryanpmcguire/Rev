module;

#include <string>
#include <deque>

#include <managed.hpp>

export module Carvera.Gui.LogSection;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;

import CarveraAir;

import Cam.Gui.Theme;
import Carvera.Gui.Style;

export namespace Carvera::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace Theme = Cam::Gui::Theme;

    // Rolling log of everything Air emits on its log channel: connect /
    // disconnect notices, sent commands, received non-status replies, errors.
    struct LogSection : public Box {

        static Carvera::Air& air() { return Carvera::Air::instance(); }

        static constexpr size_t MaxLogLines = 64;

        Text* logText = nullptr;
        std::deque<std::string> logLines_;

        LogSection(Element* parent)
            : Box(parent, Theme::withPanel({ &Style::LogBox }), "LogBox")
        {
            logText = new Text(this, "(no messages yet)", Theme::withMutedText({ &Style::LogLine }));

            air().onLog([this](Carvera::Air::LogEvent& e) {
                append(e.line);
                if (shared && shared->event) { refresh(*shared->event); }
            });
        }

        void append(const std::string& line) {
            logLines_.push_back(line);
            while (logLines_.size() > MaxLogLines) { logLines_.pop_front(); }
            if (logText) {
                std::string combined;
                for (auto& l : logLines_) { combined += l; combined += '\n'; }
                logText->content = combined;
            }
        }
    };
}
