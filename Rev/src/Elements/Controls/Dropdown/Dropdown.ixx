module;

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

#include <managed.hpp>

export module Rev.Element.Dropdown;

import Rev.Core.Pos;
import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Element.ControlTheme;

export namespace Rev::Element {

    using namespace ControlTheme;

    struct Dropdown : public Element {

        Text* label = nullptr;

        Box* dropdown = nullptr;
            Box* fieldRow = nullptr;
            Text* dropdownText = nullptr;
            Svg* dropdownArrow = nullptr;

        Box* optionsContainer = nullptr;
        std::vector<Text*> options;
        bool open = false;
        int menuHighlight = -1;

        struct Item {
            std::string name;
            std::string value;
            bool disabled = false;
        };

        struct Params {

            std::string label = "Dropdown";
            std::vector<Item> options;
            std::string placeholder;
            std::string value;

            static Params Default() {
                return {
                    .label = "Dropdown",
                    .options = {
                        { "Select...", "" },
                        { "Option 1", "1" },
                        { "Option 2", "2" },
                        { "Disabled", "3", true }
                    },
                    .placeholder = "Select...",
                    .value = ""
                };
            };
        };

        Params params;

        std::function<void(Event&)> onChange;

        Dropdown(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Element(parent, styles) {

            name = "Dropdown";
            params = p;
            tabFocusable = true;

            this->styles.add(&Control);

            label = new Text(this, p.label, { &Label });

            dropdown = new Box(this, { &Field, &FieldFocus });

                fieldRow = new Box(dropdown, { &FieldInner });
                    dropdownText = new Text(fieldRow, "", { &FieldText });
                    dropdownArrow = new Svg(
                        fieldRow,
                        File("Rev/src/Elements/Controls/Dropdown/chevron-right.svg"),
                        { &DropdownArrow }
                    );

                optionsContainer = new Box(dropdown, { &OptionsContainer });
                optionsContainer->name = "OptionsContainer";

            dropdown->onLoseFocus([this](Event& e) {
                closeMenu(&e);
            });

            dropdown->onMouseDown([this](Event& e) {
                if (open) { closeMenu(&e); }
                else { openMenu(); }
            });
        }

        void keyDown(Event& e) override {

            tell(&Element::keyDown, e);

            if (!e.propagate) {
                return;
            }

            if (isActiveTabStop()) {
                handleMenuKeyDown(e);
            }
        }

        bool isSelectable(const Item& item) const {
            return !item.disabled;
        }

        int indexOfValue(const std::string& val) const {

            for (size_t i = 0; i < params.options.size(); i++) {

                if (params.options[i].value == val) {
                    return (int)i;
                }
            }

            return -1;
        }

        int firstSelectableIndex() const {

            for (size_t i = 0; i < params.options.size(); i++) {

                if (isSelectable(params.options[i])) {
                    return (int)i;
                }
            }

            return -1;
        }

        int nextSelectableIndex(int from, int direction) const {

            if (params.options.empty()) {
                return -1;
            }

            int count = (int)params.options.size();
            int index = from;

            for (int step = 0; step < count; step++) {

                index = (index + direction + count) % count;

                if (isSelectable(params.options[index])) {
                    return index;
                }
            }

            return -1;
        }

        void setMenuHighlight(int index, Event& e) {

            menuHighlight = index;

            for (size_t i = 0; i < options.size(); i++) {

                bool highlighted = open && (int)i == menuHighlight;

                if (options[i]->targetFlags.hover != highlighted) {
                    options[i]->targetFlags.hover = highlighted;
                    options[i]->dirty.style = true;
                }
            }

            refresh(e);
        }

        void confirmHighlighted(Event& e) {

            if (
                menuHighlight >= 0 &&
                menuHighlight < (int)params.options.size()
            ) {
                Item& item = params.options[menuHighlight];

                if (isSelectable(item)) {
                    select(item, &e);
                    return;
                }
            }

            closeMenu(&e);
            e.propagate = false;
        }

        void handleMenuKeyDown(Event& e) {

            if (!isActiveTabStop()) {
                return;
            }

            bool up = e.keyboard.arrows.up;
            bool down = e.keyboard.arrows.down;
            bool enter = e.keyboard.enter;
            bool escape = e.keyboard.escape;

            if (!open) {

                if (enter) {
                    openMenu();
                    e.propagate = false;
                    return;
                }

                if (up || down) {
                    openMenu();
                    e.propagate = false;
                    return;
                }

                return;
            }

            if (escape) {
                closeMenu(&e);
                e.propagate = false;
                return;
            }

            if (up) {
                setMenuHighlight(
                    nextSelectableIndex(menuHighlight, -1),
                    e
                );
                e.propagate = false;
                return;
            }

            if (down) {
                setMenuHighlight(
                    nextSelectableIndex(menuHighlight, +1),
                    e
                );
                e.propagate = false;
                return;
            }

            if (enter) {
                confirmHighlighted(e);
                return;
            }
        }

        Item getItemWithVal(const std::string& val) const {

            for (const Item& item : params.options) {

                if (item.value == val) {
                    return item;
                }
            }

            return { params.placeholder, "" };
        }

        Item getItemWithName(const std::string& name) const {

            for (const Item& item : params.options) {

                if (item.name == name) {
                    return item;
                }
            }

            return { params.placeholder, "" };
        }

        void select(Item item, Event* event = nullptr) {

            params.value = item.value;
            dropdownText->content = item.name;

            closeMenu(event);

            if (onChange && event) {
                onChange(*event);
            }
        }

        void openMenu() {

            dropdownArrow->transition(
                &dropdownArrow->rotation,
                3.14159f / 2.0f,
                200
            );

            optionsContainer->style->visibility = Visibility::Visible;
            open = true;

            int index = indexOfValue(params.value);

            if (index < 0 || !isSelectable(params.options[index])) {
                index = firstSelectableIndex();
            }

            menuHighlight = index;

            if (shared && shared->event) {
                setMenuHighlight(menuHighlight, *shared->event);
            }
        }

        void closeMenu(Event* e = nullptr) {

            dropdownArrow->transition(
                &dropdownArrow->rotation,
                3.14159f / 2.0f + 3.14159f,
                200
            );

            if (optionsContainer->style->visibility != Visibility::Hidden) {
                optionsContainer->style->visibility = Visibility::Hidden;
                optionsContainer->dirty.style = true;
            }

            open = false;
            menuHighlight = -1;

            for (Text* option : options) {

                if (option->targetFlags.hover) {
                    option->targetFlags.hover = false;
                    option->dirty.style = true;
                }
            }

            if (e) {
                refresh(*e);
            }

            else if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        void computeChildren(Event& e) override {

            dropdownText->content = getItemWithVal(params.value).name;

            size_t oldSize = options.size();
            size_t newSize = params.options.size();

            for (size_t i = newSize; i < oldSize; i++) {
                delete options[i];
            }

            options.resize(newSize);

            for (size_t i = oldSize; i < newSize; i++) {

                Item& item = params.options[i];
                options[i] = new Text(
                    optionsContainer,
                    item.name,
                    { &Option, &OptionHover, &OptionDisabled }
                );

                options[i]->onMouseDown([this, item](Event& ev) {
                    if (item.disabled) { return; }
                    select(item, &ev);
                });
            }

            for (size_t i = 0; i < newSize; i++) {

                Item& item = params.options[i];
                options[i]->content = item.name;

                bool highlighted = open && (int)i == menuHighlight;

                if (options[i]->targetFlags.hover != highlighted) {
                    options[i]->targetFlags.hover = highlighted;
                    options[i]->dirty.style = true;
                }

                if (options[i]->resolved.disabled != item.disabled) {
                    options[i]->resolved.disabled = item.disabled;
                    options[i]->dirty.style = true;
                }
            }
        }

        void computeStyle(Event& e) override {

            if (open && !isActiveTabStop()) {
                closeMenu(&e);
            }

            bool showFieldFocus = isActiveTabStop();

            if (dropdown->targetFlags.focus != showFieldFocus) {
                dropdown->targetFlags.focus = showFieldFocus;
                dropdown->dirty.style = true;
            }

            Element::computeStyle(e);
        }
    };
}
