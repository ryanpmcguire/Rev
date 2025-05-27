module;

export module Rev.Text;

import Rev.Element;

export namespace Rev {

    struct Text : public Element {

        // Create
        Text(Element* parent) : Element(parent) {

        }

        // Destroy
        ~Text() {

        }
    };
};