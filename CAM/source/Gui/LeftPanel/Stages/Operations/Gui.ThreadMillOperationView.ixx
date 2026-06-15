module;

#include <string>
#include <vector>
#include <optional>

export module Cam.Gui.ThreadMillOperationView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.NumberInput;
import Rev.Element.Dropdown;

import Cam.App.Project;
import Cam.App.Stage;
import Cam.App.Operation;
import Cam.App.ThreadSpec;

import Cam.Gui.Theme;
import Cam.Gui.FaceOperationView;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ThreadOpStyle {

        Style CalloutColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .margin = { .left = 12_px }
        };

        Style PresetRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .padding = { .top = 1_px, .bottom = 1_px }
        };

        Style PresetField = {
            .size = { .width = 120_px }
        };
    }

    // The thread-mill operation's feature body: the standard face slot (the thread
    // hole), inherited from FaceOperationView, plus the THREAD CALLOUT editor --
    // an M-series preset over editable major / pitch / pre-bore.  The operation is
    // the single authority for the callout; changing it re-models the pre-bore and
    // derives the toolpath thread settings (Project::setThreadMillCallout).
    struct ThreadMillOperationView : public FaceOperationView {

        Dropdown*    presetDropdown = nullptr;
        NumberInput* majorInput     = nullptr;
        NumberInput* pitchInput     = nullptr;
        NumberInput* preBoreInput   = nullptr;

        ThreadMillOperationView(Element* parent) : FaceOperationView(parent) {}

        Cam::App::ThreadMillOperation* threadOp() {
            if (!state || !state->operation) { return nullptr; }
            if (state->operation->type() != Cam::App::OperationType::ThreadMill) { return nullptr; }
            return static_cast<Cam::App::ThreadMillOperation*>(state->operation);
        }

        std::vector<Dropdown::Item> presetOptions() const {
            std::vector<Dropdown::Item> items;
            for (const Cam::App::ThreadSpec& s : Cam::App::threadPresets()) {
                items.push_back({ s.label, s.label });
            }
            items.push_back({ Cam::App::threadCustomLabel, Cam::App::threadCustomValue });
            return items;
        }

        NumberInput* makeCalloutInput(Element* parent, const std::string& label, double value) {

            Box* row = new Box(parent, { &ThreadOpStyle::PresetRow }, "TMParamRow");

            new Text(
                row, label,
                Theme::layer({ &FaceOpStyle::ParamLabel }, { &Theme::Styles::MutedText })
            );

            NumberInput::Params params = NumberInput::Params::Default();
            params.label = label;
            params.placeholder = "0";
            params.maxDecimalPlaces = 3;

            NumberInput* in = new NumberInput(row, params, { &FaceOpStyle::OffsetInput });
            in->setValue(value);

            in->onValueChange = [this](Event& e, std::optional<double>) { commitCallout(e); };
            in->onKeyDown([in](Event& e) {
                if (e.keyboard.enter) { e.propagate = false; in->commit(e); }
            });

            return in;
        }

        // The user picked a preset: fill the callout fields, then commit.
        void applyPreset(Event& e) {
            const Cam::App::ThreadSpec* s =
                Cam::App::threadPresetByLabel(presetDropdown ? presetDropdown->params.value : "");
            if (s) {
                if (majorInput)   { majorInput->setValue(s->major); }
                if (pitchInput)   { pitchInput->setValue(s->pitch); }
                if (preBoreInput) { preBoreInput->setValue(s->preBore); }
            }
            commitCallout(e);
        }

        // Read the callout fields onto the operation and recompute (re-models the
        // pre-bore + derives the toolpath).  Reflect the result on the preset menu.
        void commitCallout(Event& e) {

            Cam::App::ThreadMillOperation* o = threadOp();
            if (!o) { return; }

            const double major   = majorInput   ? majorInput->valueOr(o->majorDiameter)     : o->majorDiameter;
            const double pitch   = pitchInput   ? pitchInput->valueOr(o->pitch)             : o->pitch;
            const double preBore = preBoreInput ? preBoreInput->valueOr(o->preBoreDiameter) : o->preBoreDiameter;

            if (Cam::App::Project* p = project()) {
                p->setThreadMillCallout(state, major, pitch, preBore, o->internal);
            }

            if (presetDropdown) {
                presetDropdown->params.value = Cam::App::threadPresetValueFor(major, pitch);
            }

            notifyChanged(e);
        }

        void rebuild() override {

            // The face slot(s); also clears `rows` and our (now-deleted) controls.
            FaceOperationView::rebuild();

            presetDropdown = nullptr;
            majorInput = pitchInput = preBoreInput = nullptr;

            Cam::App::ThreadMillOperation* o = threadOp();
            if (!o) { return; }

            Box* col = new Box(this, { &ThreadOpStyle::CalloutColumn }, "TMCalloutColumn");
            rows.push_back(col);

            Box* presetRow = new Box(col, { &ThreadOpStyle::PresetRow }, "TMPresetRow");
            new Text(
                presetRow, "Thread",
                Theme::layer({ &FaceOpStyle::ParamLabel }, { &Theme::Styles::MutedText })
            );
            presetDropdown = new Dropdown(
                presetRow,
                {
                    .label = "Thread",
                    .options = presetOptions(),
                    .placeholder = "Thread",
                    .value = Cam::App::threadPresetValueFor(o->majorDiameter, o->pitch)
                },
                { &ThreadOpStyle::PresetField }
            );
            presetDropdown->onChange = [this](Event& e) { applyPreset(e); };

            majorInput   = makeCalloutInput(col, "Major dia (mm)", o->majorDiameter);
            pitchInput   = makeCalloutInput(col, "Pitch (mm)",     o->pitch);
            preBoreInput = makeCalloutInput(col, "Pre-bore (mm)",  o->preBoreDiameter);
        }
    };
}
