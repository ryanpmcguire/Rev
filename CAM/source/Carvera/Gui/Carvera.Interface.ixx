module;

#include <managed.hpp>

export module Carvera.Gui.Interface;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;

import Rev.Element.Event.GestureTracker;

import CarveraAir;

import Cam.App;
import Cam.Gui.Theme;

import Carvera.Gui.Style;
import Carvera.Gui.ConnectionSection;
import Carvera.Gui.ArmSection;
import Carvera.Gui.ToolSection;
import Carvera.Gui.JogSection;
import Carvera.Gui.OriginSection;
import Carvera.Gui.LogSection;

export namespace Carvera::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace Theme = Cam::Gui::Theme;

    enum class CarveraCommand { Connect, Disconnect, Unlock, Reset };

    // ------------------------------------------------------------------
    // Interface — the Carvera Air control panel.
    //
    // This is a *thin shell* that owns six independent sections (Connection,
    // Arm, Tool, Jog, Origin, Log) and panel-wide keyboard plumbing.  It
    // does not talk to the machine: every section subscribes to whatever
    // Air events it cares about and updates itself in its own
    // computeChildren.
    // ------------------------------------------------------------------

    struct Interface : public Box {

        static Carvera::Air& air() { return Carvera::Air::instance(); }

        // Sections (owned by the framework once parented).  Held as pointers
        // so the panel can route keyboard input to the right one.
        ConnectionSection* connectionSection = nullptr;
        ArmSection*        armSection        = nullptr;
        ToolSection*       toolSection       = nullptr;
        JogSection*        jogSection        = nullptr;
        OriginSection*     originSection     = nullptr;
        LogSection*        logSection        = nullptr;

        // Panel-level shortcuts: connect/disconnect/unlock/reset gestures.
        GestureTracker<CarveraCommand> gestures = {{
            { "cn",     CarveraCommand::Connect    },
            { "ctrl+n", CarveraCommand::Connect    },
            { "dc",     CarveraCommand::Disconnect },
            { "ctrl+d", CarveraCommand::Disconnect },
            { "un",     CarveraCommand::Unlock     },
            { "ctrl+u", CarveraCommand::Unlock     },
            { "rs",     CarveraCommand::Reset      },
            { "ctrl+r", CarveraCommand::Reset      },
        }};

        Interface(Element* parent, Cam::App::AppState* app = nullptr)
            : Box(parent, {}, "CarveraInterface")
        {
            Theme::applyMode(Theme::Mode::Dark);
            this->styles.add(&Style::Root);
            this->styles.add(&Theme::Styles::Background);
            this->tabStop = true;

            connectionSection = new ConnectionSection(this);
            armSection        = new ArmSection(this);
            toolSection       = new ToolSection(this, app);
            jogSection        = new JogSection(this);
            originSection     = new OriginSection(this, app);
            logSection        = new LogSection(this);

            gestures.onGesture = [this](CarveraCommand cmd, Event& e) {
                switch (cmd) {
                    case CarveraCommand::Connect:    air().connect();    break;
                    case CarveraCommand::Disconnect: air().disconnect(); break;
                    case CarveraCommand::Unlock:     air().unlock();     break;
                    case CarveraCommand::Reset:      air().reset();      break;
                }
                refresh(e);
            };
        }

        // Keyboard plumbing: never steal keys while an Origin field is being
        // edited; otherwise let JogSection consume axis keys (arrows, q/e for
        // Z, z/x for A) and pass everything else to the gesture tracker.
        void keyDown(Event& e) override {

            if (originSection && originSection->isEditing()) {
                Box::keyDown(e);
                return;
            }

            if (jogSection && jogSection->handleKeyDown(e)) {
                e.propagate = false;
                refresh(e);
                return;
            }

            if (gestures.track(e)) { e.propagate = false; return; }

            Box::keyDown(e);
        }

        void keyUp(Event& e) override {

            if (originSection && originSection->isEditing()) {
                Box::keyUp(e);
                return;
            }

            if (jogSection && jogSection->handleKeyUp(e)) {
                e.propagate = false;
                refresh(e);
                return;
            }

            Box::keyUp(e);
        }
    };
}
