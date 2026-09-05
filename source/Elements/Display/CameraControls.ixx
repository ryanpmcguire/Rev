module;

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module Rev.Element.CameraControls;

import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Button;
import Rev.Element.Camera;
import Rev.Element.Event;
import Rev.Element.NumberInput;
import Rev.Element.Slider;
import Rev.Element.Text;
import Rev.Window;

export namespace Rev::Element {

    namespace CameraControlStyles {

        Style Root = {
            .scroll = Scroll::Vertical,
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct, 100_pct },
            .padding = { .left = 20_px, .right = 20_px, .top = 18_px, .bottom = 18_px },
            .background = { .color = rgba(15, 23, 42, 1.0) }
        };

        Style Title = {
            .margin = { .bottom = 4_px },
            .text = { .color = rgba(248, 250, 252, 1.0), .size = 22_px }
        };

        Style Description = {
            .margin = { .bottom = 10_px },
            .text = { .color = rgba(148, 163, 184, 1.0), .size = 12_px }
        };

        Style Reset = {
            .margin = { .bottom = 16_px }
        };

        Style Row = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 14_px }
        };

        Style Label = {
            .margin = { .bottom = 6_px },
            .text = { .color = rgba(226, 232, 240, 1.0), .size = 13_px }
        };

        Style Editors = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct }
        };

        Style Number = {
            .size = { .width = 112_px },
            .margin = { .right = 14_px }
        };

        Style Slider = {
            .size = { .width = Grow(), .min = { .width = 180_px } },
            .margin = { .bottom = 0_px }
        };

        Style Empty = {
            .text = { .color = rgba(148, 163, 184, 1.0), .size = 13_px }
        };
    }

    struct CameraControlRow : public Box {

        Camera* camera = nullptr;
        Media::CameraControlInfo info;
        NumberInput* number = nullptr;
        Slider* slider = nullptr;

        CameraControlRow(
            Element* parent,
            Camera* camera,
            const Media::CameraControlInfo& info
        ) : Box(parent, { &CameraControlStyles::Row }, "CameraControlRow"),
            camera(camera), info(info) {

            new Text(this, info.name, { &CameraControlStyles::Label });
            Box* editors = new Box(this, { &CameraControlStyles::Editors }, "CameraControlEditors");

            NumberInput::Params numberParams = NumberInput::Params::Default();
            numberParams.label = "";
            numberParams.placeholder = "";
            numberParams.allowEmpty = false;
            numberParams.allowNegative = info.minimum < 0;
            numberParams.allowDecimal = false;
            numberParams.maxDecimalPlaces = 0;
            numberParams.min = static_cast<double>(info.minimum);
            numberParams.max = static_cast<double>(info.maximum);

            number = new NumberInput(editors, numberParams, { &CameraControlStyles::Number });
            number->setValue(static_cast<double>(info.value));

            Slider::SliderData sliderData;
            sliderData.min = static_cast<float>(info.minimum);
            sliderData.max = static_cast<float>(info.maximum);
            sliderData.def = static_cast<float>(info.defaultValue);
            sliderData.val = static_cast<float>(info.value);
            slider = new Slider(editors, sliderData, { &CameraControlStyles::Slider });

            // The row provides its own label and paired numeric display.
            slider->textContainer->style->visibility = Visibility::Hidden;
            slider->textContainer->dirty.style = true;

            number->onValueChange = [this](Event&, std::optional<double> value) {
                if (!value || !this->camera) { return; }
                this->camera->setControl(
                    this->info.control,
                    *value,
                    Media::CameraControlMode::Manual
                );
            };

            slider->onValueChange = [this](Event&, float value) {
                if (!this->camera) { return; }
                this->camera->setControl(
                    this->info.control,
                    value,
                    Media::CameraControlMode::Manual
                );
            };

            camera->onControlChanged(this, [this](Media::CameraControlEvent& event) {
                if (event.info.control != this->info.control) { return; }
                this->info = event.info;
                reflect();
            });
        }

        ~CameraControlRow() {
            if (camera) { camera->unsubscribeControls(this); }
        }

        void detachCamera() {
            if (camera) { camera->unsubscribeControls(this); }
            camera = nullptr;
        }

        void reflect() {
            number->setValue(static_cast<double>(info.value));
            slider->setVal(static_cast<float>(info.value));
            if (shared && shared->event) { refresh(*shared->event); }
        }
    };

    struct CameraControlWindow : public Window {

        Camera* camera = nullptr;
        std::function<void()> onClosed;
        std::vector<CameraControlRow*> rows;

        CameraControlWindow(
            Window* owner,
            Camera* camera,
            std::function<void()> onClosed = {}
        ) : Window(owner, details()), camera(camera), onClosed(std::move(onClosed)) {

            Box* root = new Box(this, { &CameraControlStyles::Root }, "CameraControlsRoot");
            new Text(root, "Camera controls", { &CameraControlStyles::Title });
            new Text(
                root,
                "Values are cached when the camera opens and updated only after a control write or explicit refresh.",
                { &CameraControlStyles::Description }
            );

            Button* reset = new Button(
                root,
                Button::Params::Secondary("Reset to defaults"),
                { &CameraControlStyles::Reset }
            );
            reset->setDisabled(!camera || camera->controls().empty());
            reset->onClick([this](Event& event) {
                if (!this->camera) { return; }
                this->camera->resetControls();
                refresh(event);
            });

            if (!camera || camera->controls().empty()) {
                new Text(root, "This camera does not expose standard controls.", { &CameraControlStyles::Empty });
                return;
            }

            for (const Media::CameraControlInfo& info : camera->controls()) {
                rows.push_back(new CameraControlRow(root, camera, info));
            }
        }

        ~CameraControlWindow() {
            if (onClosed) { onClosed(); }
        }

        void detachCamera() {
            for (CameraControlRow* row : rows) {
                if (row) { row->detachCamera(); }
            }
            camera = nullptr;
        }

        static Window::Details details() {
            return {
                .name = "Camera controls",
                .size = {
                    .width = 560,
                    .height = 640,
                    .min = { 440, 360 },
                    .max = { 900, 1100 }
                },
                .resizable = true
            };
        }
    };
}
