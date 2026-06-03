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

        // Keyboard plumbing: axis keys belong to the JogSection but ONLY when
        // it has the keyboard focus — i.e. when the operator has clicked into
        // the jog area (the section shows a blue focus ring then).  Without
        // focus the keys fall through to gestures and the rest of the panel,
        // so typing in (say) an origin name field can never trip jogging.
        bool jogHasFocus() const {
            return jogSection && jogSection->targetFlags.focus;
        }

        void keyDown(Event& e) override {

            if (originSection && originSection->isEditing()) {
                Box::keyDown(e);
                return;
            }

            if (jogHasFocus() && jogSection->handleKeyDown(e)) {
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

            // Always let the jog section observe key-ups so a release after the
            // user clicks away mid-jog still finalises the session cleanly.
            if (jogSection && jogSection->handleKeyUp(e)) {
                e.propagate = false;
                refresh(e);
                return;
            }

            Box::keyUp(e);
        }
    };
}
