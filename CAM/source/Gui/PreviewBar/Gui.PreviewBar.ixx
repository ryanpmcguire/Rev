module;

#include <algorithm>
#include <functional>

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

import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

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
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 2_px }
        };

        Style TransportIconHidden = {
            .visibility = { Visibility::Hidden }
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
    }

    struct PreviewBar : public Box {

        Slider* slider = nullptr;
        Svg* playPausePlayIcon = nullptr;
        Svg* playPausePauseIcon = nullptr;

        float percent = 100.0f;
        Rev::Core::Animator animator;

        static constexpr float PlaySpeed = 12.0f;
        static constexpr float StepPercent = 1.0f;
        static constexpr double FrameRate = 150.0;

        std::function<void(Event&)> onPercentChanged;
        std::function<void(Event&)> onRefresh;

        PreviewBar(Element* parent, StyleList styles = {}) : Box(parent, styles, "PreviewBar") {

            this->styles.add(&PreviewBarStyle::Panel);

            Element* transport = new Element(
                this,
                { &PreviewBarStyle::Transport },
                "PreviewBarTransport"
            );

            auto wireTransportButton = [&](
                Box* button,
                std::function<void(Event&)> onClick
            ) {
                button->onClick([onClick](Event& e) {
                    onClick(e);
                    e.propagate = false;
                });
            };

            Box* stepBackButton = new Box(
                transport,
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
                transport,
                Theme::withSolidButton({
                    &PreviewBarStyle::TransportButton,
                    &Theme::Styles::SolidButtonHover,
                    &Theme::Styles::SolidButtonPress
                }),
                "PreviewBarPlayPause"
            );

            playPausePlayIcon = new Svg(
                playPauseButton,
                File("./Play.svg"),
                Theme::layer({
                    &PreviewBarStyle::TransportIcon,
                    &Theme::Styles::IconHover
                }, {
                    &Theme::Styles::Icon
                }),
                "PreviewBarPlayIcon"
            );

            playPausePauseIcon = new Svg(
                playPauseButton,
                File("./Pause.svg"),
                Theme::layer({
                    &PreviewBarStyle::TransportIcon,
                    &Theme::Styles::IconHover
                }, {
                    &Theme::Styles::Icon
                }),
                "PreviewBarPauseIcon"
            );

            playPausePauseIcon->styles.add(&PreviewBarStyle::TransportIconHidden);

            wireTransportButton(playPauseButton, [this](Event& e) {
                if (animator.isPlaying()) { pause(e); }
                else { play(e); }
            });

            Box* stepForwardButton = new Box(
                transport,
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

            slider->style->size = { .width = 100_pct };
            slider->styles.add(&PreviewBarStyle::SliderCompact);

            if (slider->textContainer) {
                slider->textContainer->styles.add(&PreviewBarStyle::SliderTextHidden);
            }

            auto onSliderChanged = [this](Event& e) {
                pause(e);
                applyPercentFromSlider(e);
            };

            slider->sliderContainer->onMouseDown(onSliderChanged);
            slider->sliderContainer->onDrag(onSliderChanged);

            animator.onFrame([this](Rev::Core::AnimationEvent& frame) {

                if (!shared || !shared->event) { return; }

                Event& e = *shared->event;

                const float deltaPercent =
                    (float(frame.deltaMs) / 1000.0f) * PlaySpeed;

                setPercent(percent + deltaPercent, e, true);

                if (percent >= 100.0f) {
                    animator.stop();
                    syncPlayPauseIcon(e);
                }
            });

            animator.setFrequency(FrameRate);
        }

        double progress() const {
            return double(percent) / 100.0;
        }

        void setPercent(float value, Event& e, bool requestRepaint = false) {

            percent = std::clamp(value, 0.0f, 100.0f);

            if (slider) {
                slider->setVal(percent);
                slider->refresh(e);
            }

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

            if (!playPausePlayIcon || !playPausePauseIcon) { return; }

            if (animator.isPlaying()) {
                playPausePlayIcon->styles.add(&PreviewBarStyle::TransportIconHidden);
                playPausePauseIcon->styles.remove(&PreviewBarStyle::TransportIconHidden);
            }
            else {
                playPausePauseIcon->styles.add(&PreviewBarStyle::TransportIconHidden);
                playPausePlayIcon->styles.remove(&PreviewBarStyle::TransportIconHidden);
            }

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

        void play(Event& e) {

            if (percent >= 100.0f) {
                setPercent(0.0f, e);
            }

            animator.play();
            syncPlayPauseIcon(e);

            if (onRefresh) {
                onRefresh(e);
            }
        }

        void stepBack(Event& e) {

            pause(e);
            setPercent(percent - StepPercent, e);
        }

        void stepForward(Event& e) {

            pause(e);
            setPercent(percent + StepPercent, e);
        }
    };
}
