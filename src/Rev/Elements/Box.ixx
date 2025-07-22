module;

#include <vector>
#include <dbg.hpp>

export module Rev.Box;

import Rev.Element;
import Rev.Style;
import Rev.Rect;

import Rev.OpenGL.Rectangle;
import Rev.OpenGL.Lines;

export namespace Rev {

    //using namespace WebGpu;

    struct Box : public Element {

        Rectangle* rectangle = nullptr;
        Lines* lines = nullptr;

        // Create
        Box(Element* parent) : Element(parent) {
            rectangle = new Rectangle();
            lines = new Lines(4);
        }

        // Destroy
        ~Box() {
            delete rectangle;
            delete lines;
        }

        void computePrimitives(Event& e) override {

            Style& styleRef = computed.style;

            // Test rect
            this->rect = {
                0, 0,
                styleRef.size.width.val, styleRef.size.height.val
            };
            
            // Box data
            //--------------------------------------------------

            Rectangle::Data& data = *rectangle->data;

            //roundedBox->boxDataBuffer->dirty = true;
            data = {

                .rect = {
                    rect.x, rect.y,
                    rect.w, rect.h
                },

                .color = { 1, 1, 1, 1 },
                
                .corners = {
                    styleRef.border.tl.radius.val, styleRef.border.tr.radius.val,
                    styleRef.border.bl.radius.val, styleRef.border.br.radius.val
                }
            };

            Element::computePrimitives(e);
        }

        void draw(Event& e) override {
            //rectangle->draw();

            lines->dirty = true;
            lines->points = {
                {50, 50}, {100, 100}, {200, 50}, {300, 500}
            };

            lines->draw();
        }

        // Events
        //--------------------------------------------------

        Style roundedStyle = {
            .border = {
                .tl = { .radius = Px(20) },
                .tr = { .radius = Px(20) },
                .bl = { .radius = Px(20) },
                .br = { .radius = Px(20) }
            }
        };

        Style unRoundedStyle = {
            .border = {
                .tl = { .radius = Px(0) },
                .tr = { .radius = Px(0) },
                .bl = { .radius = Px(0) },
                .br = { .radius = Px(0) }
            }
        };

        bool shouldRound = false;
        
        void mouseDown(Event& e) override {

            // Toggle
            shouldRound = !shouldRound;
            style.apply(shouldRound ? roundedStyle : unRoundedStyle);

            refresh(e);

            dbg("[Box] MouseClick!");
        }
    };
};