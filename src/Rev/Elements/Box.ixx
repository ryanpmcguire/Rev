module;

#include <string>
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
        Box(Element* parent, std::string name = "Box") : Element(parent, name) {
            rectangle = new Rectangle();
            lines = new Lines(10);
        }

        // Destroy
        ~Box() {
            delete rectangle;
            delete lines;
        }

        void computePrimitives(Event& e) override {

            Style& styleRef = computed.style;
            
            // Box data
            //--------------------------------------------------

            Rectangle::Data& data = *rectangle->data;

            //roundedBox->boxDataBuffer->dirty = true;
            data = {

                .rect = {
                    rect.x, rect.y,
                    rect.w, rect.h
                },

                .color = {
                    styleRef.background.color.r, styleRef.background.color.g,
                    styleRef.background.color.b, styleRef.background.color.a
                },
                
                .corners = {
                    styleRef.border.tl.radius.val, styleRef.border.tr.radius.val,
                    styleRef.border.bl.radius.val, styleRef.border.br.radius.val
                }
            };

            rectangle->dirty = true;

            Element::computePrimitives(e);
        }

        void draw(Event& e) override {
            
            rectangle->draw();
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