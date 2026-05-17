module;

#include <cstddef>
#include <cmath>
#include <string>

#include <dbg.hpp>

export module Interface;

import Rev.Element;
import Rev.Element.Style;
import Rev.Element.Event;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Slider;
import Rev.Element.Dropdown;
import Rev.Element.Radio;
import Rev.Element.Checkbox;
import Rev.Element.TextInput;
import Rev.Element.Chart;

import Rev.Serial;

import Rev.Window;

import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

export namespace HelloWorld {

    using namespace Rev;
    using namespace Rev::Element;

    struct Interface : public Box {

        View3d* view3d = nullptr;

        Actor3D* cubeActor = nullptr;
        bool cubeInView = false;

        // Create
        Interface(Element* parent) : Box(parent) {

            // Self
            this->style->layout = { Axis::Horizontal, Align::Center, Align::Center };
            this->style->background.color = rgba(0, 0, 0, 0.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 10_px, 10_px, 10_px, 10_px };

            view3d = new View3d(this);

            cubeActor = Actor3D::Cube(shared->canvas);
            view3d->addActor(cubeActor);
            cubeInView = true;
        }

        // Destroy
        ~Interface() {

            if (view3d && cubeActor) {
                view3d->removeActor(cubeActor);
            }

            delete cubeActor;
            cubeActor = nullptr;
        }
    };
};