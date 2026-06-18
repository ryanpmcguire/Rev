module;

#include <string>
#include <vector>
#include <format>
#include <algorithm>
#include <optional>
#include <functional>

#include <managed.hpp>

export module Carvera.Gui.OriginSection;

import Rev.Core.Observable;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.TextInput;
import Rev.Element.NumberInput;

import CarveraAir;

import Cam.App;
import Cam.App.MachineSettings;
import Cam.App.MachineProfile;
import Cam.Gui.Theme;
import Carvera.Gui.Style;

export namespace Carvera::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace Theme = Cam::Gui::Theme;

    // -- Origin section ----------------------------------------------
    //
    //   [ Origins ]
    //   [ <alias name, editable> ]      <- rename the selected origin
    //   [ < ] [ > ] [ + ] [ Delete ]    <- navigate / add-after / delete
    //   [X] [Y] [Z] [A]                 <- editable machine coords
    //   [ Set Origin ] [ Goto ] [ Home ]
    //
    // The whole list (names + coords) is persisted with the app session
    // via Cam.App.MachineSettings on AppState.
    struct OriginSection : public Box {

        static Carvera::Air& air() { return Carvera::Air::instance(); }

        Cam::App::AppState* app = nullptr;
        Cam::App::MachineSettings localMachine_;   // fallback when no app

        TextInput*   originNameInput = nullptr;
        NumberInput* originX = nullptr;
        NumberInput* originY = nullptr;
        NumberInput* originZ = nullptr;
        NumberInput* originA = nullptr;

        OriginSection(Element* parent, Cam::App::AppState* appState = nullptr)
            : Box(parent, Theme::withPanel({ &Style::Section }), "OriginSection"),
              app(appState)
        {
            build();
        }

        // -- Backing store helpers --------------------------------------

        Cam::App::MachineSettings& machine() {
            return app ? app->machine : localMachine_;
        }

        Cam::App::OriginAlias* activeOriginPtr() {
            Cam::App::MachineSettings& m = machine();
            if (m.origins.empty()) { return nullptr; }
            m.activeOrigin = std::clamp(m.activeOrigin, 0, (int)m.origins.size() - 1);
            return &m.origins[m.activeOrigin];
        }

        void persistMachine() {
            if (app) { app->saveSession(); }
        }

        // -- Construction ----------------------------------------------

        static NumberInput::Params coordParams(const char* label) {
            NumberInput::Params p;
            p.label            = label;
            p.placeholder      = "0";
            p.maxLength        = 16;
            p.selectAllOnFocus = true;
            p.allowNegative    = true;
            p.allowDecimal     = true;
            p.allowEmpty       = false;
            p.maxDecimalPlaces = 3;
            return p;
        }

        void build() {

            new Text(this, "Origins", Theme::withText({ &Style::Label }));

            // -- Editable alias name (full width, no label) -------------
            originNameInput = new TextInput(this, {
                .label            = "",
                .placeholder      = "Origin name",
                .maxLength        = 48,
                .selectAllOnFocus = true
            });
            // Commit + persist when the field loses focus.
            originNameInput->text->onLoseFocus([this](Event& e) {
                commitOriginName();
                persistMachine();
                refresh(e);
            });

            // -- Navigate / add / delete --------------------------------
            Box* navRow = new Box(this, { &Style::Row }, "OriginNavRow");

            Box* prevBtn = makeBtn(navRow, "<", Style::Btn);
            prevBtn->style->size = { .width = 30_px, .height = 30_px };
            prevBtn->onClick([this](Event& e) { selectOrigin(-1, e); e.propagate = false; });

            Box* nextBtn = makeBtn(navRow, ">", Style::Btn);
            nextBtn->style->size = { .width = 30_px, .height = 30_px };
            nextBtn->onClick([this](Event& e) { selectOrigin(+1, e); e.propagate = false; });

            Box* addBtn = makeBtn(navRow, "+", Style::Btn);
            addBtn->style->size = { .width = 30_px, .height = 30_px };
            addBtn->onClick([this](Event& e) { addOrigin(e); e.propagate = false; });

            Box* delBtn = makeBtn(navRow, "Delete", Style::Btn);
            delBtn->style->size = { .width = 64_px, .height = 30_px };
            delBtn->onClick([this](Event& e) { deleteOrigin(e); e.propagate = false; });

            // -- Coordinate fields --------------------------------------
            Box* coordRow = new Box(this, { &Style::OriginCoordRow }, "OriginCoordRow");

            auto coordCell = [&](const char* label) -> NumberInput* {
                Box* cell = new Box(coordRow, { &Style::OriginCell }, "OriginCell");
                NumberInput* in = new NumberInput(cell, coordParams(label));
                in->onValueChange = [this](Event& e, std::optional<double>) { onOriginEdited(e); };
                return in;
            };

            originX = coordCell("X");
            originY = coordCell("Y");
            originZ = coordCell("Z");
            originA = coordCell("A");

            // -- Actions ------------------------------------------------
            Box* actionRow = new Box(this, { &Style::Row }, "OriginActionRow");

            Box* setBtn = makeBtn(actionRow, "Set Origin", Style::Btn);
            setBtn->style->size = { .width = 90_px, .height = 30_px };
            setBtn->onClick([this](Event& e) { onSetOrigin(e); e.propagate = false; });

            Box* setPtBtn = makeBtn(actionRow, "Set Point", Style::Btn);
            setPtBtn->style->size = { .width = 84_px, .height = 30_px };
            setPtBtn->onClick([this](Event& e) { onSetPoint(e); e.propagate = false; });

            makeBtn(actionRow, "Goto", Style::Btn)->onClick([this](Event& e) { gotoOrigin(e); e.propagate = false; });
            makeBtn(actionRow, "Home", Style::Btn)->onClick([this](Event& e) { air().home(); refresh(e); e.propagate = false; });

            loadOriginIntoFields();
        }

        // -- Edit / navigation handlers ---------------------------------

        // Write the name field's current text back into the active alias.
        // Called before any selection change so a pending rename is never lost.
        void commitOriginName() {
            Cam::App::OriginAlias* o = activeOriginPtr();
            if (o && originNameInput) { o->name = originNameInput->text->content.get(); }
        }

        // Cycle the active alias, refreshing the name + coordinate fields.
        void selectOrigin(int delta, Event& e) {
            commitOriginName();
            Cam::App::MachineSettings& m = machine();
            if (m.origins.empty()) { return; }
            const int n = (int)m.origins.size();
            m.activeOrigin = ((m.activeOrigin + delta) % n + n) % n;
            loadOriginIntoFields();
            refresh(e);
        }

        // Add a new origin directly after the current one, and select it.
        void addOrigin(Event& e) {
            commitOriginName();
            Cam::App::MachineSettings& m = machine();
            const int insertAt = m.origins.empty()
                ? 0
                : std::clamp(m.activeOrigin, 0, (int)m.origins.size() - 1) + 1;
            Cam::App::OriginAlias o;
            o.name = "Origin " + std::to_string(m.origins.size() + 1);
            m.origins.insert(m.origins.begin() + insertAt, o);
            m.activeOrigin = insertAt;
            loadOriginIntoFields();
            persistMachine();
            refresh(e);
        }

        // Delete the current origin (never the last one).
        void deleteOrigin(Event& e) {
            Cam::App::MachineSettings& m = machine();
            if (m.origins.size() <= 1) {
                air().log("Keep at least one origin - cannot delete the last.");
                refresh(e);
                return;
            }
            m.activeOrigin = std::clamp(m.activeOrigin, 0, (int)m.origins.size() - 1);
            m.origins.erase(m.origins.begin() + m.activeOrigin);
            if (m.activeOrigin >= (int)m.origins.size()) {
                m.activeOrigin = (int)m.origins.size() - 1;
            }
            loadOriginIntoFields();
            persistMachine();
            refresh(e);
        }

        // Push the active alias's name + coordinates into the fields.
        // setValue is called without an Event so it does not re-fire
        // onValueChange (which would otherwise loop back into onOriginEdited).
        void loadOriginIntoFields() {
            Cam::App::OriginAlias* o = activeOriginPtr();
            if (originNameInput) { originNameInput->text->content = o ? o->name : std::string(); }
            if (!o) { return; }
            if (originX) originX->setValue(o->x);
            if (originY) originY->setValue(o->y);
            if (originZ) originZ->setValue(o->z);
            if (originA) originA->setValue(o->a);
        }

        // A coordinate field was edited by hand -- store it back into the active
        // alias, mark it usable, and persist.
        void onOriginEdited(Event& e) {
            Cam::App::OriginAlias* o = activeOriginPtr();
            if (!o) { return; }
            if (originX) o->x = originX->valueOr(o->x);
            if (originY) o->y = originY->valueOr(o->y);
            if (originZ) o->z = originZ->valueOr(o->z);
            if (originA) o->a = originA->valueOr(o->a);
            o->valid = true;
            persistMachine();
            refresh(e);
        }

        // Ask Air to rapid to the active alias's stored machine position.
        void gotoOrigin(Event& e) {
            commitOriginName();
            Cam::App::OriginAlias* o = activeOriginPtr();
            if (!o) { return; }
            if (!o->valid) {
                air().log(std::format("'{}' has no stored position - set it first.", o->name));
                refresh(e);
                return;
            }
            air().goTo((float)o->x, (float)o->y, (float)o->z, (float)o->a);
            air().log(std::format("Goto '{}' (X{:.3f} Y{:.3f} Z{:.3f} A{:.3f}).",
                o->name, o->x, o->y, o->z, o->a));
            refresh(e);
        }

        // SET ORIGIN = establish the WORK FRAME, which IS the machine frame whose
        // origin is the rotary axis.  Machine calibration has fixed the axis Y/Z, so
        // those are KNOWN and Set Origin must NOT influence them -- it only contributes
        // the components still up to the operator: X (where along the axis the work
        // starts) and A (the angular index).  Y/Z are taken from the calibrated axis.
        // Only when the axis is uncalibrated (zero confidence) does every component
        // fall back to the live position -- the natural degenerate.
        void onSetOrigin(Event& e) {
            Carvera::Air& a = air();
            float x, y, z, aa;
            if (!a.currentTip(x, y, z, aa)) {   // the machine's TOOL-TIP position (WPos)
                a.log("Connect and wait for a position before setting origin.");
                refresh(e);
                return;
            }

            double oy = y, oz = z;   // faith fallback (uncalibrated => zero confidence)
            if (Cam::App::MachineProfile* m = app ? app->selectedMachine() : nullptr) {
                if (m->rotaryAxisCalibrated) {
                    oy = m->rotaryAxisY;   // KNOWN -- Set Origin cannot move it
                    oz = m->rotaryAxisZ;
                }
            }
            a.setWorkOrigin(x, (float)oy, (float)oz, aa);   // X,A on faith; Y,Z known
            refresh(e);
        }

        // SET POINT = store a point of interest (the active alias / bookmark) at the
        // current TOOL-TIP position (WPos).  A navigation bookmark (used by Goto); it
        // does NOT touch the work frame.  Stored in WPos so Goto returns the TIP to
        // this point regardless of which tool is loaded -- a longer tool won't crash.
        void onSetPoint(Event& e) {
            commitOriginName();
            Carvera::Air& a = air();
            float x, y, z, aa;
            if (Cam::App::OriginAlias* o = activeOriginPtr(); o && a.currentTip(x, y, z, aa)) {
                o->x = x; o->y = y; o->z = z; o->a = aa;
                o->valid = true;
                loadOriginIntoFields();
                persistMachine();
            }
            refresh(e);
        }

        // Is one of this section's text inputs currently focused?  Used by the
        // Interface's keyboard shortcut handler to avoid eating typing.
        bool isEditing() const {
            auto editing = [](TextInput* in) { return in && in->text->targetFlags.focus; };
            return editing(originNameInput) ||
                   editing(originX) || editing(originY) || editing(originZ) || editing(originA);
        }
    };
}
