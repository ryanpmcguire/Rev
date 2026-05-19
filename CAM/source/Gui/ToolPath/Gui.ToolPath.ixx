module;

#include <vector>

export module Cam.Gui.ToolPath;

import Rev.Graphics.Canvas;

import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

import Rev.Core.Vertex3;

import Rev.Primitive.Lines3d;

import Cam.App.MaterialState;
import Cam.App.ToolPath;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    struct ToolPath {

        View3d::Actor* actor = nullptr;

        std::vector<Rev::Core::Vertex3> lines;

        // Create / destroy
        //--------------------------------------------------

        void create(Rev::Graphics::Canvas* canvas) {

            actor = new View3d::Actor();

            actor->visible = false;
            actor->selectable = false;
            actor->ownsLines = true;
            actor->includeInFit = false;

            actor->lines = new Primitives::Lines3d(canvas, {
                .lines = &lines
            });

            actor->lines->color = {
                1.0f,
                0.0f,
                1.0f,
                1.0f
            };
        }

        void destroy() {

            delete actor;
            actor = nullptr;

            lines.clear();
        }

        // State
        //--------------------------------------------------

        void clear() {

            lines.clear();

            if (!actor || !actor->lines) { return; }

            actor->visible = false;
            actor->lines->compute();
        }

        void sync(Cam::App::MaterialState* state) {

            if (!actor || !actor->lines) { return; }

            lines.clear();

            if (!state || !state->hasToolPath) {
                actor->visible = false;
                actor->lines->compute();
                return;
            }

            state->toolPath.buildLineSegments(lines);

            actor->visible = !lines.empty();
            actor->lines->compute();
        }
    };
}