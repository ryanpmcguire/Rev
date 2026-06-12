module;

#include <algorithm>
#include <functional>
#include <string>

export module Sketch.Gui.PreviewBar;

import Rev.Core.Animator;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Slider;

import Sketch.Gui.Theme;

export namespace Sketch::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace PreviewBarStyle {

        Style Panel = {
            .layout = {
                .direction = Axis::Vertical,
                .horizontal = Align::Start,
                .vertical = Align::Start,
                .wrap = Wrap::False,
                .position = Position::Absolute
            },
            .position = { .left = 0_px, .bottom = 0_px },
            .size = { .width = 100_pct },
            .zIndex = 1
        };

        Style Transport = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct, .height = 30_px },
            .padding = { 12_px, 12_px, 2_px, 2_px }
        };

        Style PlayButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .height = 24_px },
            .padding = { 10_px, 10_px, 2_px, 2_px },
            .background = { .color = rgba(0, 0, 0, 0.0), .transition = 100_ms },
            .border = { .color = rgba(0, 0, 0, 0.0), .width = 1_px, .radius = 6_px },
            .cursor = Cursor::Hand
        };

        Style PlayLabel = {
            .text = { .size = 12_px }
        };

        Style SliderCompact = {
            .margin = { .bottom = 0_px },
            .padding = { .left = 12_px, .right = 12_px, .top = 0_px, .bottom = 8_px }
        };

        Style SliderTextHidden = {
            .visibility = { Visibility::Hidden }
        };
    }

    // The toolpath playback bar -- a trimmed cousin of CAM's PreviewBar: a
    // play/pause control and a scrub slider over the flattened toolpath. The
    // bar owns playback state (percent, animator); what the percent MEANS is
    // the view's business.
    struct PreviewBar : public Box {

        Slider* slider = nullptr;
        Box* playButton = nullptr;
        Text* playLabel = nullptr;
        Box* directionButton = nullptr;
        Text* directionLabel = nullptr;

        float percent = 0.0f;
        bool reverse = false;          // execute the whole path backwards
        Rev::Core::Animator animator;

        std::function<void(Event&)> onPercentChanged;
        std::function<void(Event&)> onDirectionChanged;
        std::function<void(Rev::Core::AnimationEvent&, Event&)> onAnimateFrame;

        PreviewBar(Element* parent, StyleList styles = {}) : Box(parent, styles, "PreviewBar") {

            this->styles.add(&PreviewBarStyle::Panel);
            this->styles.add(&Theme::Styles::TabBar);
            interceptHits = true;

            Box* transport = new Box(this, { &PreviewBarStyle::Transport }, "PreviewTransport");
            transport->interceptHits = true;

            playButton = new Box(
                transport,
                Theme::layer({ &PreviewBarStyle::PlayButton, &Theme::Styles::ChromeHover }, {}),
                "PreviewPlay"
            );
            playButton->interceptHits = true;

            playLabel = new Text(
                playButton,
                "Play",
                Theme::layer({
                    &PreviewBarStyle::PlayLabel,
                    &Theme::Styles::ChromeIconHover
                }, {
                    &Theme::Styles::ChromeIcon
                })
            );

            playButton->onClick([this](Event& e) {
                if (animator.isPlaying()) { pause(e); } else { play(e); }
                e.propagate = false;
            });

            // Direction: forward or the entire path executed backwards.
            directionButton = new Box(
                transport,
                Theme::layer({ &PreviewBarStyle::PlayButton, &Theme::Styles::ChromeHover }, {}),
                "PreviewDirection"
            );
            directionButton->interceptHits = true;

            directionLabel = new Text(
                directionButton,
                "Fwd",
                Theme::layer({
                    &PreviewBarStyle::PlayLabel,
                    &Theme::Styles::ChromeIconHover
                }, {
                    &Theme::Styles::ChromeIcon
                })
            );

            directionButton->onClick([this](Event& e) {
                reverse = !reverse;
                if (directionLabel) { directionLabel->setContent(std::string(reverse ? "Rev" : "Fwd")); }
                if (onDirectionChanged) { onDirectionChanged(e); }
                refresh(e);
                e.propagate = false;
            });

            Slider::SliderData sd;
            sd.min = 0.0f; sd.max = 100.0f; sd.def = 0.0f; sd.val = 0.0f;

            slider = new Slider(this, sd, {}, "PreviewSlider");
            slider->interceptHits = true;
            slider->sliderContainer->interceptHits = true;
            slider->style->size = { .width = 100_pct };
            slider->styles.add(&PreviewBarStyle::SliderCompact);
            if (slider->textContainer) { slider->textContainer->styles.add(&PreviewBarStyle::SliderTextHidden); }

            auto scrub = [this](Event& e) {
                pause(e);
                percent = slider->data.val;
                if (onPercentChanged) { onPercentChanged(e); }
                e.propagate = false;
            };
            slider->sliderContainer->onMouseDown(scrub);
            slider->sliderContainer->onDrag(scrub);

            animator.onFrame([this](Rev::Core::AnimationEvent& frame) {
                if (!shared || !shared->event) { return; }
                if (onAnimateFrame) { onAnimateFrame(frame, *shared->event); }
            });
            animator.setFrequency(60.0);
        }

        void setPercent(float v, Event& e) {
            percent = std::clamp(v, 0.0f, 100.0f);
            if (slider) { slider->setVal(percent); slider->refresh(e); }
            if (onPercentChanged) { onPercentChanged(e); }
        }

        void play(Event& e) {
            if (percent >= 100.0f) { setPercent(0.0f, e); }   // replay from the top
            animator.play();
            if (playLabel) { playLabel->setContent(std::string("Pause")); }
            refresh(e);
        }

        void pause(Event& e) {
            if (!animator.isPlaying()) { return; }
            animator.pause();
            if (playLabel) { playLabel->setContent(std::string("Play")); }
            refresh(e);
        }
    };
}
