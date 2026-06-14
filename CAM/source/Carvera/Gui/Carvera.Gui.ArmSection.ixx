module;

#include <managed.hpp>

export module Carvera.Gui.ArmSection;

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

    // Arm section -- the ARM / SPINDLE ARM toggles and the START/STOP run button.
    // All three reflect Air state in computeChildren.
    struct ArmSection : public Box {

        static Carvera::Air& air() { return Carvera::Air::instance(); }

        Box*  armBtn      = nullptr;  Text* armLabel     = nullptr;
        Box*  spindleBtn  = nullptr;  Text* spindleLabel = nullptr;
        Box*  runBtn      = nullptr;  Text* runLabel     = nullptr;

        // Last applied states.  computeChildren only swaps a style when it
        // actually differs -- unconditional styles.add/remove marks the element
        // dirty even when the resolved style is identical, perpetuating draw.
        enum class RunKind { Unset, Idle, Armed, Executing };
        bool    lastArmedApplied_       = false;
        bool    lastArmedValid_         = false;
        int     lastSpindleApplied_     = 0;   // 0 = off, 1 = armed, 2 = locked
        bool    lastSpindleValid_       = false;
        RunKind lastRunApplied_         = RunKind::Unset;

        ArmSection(Element* parent)
            : Box(parent, Theme::withPanel({ &Style::Section }), "ArmSection")
        {
            build();
            subscribe();
        }

        void build() {

            Box* row = new Box(this, { &Style::ArmRow }, "ArmRow");

            armBtn = new Box(
                row,
                Theme::withButton({ &Style::ArmButton, &Style::BtnHover, &Style::BtnPress }),
                "ArmButton"
            );
            armLabel = new Text(armBtn, "ARM", Theme::withText({ &Style::ArmLabel }));
            armBtn->onClick([this](Event& e) { air().toggleArm(); refresh(e); e.propagate = false; });

            spindleBtn = new Box(
                row,
                Theme::withButton({ &Style::ArmButton, &Style::BtnHover, &Style::BtnPress }),
                "SpindleArmButton"
            );
            spindleLabel = new Text(spindleBtn, "SPINDLE ARM", Theme::withText({ &Style::ArmLabel }));
            spindleBtn->onClick([this](Event& e) { air().toggleSpindleArm(); refresh(e); e.propagate = false; });

            // Full-width START / STOP button directly below the arm row.
            runBtn = new Box(
                this,
                Theme::withButton({ &Style::ArmButton, &Style::BtnHover, &Style::BtnPress }),
                "RunButton"
            );
            runLabel = new Text(runBtn, "START", Theme::withText({ &Style::ArmLabel }));
            runBtn->onClick([this](Event& e) { air().requestStart(); refresh(e); e.propagate = false; });
        }

        void subscribe() {
            auto bump = [this](auto&) { if (shared && shared->event) { refresh(*shared->event); } };
            air().onArm       ([bump](Carvera::Air::ArmEvent&        e) { bump(e); });
            air().onState     ([bump](Carvera::Air::StateEvent&      e) { bump(e); });
            air().onConnection([bump](Carvera::Air::ConnectionEvent& e) { bump(e); });
            air().onStart     ([bump](Carvera::Air::StartEvent&      e) { bump(e); });

            // Spindle-safety interlock: a probe / spindle-disabled tool tried
            // (or a program/operator tried) to spin the spindle.  Air has
            // already forced M5 + stopped the program; surface it loudly here.
            air().onSafety([this](Carvera::Air::SafetyEvent&) {
                if (shared && shared->event) { refresh(*shared->event); }
            });
        }

        void computeChildren(Event& e) override {

            Carvera::Air& a = air();

            // -- ARM toggle --
            if (armBtn && armLabel) {
                const bool armed = a.isArmed();
                if (!lastArmedValid_ || armed != lastArmedApplied_) {
                    armBtn->styles.remove(&Style::ArmedBanner);
                    if (armed) { armBtn->styles.add(&Style::ArmedBanner); }
                    armLabel->content = armed ? "ARMED" : "ARM";
                    lastArmedApplied_ = armed;
                    lastArmedValid_   = true;
                }
            }

            // -- SPINDLE ARM toggle --
            // Arming is a USER control: it is ALWAYS available, even with a probe
            // loaded.  The interlock guarantees the spindle never actually spins
            // while inhibited (M3/M4 refused), so when the user has armed AND a
            // probe is loaded we show "ARMED (HELD)" -- armed by intent, held off
            // by the machine -- rather than pretending the control is disabled.
            if (spindleBtn && spindleLabel) {
                const bool inhibited    = a.isSpindleInhibited();
                const bool spindleArmed = a.isSpindleArmed();
                const int  spindleState = (spindleArmed ? 1 : 0) | (inhibited ? 2 : 0);
                if (!lastSpindleValid_ || spindleState != lastSpindleApplied_) {
                    spindleBtn->styles.remove(&Style::ArmedBanner);
                    if (spindleArmed) { spindleBtn->styles.add(&Style::ArmedBanner); }
                    spindleLabel->content =
                          (spindleArmed && inhibited) ? "SPINDLE ARMED (HELD)"
                        :  spindleArmed               ? "SPINDLE ARMED"
                                                      : "SPINDLE ARM";
                    lastSpindleApplied_ = spindleState;
                    lastSpindleValid_   = true;
                }
            }

            // -- RUN button (Idle / Armed / Executing) --
            if (runBtn && runLabel) {
                const bool executing = a.isExecuting();
                const bool armed     = a.isArmed();
                const RunKind kind = executing ? RunKind::Executing
                                  : armed     ? RunKind::Armed
                                              : RunKind::Idle;
                if (kind != lastRunApplied_) {
                    runBtn->styles.remove(&Style::ArmedBanner);
                    runBtn->styles.remove(&Style::RunBanner);
                    switch (kind) {
                        case RunKind::Executing: runBtn->styles.add(&Style::ArmedBanner); runLabel->content = "STOP";  break;
                        case RunKind::Armed:     runBtn->styles.add(&Style::RunBanner);   runLabel->content = "START"; break;
                        case RunKind::Idle:                                                runLabel->content = "START"; break;
                        case RunKind::Unset: break;
                    }
                    lastRunApplied_ = kind;
                }
            }

            Box::computeChildren(e);
        }
    };
}
