module;

export module Interface;

import Rev.Element;
import Rev.Box;

export namespace HelloWorld {

    using namespace Rev;

    struct Interface : public Element {

        // Create
        Interface(Element* parent) : Element(parent) {

            Box* box = new Box(this);
            box->style = {
                .size = { .width = 50_px, .height = 50_px },
                .border = {
                    .tl = { .radius = 50_px },
                    .tr = { .radius = 50_px },
                    .bl = { .radius = 50_px },
                    .br = { .radius = 50_px }
                },
                .background { .color = Color(1, 0.5, 0.5, 1) },
            };
        }

        // Destroy
        ~Interface() {

        }
    };
};