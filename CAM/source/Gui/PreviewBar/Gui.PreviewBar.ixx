module;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <managed.hpp>

export module Cam.Gui.PreviewBar;

import Rev.Core.Resource;
import Rev.Core.Animator;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Svg;
import Rev.Element.Slider;
import Rev.Element.Text;
import Rev.Element.NumberInput;
import Rev.Element.Dropdown;
import Rev.Element.ControlTheme;

import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    enum class PreviewMode {
        AbsoluteToolPath,    // standard view: part static, tool traces path in world space
        MachineSimulation    // IK view: part physically moves/rotates as the machine would
    };

    namespace PreviewBarStyle {

        Style Panel = {
            .layout = {
                Axis::Vertical,
                Align::Start,
                Align::Start,
                Wrap::False,
                Position::Absolute
            },
            .position = { .left = 0_px, .bottom = 0_px },
            .size = { .width = 100_pct },
            .zIndex = 1
        };

        Style Transport = {
            .layout = {
                Axis::Horizontal,
                Align::Center,
                Align::Center,
                Wrap::False
            },
            .size = { .width = 100_pct, .height = 32_px },
            .padding = { 12_px, 12_px, 4_px, 4_px },
            .margin = { .bottom = 2_px }
        };

        Style TimeGroup = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .height = 32_px }
        };

        Style TransportButtons = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .height = 32_px },
            .margin = { 10_px, 0_px, 0_px, 0_px }
        };

        Style SpeedGroup = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .height = 32_px },
            .margin = { 10_px, 0_px, 0_px, 0_px }
        };

        Style TransportButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 30_px, .height = 28_px },
            .margin = { .left = 3_px, .right = 3_px },
            .padding = { .left = 4_px, .right = 4_px, .top = 4_px, .bottom = 4_px },
            .border = { .radius = 6_px },
            .cursor = Cursor::Hand
        };

        Style TransportIcon = {
            .size = { 15_px, 15_px }
        };

        Style SliderTextHidden = {
            .visibility = { Visibility::Hidden }
        };

        Style SliderCompact = {
            .margin = { .bottom = 0_px },
            .padding = { .left = 12_px, .right = 12_px, .top = 0_px, .bottom = 8_px }
        };

        Style TimeText = {
            .text = { .size = 12_px }
        };

        Style TimeSeparator = {
            .margin = { 2_px, 2_px, 0_px, 2_px },
            .text = { .size = 12_px }
        };

        Style SpeedControl = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 52_px, .height = 32_px },
            .margin = { 0_px, 0_px, 0_px, 0_px }
        };

        Style SpeedContainerIdle = {
            .padding = { 2_px, 4_px, 2_px, 4_px },
            .background = { .color = rgba(0, 0, 0, 0.0) },
            .border = { .radius = 4_px, .width = 0_px },
            .shadow = {
                .color = rgba(0, 0, 0, 0.0),
                .size = 0_px,
                .blur = 0_px,
                .y = 0_px
            }
        };

        Style LabelHidden = {
            .visibility = { Visibility::Hidden },
            .size = { 0_px, 0_px },
            .margin = { 0_px, 0_px, 0_px, 0_px }
        };

        // Behind transport + slider (out of layout flow). Blocks the 3D view for
        // empty areas while controls drawn later still receive hits.
        Style ViewModeGroup = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size   = { .height = 32_px },
            .margin = { 10_px, 0_px, 0_px, 0_px }
        };

        Style ViewModeDropdown = {
            .size = { .width = 140_px }
        };

        Style HitBackdrop = {
            .layout = {
                Axis::Vertical,
                Align::Start,
                Align::Start,
                Wrap::False,
                Position::Absolute
            },
            .position = { .left = 0_px, .bottom = 0_px },
            .size = { 100_pct, 100_pct }
        };
    }

    struct PreviewBar : public Box {

        Slider* slider = nullptr;
        Svg* playPauseIcon = nullptr;
        Text* elapsedTimeText = nullptr;
        Text* totalTimeText = nullptr;
        NumberInput* speedInput = nullptr;
        Dropdown* viewModeDropdown = nullptr;

        float percent = 100.0f;
        double playbackSpeed = 1.0;
        PreviewMode previewMode = PreviewMode::AbsoluteToolPath;
        Rev::Core::Animator animator;

        static constexpr double FrameRate = 150.0;
        static constexpr double MinPlaybackSpeed = 0.1;
        static constexpr double MaxPlaybackSpeed = 32.0;

        std::function<void(Event&)> onPercentChanged;
        std::function<void(Event&)> onRefresh;
        std::function<void(Event&)> onStepForward;
        std::function<void(Event&)> onStepBack;
        std::function<void(Event&)> onPlayRequested;
        std::function<void(Event&)> onPlaybackSpeedChanged;
        std::function<void(PreviewMode, Event&)> onViewModeChanged;
        std::function<void(Rev::Core::AnimationEvent&, Event&)> onAnimateFrame;

        static void wireHitInterceptor(Element* element) {
            element->interceptHits = true;
        }

        static std::string formatMinutesSeconds(double seconds) {

            if (!std::isfinite(seconds) || seconds < 0.0) {
                seconds = 0.0;
            }

            const int total = (int)std::floor(seconds + 1e-9);
            const int minutes = total / 60;
            const int secs = total % 60;

            char buffer[16] = {};
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, secs);

            return std::string(buffer);
        }

        PreviewBar(Element* parent, StyleList styles = {}) : Box(parent, styles, "PreviewBar") {

            this->styles.add(&PreviewBarStyle::Panel);

            Box* hitBackdrop = new Box(
                this,
                { &PreviewBarStyle::HitBackdrop },
                "PreviewBarHitBackdrop"
            );

            wireHitInterceptor(hitBackdrop);

            Element* transport = new Element(
                this,
                { &PreviewBarStyle::Transport },
                "PreviewBarTransport"
            );

            Element* timeGroup = new Element(
                transport,
                { &PreviewBarStyle::TimeGroup },
                "PreviewBarTimeGroup"
            );

            wireHitInterceptor(timeGroup);

            elapsedTimeText = new Text(
                timeGroup,
                "00:00",
                Theme::layer({}, { &PreviewBarStyle::TimeText, &Theme::Styles::Text })
            );

            new Text(
                timeGroup,
                " / ",
                Theme::layer(
                    { &PreviewBarStyle::TimeSeparator },
                    { &PreviewBarStyle::TimeText, &Theme::Styles::MutedText }
                )
            );

            totalTimeText = new Text(
                timeGroup,
                "00:00",
                Theme::layer({}, { &PreviewBarStyle::TimeText, &Theme::Styles::MutedText })
            );

            Element* transportButtons = new Element(
                transport,
                { &PreviewBarStyle::TransportButtons },
                "PreviewBarTransportButtons"
            );

            wireHitInterceptor(transportButtons);

            auto wireTransportButton = [&](
                Box* button,
                std::function<void(Event&)> onClick
            ) {
                wireHitInterceptor(button);

                button->onMouseDown([](Event& e) {
                    e.propagate = false;
                });

                button->onClick([onClick](Event& e) {
                    onClick(e);
                    e.propagate = false;
                });
            };

            Box* stepBackButton = new Box(
                transportButtons,
                Theme::withSolidButton({
                    &PreviewBarStyle::TransportButton,
                    &Theme::Styles::SolidButtonHover,
                    &Theme::Styles::SolidButtonPress
                }),
                "PreviewBarStepBack"
            );

            new Svg(
                stepBackButton,
                File("./StepBack.svg"),
                Theme::layer({
                    &PreviewBarStyle::TransportIcon,
                    &Theme::Styles::IconHover
                }, {
                    &Theme::Styles::Icon
                }),
                "PreviewBarStepBackIcon"
            );

            wireTransportButton(stepBackButton, [this](Event& e) { stepBack(e); });

            Box* playPauseButton = new Box(
                transportButtons,
                Theme::withSolidButton({
                    &PreviewBarStyle::TransportButton,
                    &Theme::Styles::SolidButtonHover,
                    &Theme::Styles::SolidButtonPress
                }),
                "PreviewBarPlayPause"
            );

            playPauseIcon = new Svg(
                playPauseButton,
                File("./Play.svg"),
                Theme::layer({
                    &PreviewBarStyle::TransportIcon,
                    &Theme::Styles::IconHover
                }, {
                    &Theme::Styles::Icon
                }),
                "PreviewBarPlayPauseIcon"
            );

            wireTransportButton(playPauseButton, [this](Event& e) {
                if (animator.isPlaying()) { pause(e); }
                else { play(e); }
            });

            Box* stepForwardButton = new Box(
                transportButtons,
                Theme::withSolidButton({
                    &PreviewBarStyle::TransportButton,
                    &Theme::Styles::SolidButtonHover,
                    &Theme::Styles::SolidButtonPress
                }),
                "PreviewBarStepForward"
            );

            new Svg(
                stepForwardButton,
                File("./StepForward.svg"),
                Theme::layer({
                    &PreviewBarStyle::TransportIcon,
                    &Theme::Styles::IconHover
                }, {
                    &Theme::Styles::Icon
                }),
                "PreviewBarStepForwardIcon"
            );

            wireTransportButton(stepForwardButton, [this](Event& e) { stepForward(e); });

            Element* speedGroup = new Element(
                transport,
                { &PreviewBarStyle::SpeedGroup },
                "PreviewBarSpeedGroup"
            );

            wireHitInterceptor(speedGroup);

            NumberInput::Params speedParams;
            speedParams.label = "";
            speedParams.placeholder = "1";
            speedParams.maxLength = 8;
            speedParams.selectAllOnFocus = true;
            speedParams.allowNegative = false;
            speedParams.allowDecimal = true;
            speedParams.allowEmpty = false;
            speedParams.maxDecimalPlaces = 2;
            speedParams.min = MinPlaybackSpeed;
            speedParams.max = MaxPlaybackSpeed;

            speedInput = new NumberInput(
                speedGroup,
                speedParams,
                { &PreviewBarStyle::SpeedControl }
            );

            if (speedInput->label) {
                speedInput->label->styles.add(&PreviewBarStyle::LabelHidden);
            }

            speedInput->styles.remove(&ControlTheme::Control);

            speedInput->container->styles.remove(&ControlTheme::Field);
            speedInput->container->styles.remove(&ControlTheme::FieldFocus);
            speedInput->container->styles.add(&PreviewBarStyle::SpeedContainerIdle);
            speedInput->container->styles.add(&Theme::Styles::SolidButtonHover);

            speedInput->text->styles.add(&PreviewBarStyle::TimeText);
            speedInput->text->styles.add(&Theme::Styles::Text);

            if (speedInput->placeholderText) {
                speedInput->placeholderText->styles.add(&PreviewBarStyle::TimeText);
                speedInput->placeholderText->styles.add(&Theme::Styles::MutedText);
            }

            wireHitInterceptor(speedInput);
            wireHitInterceptor(speedInput->container);

            speedInput->setValue(playbackSpeed);
            speedInput->onValueChange = [this](Event& e, std::optional<double> value) {
                applyPlaybackSpeedFromInput(e, value);
            };

            speedInput->onTextInput([this](Event& e) {
                updatePlaybackSpeedFromInput(e);
            });

            // ── View mode dropdown ─────────────────────────────────────────
            Element* viewModeGroup = new Element(
                transport,
                { &PreviewBarStyle::ViewModeGroup },
                "PreviewBarViewModeGroup"
            );

            wireHitInterceptor(viewModeGroup);

            Dropdown::Params viewModeParams;
            viewModeParams.label = "";
            viewModeParams.options = {
                { "Toolpath",    "toolpath" },
                { "Machine Sim", "machine"  }
            };
            viewModeParams.value = "toolpath";

            viewModeDropdown = new Dropdown(
                viewModeGroup,
                viewModeParams,
                { &PreviewBarStyle::ViewModeDropdown }
            );

            wireHitInterceptor(viewModeDropdown);

            viewModeDropdown->onChange = [this](Event& e) {
                if (!viewModeDropdown) { return; }

                PreviewMode next = viewModeDropdown->params.value == "machine"
                    ? PreviewMode::MachineSimulation
                    : PreviewMode::AbsoluteToolPath;

                if (next == previewMode) { return; }

                previewMode = next;

                if (onViewModeChanged) {
                    onViewModeChanged(previewMode, e);
                }
            };

            Slider::SliderData sliderData;
            sliderData.min = 0.0f;
            sliderData.max = 100.0f;
            sliderData.def = 1.0f;
            sliderData.val = 100.0f;

            slider = new Slider(
                this,
                sliderData,
                {},
                "PreviewBarSlider"
            );

            wireHitInterceptor(slider);
            wireHitInterceptor(slider->sliderContainer);

            slider->style->size = { .width = 100_pct };
            slider->styles.add(&PreviewBarStyle::SliderCompact);

            if (slider->textContainer) {
                slider->textContainer->styles.add(&PreviewBarStyle::SliderTextHidden);
            }

            auto onSliderChanged = [this](Event& e) {
                pause(e);
                applyPercentFromSlider(e);
                e.propagate = false;
            };

            slider->sliderContainer->onMouseDown(onSliderChanged);
            slider->sliderContainer->onDrag(onSliderChanged);

            animator.onFrame([this](Rev::Core::AnimationEvent& frame) {

                if (!shared || !shared->event) { return; }

                if (onAnimateFrame) {
                    onAnimateFrame(frame, *shared->event);
                }
            });

            animator.setFrequency(FrameRate);
        }

        bool isPlaying() const {
            return animator.isPlaying();
        }

        double playbackSpeedMultiplier() const {
            return playbackSpeed;
        }

        void syncTimeDisplay(double elapsedSeconds, double totalSeconds, Event& e) {

            if (elapsedTimeText) {
                elapsedTimeText->setContent(formatMinutesSeconds(elapsedSeconds));
            }

            if (totalTimeText) {
                totalTimeText->setContent(formatMinutesSeconds(totalSeconds));
            }

            refresh(e);
        }

        void applyPlaybackSpeedFromInput(
            Event& e,
            std::optional<double> value
        ) {

            if (!value || *value <= 0.0) { return; }

            const double clamped = std::clamp(
                *value,
                MinPlaybackSpeed,
                MaxPlaybackSpeed
            );

            if (std::fabs(clamped - playbackSpeed) < 1e-9) { return; }

            playbackSpeed = clamped;

            if (onPlaybackSpeedChanged) {
                onPlaybackSpeedChanged(e);
            }
        }

        void updatePlaybackSpeedFromInput(Event& e) {

            if (!speedInput) { return; }

            double value = 0.0;

            if (!speedInput->tryGetValue(value) || value <= 0.0) { return; }

            const double clamped = std::clamp(
                value,
                MinPlaybackSpeed,
                MaxPlaybackSpeed
            );

            if (std::fabs(clamped - playbackSpeed) < 1e-9) { return; }

            playbackSpeed = clamped;

            if (onPlaybackSpeedChanged) {
                onPlaybackSpeedChanged(e);
            }
        }

        void syncSliderDisplay(Event& e) {
            if (slider) {
                slider->setVal(percent);
                slider->refresh(e);
            }
        }
        void setPercent(float value, Event& e, bool requestRepaint = false) {
            percent = std::clamp(value, 0.0f, 100.0f);
            syncSliderDisplay(e);
            if (onPercentChanged) {
                onPercentChanged(e);
            }

            if (requestRepaint && onRefresh) {
                onRefresh(e);
            }
        }

        void applyPercentFromSlider(Event& e) {

            if (slider) {
                percent = slider->data.val;
            }

            if (onPercentChanged) {
                onPercentChanged(e);
            }
        }

        void syncPlayPauseIcon(Event& e) {

            if (!playPauseIcon) { return; }

            playPauseIcon->resource = animator.isPlaying()
                ? File("./Pause.svg")
                : File("./Play.svg");

            refresh(e);
        }

        void pause(Event& e) {

            if (!animator.isPlaying()) { return; }

            animator.pause();
            syncPlayPauseIcon(e);

            if (onRefresh) {
                onRefresh(e);
            }
        }

        void stop(Event& e) {

            if (!animator.isPlaying()) { return; }

            animator.stop();
            syncPlayPauseIcon(e);

            if (onRefresh) {
                onRefresh(e);
            }
        }

        void play(Event& e) {

            if (onPlayRequested) {
                onPlayRequested(e);
            }

            animator.play();
            syncPlayPauseIcon(e);

            if (onRefresh) {
                onRefresh(e);
            }
        }

        // Resume animation without resetting the preview clock (for chapter skips).
        void resumePlaying(Event& e) {

            if (animator.isPlaying()) { return; }

            animator.play();
            syncPlayPauseIcon(e);

            if (onRefresh) {
                onRefresh(e);
            }
        }

        void stepBack(Event& e) {
            if (onStepBack) {
                onStepBack(e);
            }
        }
        void stepForward(Event& e) {
            if (onStepForward) {
                onStepForward(e);
            }
        }
    };
}
