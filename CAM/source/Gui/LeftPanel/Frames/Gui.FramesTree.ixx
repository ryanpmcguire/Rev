module;

#include <string>
#include <cmath>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.FramesTree;

import Rev.Core.Resource;
import Rev.OS.File;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Element.Collapsible;
import Rev.Element.Button;
import Rev.Element.ControlTheme;

import Cam.App;
import Cam.App.MachineProfile;
import Cam.Gui.Theme;
import Cam.Gui.LockableNumberInput;

import CarveraAir;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace FramesTreeStyle {

        Style HeaderEyeButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .left = 4_px, .right = 2_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style Eye = { .size = { 15_px, 15_px } };

        // X Y Z (+A) inputs and the Set button, side by side, BOTTOM-aligned:
        // crossAlign=True enables per-member cross-axis (vertical) alignment, and
        // vertical=End drops the short Set button to the bottom of the row, level
        // with the input boxes (not their labels).
        Style FieldRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::End, Wrap::False, CrossAlign::True },
            .size = { .width = 100_pct },
            .padding = { .left = 4_px, .right = 4_px, .top = 2_px, .bottom = 4_px }
        };

        Style FieldCell = {
            .size = { .width = Grow() },
            .margin = { .right = 4_px }
        };

        // The inline Set button: matches the input boxes' HEIGHT by using the SAME
        // vertical padding as the Field style (top 6 / bottom 5) -- the Button*
        // theme styles otherwise force 7/9, making it taller.  Kept LAST in the
        // button's style list (re-asserted after every colour toggle) so it always
        // wins over ButtonPrimary/Secondary's padding.
        Style SetButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .padding = { .left = 12_px, .right = 12_px, .top = 6_px, .bottom = 5_px },
            .margin = { .bottom = 1_px }
        };
    };

    // The "Frames" panel: each coordinate system (machine / rotary / work / part)
    // is its own collapsible, with the visibility eye IN the header, a row of
    // X Y Z (+A) lockable inputs, and an inline blue/grey "Set" button at the end
    // of the row.  Each field has its own lock toggle (locked = display-only);
    // sensible defaults are locked, but any field can be unlocked to edit.  "Set"
    // captures the live machine position into the currently-UNLOCKED fields.
    struct FramesTree : public Box {

        enum Frame { Machine = 0, Rotary, Work, Part, FrameCount };

        Cam::App::AppState* app = nullptr;

        static Carvera::Air& air() { return Carvera::Air::instance(); }

        struct Row {
            Collapsible* node = nullptr;
            Box*  eyeButton = nullptr;
            Svg*  eye = nullptr;
            LockableNumberInput* in[4] = {};   // X, Y, Z, A
            bool  hasA = false;
            Button* setBtn = nullptr;
            bool  setBlue = false;
        };

        Row rows[FrameCount];

        Rev::Core::Resource eyeOnResource;
        Rev::Core::Resource eyeOffResource;

        std::function<void(Event&)> onChanged;

        FramesTree(Element* parent, StyleList styles = {}) : Box(parent, styles, "FramesTree") {

            app = Cam::App::AppState::Get(shared->state);

            eyeOnResource  = File("CAM/source/Gui/LeftPanel/Stages/Eye.svg");
            eyeOffResource = File("CAM/source/Gui/LeftPanel/Stages/Eye-Off.svg");

            // hasA, then default lock state for X / Y / Z / A.
            buildRow(Machine, "Machine frame", false, true,  true,  true,  true);
            buildRow(Rotary,  "Rotary axis",   true,  false, true,  true,  true);
            buildRow(Work,    "Work frame",    true,  false, true,  true,  false);
            buildRow(Part,    "Part frame",    false, false, false, false, true);

            refreshFields();
        }

        void buildRow(Frame f, const std::string& title, bool hasA,
                      bool lX, bool lY, bool lZ, bool lA) {

            Row& r = rows[f];
            r.hasA = hasA;
            const bool lockedDefault[4] = { lX, lY, lZ, lA };

            r.node = new Collapsible(this, title, { &CollapsibleStyle::Self }, /*open*/ false);
            if (r.node->arrow)     { r.node->arrow->styles.add(&Theme::Styles::Icon); }
            if (r.node->titleText) { r.node->titleText->styles.add(&Theme::Styles::Text); }

            // Visibility eye in the collapsible header.
            r.eyeButton = new Box(r.node->header, { &FramesTreeStyle::HeaderEyeButton }, "FrameEyeButton");
            r.eye = new Svg(r.eyeButton, eyeOnResource,
                            Theme::layer({ &FramesTreeStyle::Eye }, { &Theme::Styles::Text }), "FrameEye");
            r.eye->opacity = 0.5f;
            r.eyeButton->onClick([this, f](Event& e) {
                e.propagate = false;
                if (bool* v = visField(f)) { *v = !*v; }
                if (onChanged) { onChanged(e); }
                refresh(e);
            });

            // One row: X Y Z (+A) lockable inputs, then the inline Set button.
            Box* fieldRow = new Box(r.node->container, { &FramesTreeStyle::FieldRow }, "FrameFieldRow");

            const char* names[4] = { "X", "Y", "Z", "A" };
            const int   count    = hasA ? 4 : 3;
            for (int i = 0; i < count; i++) {
                r.in[i] = new LockableNumberInput(
                    fieldRow, names[i], lockedDefault[i], { &FramesTreeStyle::FieldCell });
                r.in[i]->onValueChange = [this, f](Event& e, std::optional<double>) {
                    commitFields(f, e);
                };
            }

            // The Set button (the machine frame is the fixed datum -- no Set).
            if (f != Machine) {
                r.setBtn = new Button(fieldRow, { .label = "Set" },
                                      { &ControlTheme::ButtonSecondary,
                                        &ControlTheme::ButtonSecondaryHover,
                                        &FramesTreeStyle::SetButton });
                if (r.setBtn->labelText) { r.setBtn->labelText->styles.add(&ControlTheme::ButtonSecondaryLabel); }
                r.setBtn->onClick([this, f](Event& e) {
                    e.propagate = false;
                    applySetFromLive(f, e);
                });
            }
        }

        bool* visField(Frame f) {
            if (!app) { return nullptr; }
            switch (f) {
                case Machine: return &app->frameVisible.machine;
                case Rotary:  return &app->frameVisible.rotary;
                case Work:    return &app->frameVisible.work;
                case Part:    return &app->frameVisible.part;
                default:      return nullptr;
            }
        }

        // Currently STORED values for a frame (shown in the fields).
        bool readModel(Frame f, double& x, double& y, double& z, double& a) {
            x = y = z = a = 0.0;
            switch (f) {
                case Machine:
                    return true;
                case Rotary: {
                    Cam::App::MachineProfile* m = app ? app->selectedMachine() : nullptr;
                    if (!m) { return false; }
                    x = m->rotaryAxisX; y = m->rotaryAxisY; z = m->rotaryAxisZ;
                    return true;
                }
                case Work: {
                    float mx, my, mz, ma;
                    if (!air().tipOrigin(mx, my, mz, ma)) { return false; }
                    x = mx; y = my; z = mz; a = ma;
                    return true;
                }
                case Part: {
                    Cam::App::Project* prj = app ? app->activeProject : nullptr;
                    if (!prj) { return false; }
                    x = prj->probeCorrection.t.x;
                    y = prj->probeCorrection.t.y;
                    z = prj->probeCorrection.t.z;
                    return true;
                }
                default: return false;
            }
        }

        // What "Set" would apply: the live machine position in the frame's reference.
        bool liveTarget(Frame f, double& x, double& y, double& z, double& a) {
            x = y = z = a = 0.0;
            switch (f) {
                case Machine:
                    return false;
                case Rotary: {
                    float mx, my, mz, ma;
                    if (!air().telemetry(mx, my, mz, ma)) { return false; }
                    x = mx; y = my; z = mz;
                    return true;
                }
                case Work: {
                    float tx, ty, tz, ta;
                    if (!air().currentTip(tx, ty, tz, ta)) { return false; }
                    x = tx; y = ty; z = tz; a = ta;
                    // Y/Z default to the rotary axis (in the WPos frame) unless the
                    // operator has unlocked them to set explicitly.
                    if (Cam::App::MachineProfile* m = app ? app->selectedMachine() : nullptr) {
                        float mmx, mmy, mmz, mma, wwx, wwy, wwz, wwa;
                        double dY = 0.0, dZ = 0.0;
                        if (air().telemetry(mmx, mmy, mmz, mma) && air().tipTelemetry(wwx, wwy, wwz, wwa)) {
                            dY = mmy - wwy; dZ = mmz - wwz;
                        }
                        double py = y, pz = z;
                        m->pinWorkOriginYZ(py, pz, dY, dZ);
                        if (rows[Work].in[1] && rows[Work].in[1]->locked()) { y = py; }
                        if (rows[Work].in[2] && rows[Work].in[2]->locked()) { z = pz; }
                    }
                    return true;
                }
                case Part: {
                    float tx, ty, tz, ta;
                    float ox, oy, oz, oa;
                    if (!air().currentTip(tx, ty, tz, ta) || !air().tipOrigin(ox, oy, oz, oa)) {
                        return false;
                    }
                    x = tx - ox; y = ty - oy; z = tz - oz;
                    return true;
                }
                default: return false;
            }
        }

        // Apply the UNLOCKED fields to the data model.
        void commitFields(Frame f, Event& e) {
            Row& r = rows[f];
            auto val = [&](int i, double fallback) {
                return (r.in[i] && !r.in[i]->locked()) ? r.in[i]->valueOr(fallback) : fallback;
            };

            double cx, cy, cz, ca;
            if (!readModel(f, cx, cy, cz, ca)) { return; }
            const double x = val(0, cx), y = val(1, cy), z = val(2, cz), a = val(3, ca);

            switch (f) {
                case Rotary: {
                    Cam::App::MachineProfile* m = app ? app->selectedMachine() : nullptr;
                    if (!m) { return; }
                    Cam::App::MachineProfile src = *m;
                    src.rotaryAxisX = x; src.rotaryAxisY = y; src.rotaryAxisZ = z;
                    src.rotaryAxisCalibrated = true;
                    if (app) { app->saveMachine(m->name, src, {}); }
                    break;
                }
                case Work:
                    air().setWorkOrigin((float)x, (float)y, (float)z, (float)a);
                    break;
                case Part: {
                    Cam::App::Project* prj = app ? app->activeProject : nullptr;
                    if (!prj) { return; }
                    prj->probeCorrection.t = { (float)x, (float)y, (float)z };
                    prj->probeCorrection.valid = true;
                    prj->markDirty();
                    break;
                }
                default: break;
            }

            if (onChanged) { onChanged(e); }
            refresh(e);
        }

        // Set button: push the live position into the unlocked fields, then commit.
        void applySetFromLive(Frame f, Event& e) {
            Row& r = rows[f];
            double lx, ly, lz, la;
            if (!liveTarget(f, lx, ly, lz, la)) { return; }
            const double v[4] = { lx, ly, lz, la };
            for (int i = 0; i < 4; i++) {
                if (r.in[i] && !r.in[i]->locked()) { r.in[i]->setValue(v[i]); }
            }
            commitFields(f, e);
        }

        void refreshFields() {
            for (int fi = 0; fi < FrameCount; fi++) {
                Frame f = (Frame)fi;
                Row& r = rows[f];
                double x, y, z, a;
                if (!readModel(f, x, y, z, a)) { continue; }
                const double v[4] = { x, y, z, a };
                for (int i = 0; i < 4; i++) {
                    if (r.in[i]) { r.in[i]->setValue(v[i]); }
                }
            }
        }

        // Would "Set" change anything? (any unlocked field != live position).
        bool setWouldChange(Frame f) {
            Row& r = rows[f];
            if (!r.setBtn) { return false; }
            double lx, ly, lz, la;
            if (!liveTarget(f, lx, ly, lz, la)) { return false; }
            const double live[4] = { lx, ly, lz, la };
            for (int i = 0; i < 4; i++) {
                if (!r.in[i] || r.in[i]->locked()) { continue; }
                if (std::fabs(r.in[i]->valueOr(live[i]) - live[i]) > 1e-4) { return true; }
            }
            return false;
        }

        void setButtonColour(Row& r, bool blue) {
            if (!r.setBtn || blue == r.setBlue) { return; }
            r.setBlue = blue;
            Text* label = r.setBtn->labelText;
            if (blue) {
                r.setBtn->styles.remove(&ControlTheme::ButtonSecondary);
                r.setBtn->styles.remove(&ControlTheme::ButtonSecondaryHover);
                r.setBtn->styles.add(&ControlTheme::ButtonPrimary);
                r.setBtn->styles.add(&ControlTheme::ButtonPrimaryHover);
                if (label) {
                    label->styles.remove(&ControlTheme::ButtonSecondaryLabel);
                    label->styles.add(&ControlTheme::ButtonPrimaryLabel);
                }
            }
            else {
                r.setBtn->styles.remove(&ControlTheme::ButtonPrimary);
                r.setBtn->styles.remove(&ControlTheme::ButtonPrimaryHover);
                r.setBtn->styles.add(&ControlTheme::ButtonSecondary);
                r.setBtn->styles.add(&ControlTheme::ButtonSecondaryHover);
                if (label) {
                    label->styles.remove(&ControlTheme::ButtonPrimaryLabel);
                    label->styles.add(&ControlTheme::ButtonSecondaryLabel);
                }
            }
            // Keep our sizing override LAST so the (just re-added) Button* padding
            // can't make the button taller than the inputs.
            r.setBtn->styles.remove(&FramesTreeStyle::SetButton);
            r.setBtn->styles.add(&FramesTreeStyle::SetButton);
            r.setBtn->dirty.style = true;
        }

        void computeStyle(Event& e) override {
            for (int fi = 0; fi < FrameCount; fi++) {
                Frame f = (Frame)fi;
                Row& r = rows[f];

                if (r.eye) {
                    bool on = true;
                    if (bool* v = visField(f)) { on = *v; }
                    r.eye->resource = on ? eyeOnResource : eyeOffResource;
                    const bool hover = r.eyeButton && r.eyeButton->targetFlags.hover;
                    r.eye->opacity = hover ? 0.95f : (on ? 0.5f : 0.26f);
                }

                setButtonColour(r, setWouldChange(f));
            }

            Element::computeStyle(e);
        }
    };
}
