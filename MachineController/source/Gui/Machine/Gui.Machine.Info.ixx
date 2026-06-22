module;

#include <string>

export module Gui.Machine.Info;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;

import Machine.Base;
import Gui.Machine.Telemetry;   // reuse Field + Group
import Gui.Machine.Connect;     // reuse button styles

export namespace Gui {

    using namespace Rev::Element;
    using namespace Rev::Appearance;

    // Pin Text to the element (there is also a Rev::Primitive::Text in scope).
    using Text = Rev::Element::Text;

    // A titled group whose body is one coordinate, shown inline: X Y Z A B.
    struct FrameRow : public Group {

        Field* x = nullptr; Field* y = nullptr; Field* z = nullptr; Field* a = nullptr; Field* b = nullptr;

        FrameRow(Element* parent, const std::string& name) : Group(parent, name) {
            x = new Field(body, "X"); y = new Field(body, "Y"); z = new Field(body, "Z");
            a = new Field(body, "A"); b = new Field(body, "B");
        }

        void set(const Machine::Coord& c) {
            x->set(c.x); y->set(c.y); z->set(c.z); a->set(c.a); b->set(c.b);
        }
    };

    // The Info section: the machine's request/response facts. A "Get Info" button
    // queries the machine; replies flow back through the onInfo channel and this
    // section reflects machine.info. Everything the domain does not decode yet
    // (identity, network) stays at its "---" default.
    struct InfoSection : public Box {

        static inline Style Section = {
            .layout     = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size       = { .width = 100_pct },
            .margin     = { .top = 12_px },
            .padding    = { 14_px, 14_px, 14_px, 14_px },
            .background = { .color = rgba(255, 255, 255, 0.03) },
            .border     = { .color = rgba(255, 255, 255, 0.08), .radius = 6_px, .width = 1_px }
        };

        static inline Style Heading = {
            .margin = { .bottom = 12_px },
            .text   = { .color = rgba(236, 238, 242, 1.0), .size = 15_px }
        };

        static inline Style Row = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False, CrossAlign::True },
            .size   = { .width = 100_pct },
            .margin = { .bottom = 6_px }
        };

        // Elements
        //--------------------------------------------------

        Box*    actionRow = nullptr;
            Button* getInfo = nullptr;

        Group* identityGroup = nullptr;
            Field* firmware = nullptr; Field* model = nullptr; Field* name = nullptr;

        Group* networkGroup = nullptr;
            Field* ip = nullptr; Field* mac = nullptr; Field* port = nullptr;

        FrameRow* g54 = nullptr; FrameRow* g55 = nullptr; FrameRow* g56 = nullptr;
        FrameRow* g57 = nullptr; FrameRow* g58 = nullptr; FrameRow* g59 = nullptr;
        FrameRow* g28 = nullptr; FrameRow* g30 = nullptr; FrameRow* g92 = nullptr;

        Group* offsetsGroup = nullptr;
            Field* spindleOffset = nullptr;
            Field* tlo           = nullptr;

        Group* probeGroup = nullptr;
            Field* probeX = nullptr; Field* probeY = nullptr; Field* probeZ = nullptr; Field* probeHit = nullptr;

        // State
        //--------------------------------------------------

        Machine::MachineBase& machine;
        bool                  dirty = false;   // an info reply arrived; reflect next compute

        // Create
        //--------------------------------------------------

        InfoSection(Element* parent, Machine::MachineBase& machine)
            : Box(parent, { &Section }, "InfoSection"), machine(machine) {

            new Text(this, "Info", { &Heading });

            actionRow = new Box(this, { &Row }, "ActionRow");
                getInfo = new Button(actionRow, { .label = "Get Info", .labelStyles = { &ConnectSection::BtnLabel, &ConnectSection::BtnLabelDisabled } }, { &ConnectSection::Btn, &ConnectSection::BtnHover, &ConnectSection::BtnPress, &ConnectSection::BtnDisabled });

            identityGroup = new Group(this, "Identity");
                firmware = new Field(identityGroup->body, "fw");
                model    = new Field(identityGroup->body, "model");
                name     = new Field(identityGroup->body, "name");

            networkGroup = new Group(this, "Network");
                ip   = new Field(networkGroup->body, "ip");
                mac  = new Field(networkGroup->body, "mac");
                port = new Field(networkGroup->body, "port");

            g54 = new FrameRow(this, "G54"); g55 = new FrameRow(this, "G55"); g56 = new FrameRow(this, "G56");
            g57 = new FrameRow(this, "G57"); g58 = new FrameRow(this, "G58"); g59 = new FrameRow(this, "G59");
            g28 = new FrameRow(this, "G28"); g30 = new FrameRow(this, "G30"); g92 = new FrameRow(this, "G92");

            offsetsGroup = new Group(this, "Offsets");
                spindleOffset = new Field(offsetsGroup->body, "spindle");
                tlo           = new Field(offsetsGroup->body, "TL0");

            probeGroup = new Group(this, "Last probe");
                probeX   = new Field(probeGroup->body, "X");
                probeY   = new Field(probeGroup->body, "Y");
                probeZ   = new Field(probeGroup->body, "Z");
                probeHit = new Field(probeGroup->body, "hit");

            // Drive + reflect.
            getInfo->onClick([this](Event&) { this->machine.queryAll(); });
            machine.info.onUpdate(this, [this]() { dirty = true; bump(); });

            // Known/static facts the machine set on construction -- show them now;
            // they need no query.
            spindleOffset->set(machine.info.frames.spindleOffset);
            model->set(machine.info.identity.model);
        }

        // Destroy
        //--------------------------------------------------

        ~InfoSection() { machine.unsubscribe(this); }

        // Reflect
        //--------------------------------------------------

        void bump() { if (shared && shared->event) { this->refresh(*shared->event); } }

        void computeChildren(Event& e) override {

            if (dirty) {
                reflect();
                dirty = false;
            }

            Box::computeChildren(e);
        }

        void reflect() {

            const auto& f = machine.info.frames;
            g54->set(f.g54); g55->set(f.g55); g56->set(f.g56);
            g57->set(f.g57); g58->set(f.g58); g59->set(f.g59);
            g28->set(f.g28); g30->set(f.g30); g92->set(f.g92);
            tlo->set(f.toolLengthOffset);
            spindleOffset->set(f.spindleOffset);

            const auto& p = machine.info.probe;
            probeX->set(p.position.x); probeY->set(p.position.y); probeZ->set(p.position.z);
            probeHit->set(std::string(p.triggered ? "yes" : "no"));

            // identity / network: not decoded yet -> left at "---".
        }
    };
}
