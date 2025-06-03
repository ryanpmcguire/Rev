module;

#include <vector>

#define DEBUG true
#include <dbg.hpp>

export module Rev.Box;

import Rev.Element;
import Rev.Style;
import Rev.Rect;

import WebGpu;
import WebGpu.Shader;
import WebGpu.Topology;
import WebGpu.Primitive;
import WebGpu.UniformBuffer;
import WebGpu.RoundedBox;
import WebGpu.Vertex;
import WebGpu.Color;

export namespace Rev {

    using namespace WebGpu;

    struct Box : public Element {

        RoundedBox* roundedBox = nullptr;

        // Test variable
        bool shouldRound = false;
        
        // Create
        Box(Element* parent) : Element(parent) {

            roundedBox = new RoundedBox(surface);
        }

        // Destroy
        ~Box() {
            delete roundedBox;
        }

        void computePrimitives() override {

            Style& styleRef = computed.style;

            // Test rect
            this->rect = {
                0, 0,
                styleRef.size.width.val, styleRef.size.height.val
            };
            
            // Box data
            //--------------------------------------------------

            RoundedBox::BoxData& boxData = roundedBox->boxData;

            roundedBox->boxDataBuffer->dirty = true;
            roundedBox->boxData = {

                rect.x, rect.y,
                rect.w, rect.h,

                styleRef.border.tl.radius.val + 1, styleRef.border.tr.radius.val + 1,
                styleRef.border.bl.radius.val + 1, styleRef.border.br.radius.val + 1,

                0, 0, 1, 1
            };

            Element::computePrimitives();
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

        void mouseDown(Event& e) override {

            // Toggle
            shouldRound = !shouldRound;
            style.apply(shouldRound ? roundedStyle : unRoundedStyle);

            //refresh(e);

            dbg("[Box] MouseClick!");
        }
    };
};