module;

#include <string>
#include <functional>

#include <managed.hpp>

export module Rev.Element.Collapsible;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

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
            .padding = { .left = 6_px, .right = 2_px, .top = 6_px, .bottom = 4_px }
        };

        // Only the chevron toggles the body, so it (not the header) carries the
        // hand cursor. A little padding gives it a comfortable click target.
        Style Arrow = {
            .size = { 12_px, 12_px },
            .margin = { .right = 6_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
            .text = { .color = rgba(255, 255, 255, 1.0f) },
            .cursor = Cursor::Hand
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
            .padding = { .left = 18_px, .top = 2_px }
        };
    };

    // A collapsible disclosure element with two parts:
    //
    //   header     — a horizontal row whose first child is the chevron toggle.
    //                Host arbitrary children here (name, badges, buttons, ...).
    //   container  — the body, hidden/shown by the chevron. Add content here.
    //
    //   Collapsible* group = new Collapsible(parent, "Stage 1");
    //   new Text(group->header, "extra header thing");   // arbitrary header children
    //   new Text(group->container, "child a");           // body children
    //
    // Only the chevron toggles — clicking elsewhere in the header does nothing
    // (so the host can use header clicks for selection). Unlike a dropdown menu
    // (an absolutely-positioned overlay), the container fully participates in
    // layout when open.
    // Extends Box (not Element) so the control's root can paint its own
    // background/border — hosts (e.g. a CAD tree node) can give the whole
    // collapsible a card background, hover, and selection colour. The base
    // Self style sets no background, so a plain collapsible stays transparent.
    struct Collapsible : public Box {

        Box*  header    = nullptr;
        Svg*  arrow     = nullptr;
        Text* titleText = nullptr;
        Box*  container = nullptr;

        bool open = false;

        // Arrow points right (▸) when closed and down (▾) when open.
        static constexpr float ClosedRotation = 0.0f;
        static constexpr float OpenRotation   = -3.14159265f / 2.0f;

        std::function<void(Event&)> onToggle;

        Collapsible(
            Element* parent,
            std::string title = "",
            StyleList styles = {},
            bool startOpen = false
        ) : Box(parent, styles, "Collapsible") {

            this->styles.prepend(&CollapsibleStyle::Self);

            header = new Box(this, { &CollapsibleStyle::Header }, "CollapsibleHeader");

            arrow = new Svg(
                header,
                File("Rev/src/Elements/Controls/Dropdown/chevron-right.svg"),
                { &CollapsibleStyle::Arrow },
                "CollapsibleArrow"
            );

            // Optional convenience title; hosts may instead add their own header
            // children. Created only when a title is supplied.
            if (!title.empty()) {
                titleText = new Text(header, title, { &CollapsibleStyle::Title });
            }

            container = new Box(this, { &CollapsibleStyle::Container }, "CollapsibleContainer");

            // Only the chevron toggles — not the whole header.
            arrow->onClick([this](Event& e) {
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

        // Apply the open/closed flag to the container and arrow. Collapsing sets
        // the container's style visibility to Hidden, which excludes it from
        // layout, drawing, and hit-testing. `animate` rotates the arrow smoothly;
        // otherwise it snaps (initial state).
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
