module;

#include <string>

export module Gui.Machine.Telemetry;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;

import Machine.Base;
import Machine.Events;

export namespace Gui {

    using namespace Rev::Element;
    using namespace Rev::Appearance;

    // Pin Text to the element (there is also a Rev::Primitive::Text in scope).
    using Text = Rev::Element::Text;

    // A labelled value readout; the held value is settable via set().
    struct Field : public Box {

        static inline Style Self = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False, CrossAlign::True },
            .margin = { .right = 12_px, .bottom = 2_px }
        };

        static inline Style LabelStyle = {
            .margin = { .right = 4_px },
            .text   = { .color = rgba(120, 126, 136, 1.0), .size = 11_px }
        };

        static inline Style ValueStyle = {
            .text = { .color = rgba(214, 218, 224, 1.0), .size = 13_px }
        };

        Text* label = nullptr;
        Text* value = nullptr;

        Field(Element* parent, const std::string& name) : Box(parent, { &Self }, "Field") {
            label = new Text(this, name,  { &LabelStyle });
            value = new Text(this, "---", { &ValueStyle });
        }

        // For the later, connected version:
        void set(const std::string& v)     { value->setContent(v); }
        void set(float v, int digits = 3)  { value->setContent(v, digits); }
        void set(int v)                    { value->setContent(std::to_string(v)); }
        void clear()                       { value->setContent("---"); }
    };

    // A titled group: a 1px-underlined title over a row of inline fields.
    struct Group : public Box {

        static inline Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size   = { .width = 100_pct },
            .margin = { .bottom = 10_px }
        };

        static inline Style TitleStyle = {
            .size    = { .width = 100_pct },
            .padding = { .bottom = 3_px },
            .margin  = { .bottom = 6_px },
            .border  = { .bottom = { .color = rgba(255, 255, 255, 0.10), .width = 1_px } },
            .text    = { .color = rgba(150, 156, 166, 1.0), .size = 11_px }
        };

        static inline Style BodyStyle = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::True, CrossAlign::True },
            .size   = { .width = 100_pct }
        };

        Text* title = nullptr;
        Box*  body  = nullptr;

        Group(Element* parent, const std::string& name) : Box(parent, { &Self }, "Group") {
            title = new Text(this, name, { &TitleStyle });
            body  = new Box(this, { &BodyStyle }, "Body");
        }
    };

    // The Telemetry section: every value the machine can report, grouped. The
    // structure is declared once, all elements held by pointer. Lobotomised for
    // now -- every field reads "---" until wired to the machine's channels.
    struct TelemetrySection : public Box {

        static inline Style Section = {
            .layout     = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size       = { .width = 100_pct },
            .margin     = { .top = 12_px },
            .padding    = { 14_px, 14_px, 14_px, 14_px },
            .background = { .color = rgba(255, 255, 255, 0.03) },
            .border     = { .color = rgba(255, 255, 255, 0.08), .radius = 8_px, .width = 1_px }
        };

        static inline Style Heading = {
            .margin = { .bottom = 12_px },
            .text   = { .color = rgba(236, 238, 242, 1.0), .size = 15_px }
        };

        // Groups + their fields (all referenceable; indentation mirrors the tree).
        //--------------------------------------------------

        Group* statusGroup = nullptr;
            Field* state = nullptr;
            Field* halt  = nullptr;

        Group* jobGroup = nullptr;
            Field* jobLines   = nullptr;
            Field* jobPercent = nullptr;
            Field* jobSeconds = nullptr;

        Group* mposGroup = nullptr;
            Field* mposX = nullptr; Field* mposY = nullptr; Field* mposZ = nullptr; Field* mposA = nullptr; Field* mposB = nullptr;

        Group* wposGroup = nullptr;
            Field* wposX = nullptr; Field* wposY = nullptr; Field* wposZ = nullptr; Field* wposA = nullptr; Field* wposB = nullptr;

        Group* feedGroup = nullptr;
            Field* feedCurrent = nullptr; Field* feedTarget = nullptr; Field* feedScale = nullptr;

        Group* spindleGroup = nullptr;
            Field* spindleRpm = nullptr; Field* spindleTarget = nullptr; Field* spindleScale = nullptr;
            Field* spindleTemp = nullptr; Field* spindleLoad = nullptr;

        Group* toolGroup = nullptr;
            Field* toolNumber = nullptr; Field* toolOffset = nullptr;

        Group* probeGroup = nullptr;
            Field* probeVoltage = nullptr;

        Group* laserGroup = nullptr;
            Field* laserPower = nullptr; Field* laserTarget = nullptr; Field* laserScale = nullptr;

        // Reflection
        //--------------------------------------------------

        Machine::MachineBase& machine;
        std::string   stateText;
        bool          dirty = false;   // a telemetry/state edge arrived; reflect next compute

        // Create
        //--------------------------------------------------

        TelemetrySection(Element* parent, Machine::MachineBase& machine)
            : Box(parent, { &Section }, "TelemetrySection"), machine(machine) {

            new Text(this, "Telemetry", { &Heading });

            statusGroup = new Group(this, "Status");
                state = new Field(statusGroup->body, "state");
                halt  = new Field(statusGroup->body, "halt");

            jobGroup = new Group(this, "Job");
                jobLines   = new Field(jobGroup->body, "line");
                jobPercent = new Field(jobGroup->body, "pct");
                jobSeconds = new Field(jobGroup->body, "sec");

            mposGroup = new Group(this, "Machine Position");
                mposX = new Field(mposGroup->body, "X");
                mposY = new Field(mposGroup->body, "Y");
                mposZ = new Field(mposGroup->body, "Z");
                mposA = new Field(mposGroup->body, "A");
                mposB = new Field(mposGroup->body, "B");

            wposGroup = new Group(this, "Work Position");
                wposX = new Field(wposGroup->body, "X");
                wposY = new Field(wposGroup->body, "Y");
                wposZ = new Field(wposGroup->body, "Z");
                wposA = new Field(wposGroup->body, "A");
                wposB = new Field(wposGroup->body, "B");

            feedGroup = new Group(this, "Feed");
                feedCurrent = new Field(feedGroup->body, "cur");
                feedTarget  = new Field(feedGroup->body, "tgt");
                feedScale   = new Field(feedGroup->body, "ovr");

            spindleGroup = new Group(this, "Spindle");
                spindleRpm    = new Field(spindleGroup->body, "rpm");
                spindleTarget = new Field(spindleGroup->body, "tgt");
                spindleScale  = new Field(spindleGroup->body, "ovr");
                spindleTemp   = new Field(spindleGroup->body, "temp");
                spindleLoad   = new Field(spindleGroup->body, "load");

            toolGroup = new Group(this, "Tool");
                toolNumber = new Field(toolGroup->body, "#");
                toolOffset = new Field(toolGroup->body, "offset");

            probeGroup = new Group(this, "Probe");
                probeVoltage = new Field(probeGroup->body, "volts");

            laserGroup = new Group(this, "Laser");
                laserPower  = new Field(laserGroup->body, "pwr");
                laserTarget = new Field(laserGroup->body, "tgt");
                laserScale  = new Field(laserGroup->body, "ovr");

            // Subscribe AS `this`, so the destructor can drop these cleanly. Both
            // edges just flag + nudge a refresh; computeChildren reads the machine.
            // (onTelemetry is a no-payload signal: "it changed, go read the cache".)
            machine.telemetry.onUpdate(this, [this]()                   { dirty = true; bump(); });
            machine.onState           (this, [this](Machine::Event::State& e) { stateText = e.state; dirty = true; bump(); });
        }

        // Destroy
        //--------------------------------------------------

        // Drop our subscriptions before we die; the machine outlives us.
        ~TelemetrySection() { machine.unsubscribe(this); }

        // Reflect
        //--------------------------------------------------

        void bump() { if (shared && shared->event) { this->refresh(*shared->event); } }

        // Reflect only when an edge arrived (the dirty gate keeps this off the
        // per-frame path and out of a redraw loop).
        void computeChildren(Event& e) override {

            if (dirty) {
                reflect();
                dirty = false;
            }

            Box::computeChildren(e);
        }

        // Push cached telemetry + run-state into the fields.
        void reflect() {

            const Machine::MachineBase::Telemetry& t = machine.telemetry;

            mposX->set(t.spindle.pos.x); mposY->set(t.spindle.pos.y); mposZ->set(t.spindle.pos.z);
            mposA->set(t.spindle.pos.a); mposB->set(t.spindle.pos.b);

            wposX->set(t.tool.pos.x); wposY->set(t.tool.pos.y); wposZ->set(t.tool.pos.z);
            wposA->set(t.tool.pos.a); wposB->set(t.tool.pos.b);

            feedCurrent->set(t.tool.speed); feedTarget->set(t.tool.speedTarget); feedScale->set(t.tool.scale);

            spindleRpm->set(t.spindle.rpm);   spindleTarget->set(t.spindle.target); spindleScale->set(t.spindle.scale);
            spindleTemp->set(t.spindle.temp); spindleLoad->set(t.spindle.load);

            toolNumber->set(t.tool.number); toolOffset->set(t.tool.offset);

            probeVoltage->set(t.probe.voltage);

            laserPower->set(t.laser.power); laserScale->set(t.laser.scale);

            state->set(stateText.empty() ? "---" : stateText);
        }
    };
}
