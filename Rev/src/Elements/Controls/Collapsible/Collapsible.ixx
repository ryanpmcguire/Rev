module;

#include <string>
#include <functional>

#include <managed.hpp>

export module Rev.Element.Collapsible;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

export namespace Rev::Element {

    namespace CollapsibleStyle {

        // The whole control: a clickable header row followed by the (optionally
        // hidden) content container. Vertical, full width.
        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct }
        };

        Style Header = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .left = 2_px, .right = 2_px, .top = 4_px, .bottom = 4_px },
            .cursor = Cursor::Hand
        };

        Style Arrow = {
            .size = { 12_px, 12_px },
            .margin = { .right = 6_px },
            .text = { .color = rgba(100, 116, 139, 1.0) }
        };

        Style Title = {
            .overflow = Overflow::Hide,
            .text = { .size = 13_px, .wrap = Wrap::False }
        };

        // The collapsible content. A normal in-flow box — when hidden it is
        // removed from layout entirely (zero space), when visible it lays out
        // and pushes following siblings down. Children are added here.
        Style Container = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .left = 14_px }
        };
    };

    // A collapsible disclosure element. Clicking the header toggles the content
    // container open/closed. Add content by parenting elements to `container`.
    //
    //   Collapsible* group = new Collapsible(parent, "Stage 1");
    //   new Text(group->container, "child a");
    //   new Text(group->container, "child b");
    //
    // Unlike a dropdown menu (an absolutely-positioned overlay), the container
    // is a full participant in layout when open.
    struct Collapsible : public Element {

        Box*  header    = nullptr;
        Svg*  arrow     = nullptr;
        Text* titleText = nullptr;
        Box*  container = nullptr;

        bool open = false;

        // Arrow points right (▸) when closed and down (▾) when open.
        static constexpr float ClosedRotation = 0.0f;
        static constexpr float OpenRotation   = 3.14159265f / 2.0f;

        std::function<void(Event&)> onToggle;

        Collapsible(
            Element* parent,
            std::string title = "",
            StyleList styles = {},
            bool startOpen = false
        ) : Element(parent, styles, "Collapsible") {

            this->styles.prepend(&CollapsibleStyle::Self);

            header = new Box(this, { &CollapsibleStyle::Header }, "CollapsibleHeader");

            arrow = new Svg(
                header,
                File("Rev/src/Elements/Controls/Dropdown/chevron-right.svg"),
                { &CollapsibleStyle::Arrow },
                "CollapsibleArrow"
            );

            titleText = new Text(header, title, { &CollapsibleStyle::Title });

            container = new Box(this, { &CollapsibleStyle::Container }, "CollapsibleContainer");

            header->onClick([this](Event& e) {
                toggle(e);
                e.propagate = false;
            });

            open = startOpen;
            applyOpenState(nullptr, false);
        }

        // State
        //--------------------------------------------------

        void toggle(Event& e) {

            open = !open;
            applyOpenState(&e, true);

            if (onToggle) { onToggle(e); }
        }

        void setOpen(bool shouldOpen, Event* e = nullptr) {

            if (open == shouldOpen) { return; }

            open = shouldOpen;
            applyOpenState(e, e != nullptr);
        }

        void expand(Event* e = nullptr)   { setOpen(true, e); }
        void collapse(Event* e = nullptr) { setOpen(false, e); }

        // Apply the open/closed flag to the container visibility and arrow.
        // `animate` rotates the arrow smoothly; otherwise it snaps (initial state).
        void applyOpenState(Event* e, bool animate) {

            const Visibility target = open ? Visibility::Visible : Visibility::Hidden;

            if (container->style->visibility != target) {
                container->style->visibility = target;
                container->dirty.style = true;
            }

            const float targetRotation = open ? OpenRotation : ClosedRotation;

            if (arrow) {
                if (animate) {
                    arrow->transition(&arrow->rotation, targetRotation, 160);
                }
                else {
                    arrow->rotation = targetRotation;
                }
            }

            if (e) {
                refresh(*e);
            }
            else if (shared && shared->event) {
                refresh(*shared->event);
            }
        }
    };
}
