module;



#include <string>

#include <algorithm>



#include <managed.hpp>



export module Rev.Element.TextInput;



import Rev.Core.Pos;

import Rev.Core.Resource;

import Rev.Core.Observable;



import Rev.Element;

import Rev.Element.Event;

import Rev.Element.Style;



import Rev.Element.Box;

import Rev.Element.Text;

import Rev.Element.Svg;

import Rev.Element.ControlTheme;



export namespace Rev::Element {



    using namespace ControlTheme;



    struct TextInput : public Element {



        struct Params {



            std::string label;

            std::string placeholder;

            size_t maxLength = 500;

            bool selectAllOnFocus = true;



            static Params Default() {

                return {

                    .label = "Text Input",

                    .placeholder = "Enter text...",

                    .maxLength = 500,

                    .selectAllOnFocus = true

                };

            };

        };



        Params params;

        Observable<bool> value;



        Text* label = nullptr;



        Box* container = nullptr;

            Box* field = nullptr;

            Text* placeholderText = nullptr;

            Text* text = nullptr;



        TextInput(Element* parent, Params p = Params::Default(), StyleList styles = {}) : Element(parent, styles) {



            this->name = "TextInput";

            this->styles.add(&Control);



            this->params = p;

            tabFocusable = true;



            label = new Text(this, params.label, { &Label });



            container = new Box(this, { &Field, &FieldFocus });



            field = new Box(container, { &FieldInner });



                placeholderText = new Text(field, params.placeholder, { &Placeholder });



                text = new Text(field, "", { &FieldText });

                text->editable = true;

                text->selectable = true;

                text->selectAllOnFocus = params.selectAllOnFocus;



            installInputFilter();



            onKeyDown([this](Event& e) {



                if (!isActiveTabStop() || !e.keyboard.enter) {

                    return;

                }



                if (text->targetFlags.focus) {

                    text->loseFocus(e);

                }



                else {

                    text->gainFocus(e);

                }



                e.propagate = false;

            });

        }



        void keyDown(Event& e) override {



            tell(&Element::keyDown, e);



            if (!e.propagate) {

                return;

            }



            if (text->targetFlags.focus) {

                text->keyDown(e);

            }

        }



        void textInput(Event& e) override {



            tell(&Element::textInput, e);



            if (!e.propagate) {

                return;

            }



            if (text->targetFlags.focus) {

                text->textInput(e);

            }

        }



        static std::string previewAfterInput(

            Text* text,

            const std::string& input

        ) {



            std::string current = text->content.get();



            int left = std::min(text->selectAnchor, text->selectEnd);

            int right = std::max(text->selectAnchor, text->selectEnd);



            if (left != right) {

                current.erase(

                    (size_t)left,

                    (size_t)(right - left)

                );

                current.insert((size_t)left, input);

                return current;

            }



            int pos = std::clamp(

                text->cursor,

                0,

                (int)current.size()

            );



            current.insert((size_t)pos, input);



            return current;

        }



        virtual bool acceptProposedContent(const std::string& proposed) const {



            if (params.maxLength > 0 && proposed.size() > params.maxLength) {

                return false;

            }



            return true;

        }



        void installInputFilter() {



            text->onTextInput([this](Event& e) {



                if (e.keyboard.input == "\r" || e.keyboard.input == "\n") {

                    e.propagate = false;

                    return;

                }



                std::string proposed = previewAfterInput(

                    text,

                    e.keyboard.input

                );



                if (!acceptProposedContent(proposed)) {

                    e.propagate = false;

                }

            });

        }



        void computeStyle(Event& e) override {



            bool showFieldFocus =

                isActiveTabStop() || text->targetFlags.focus;



            if (container->targetFlags.focus != showFieldFocus) {

                container->targetFlags.focus = showFieldFocus;

                container->dirty.style = true;

            }



            bool empty = text->content.get().empty();



            Visibility visibility = empty

                ? Visibility::Visible

                : Visibility::Hidden;



            if (placeholderText->style->visibility != visibility) {

                placeholderText->style->visibility = visibility;

                placeholderText->dirty.style = true;

            }



            Element::computeStyle(e);

        }

    };

}

