module;

#include <algorithm>
#include <cmath>
#include <vector>

export module Cam.Gui.ToolPreview;

import Rev.Core.Vertex;
import Rev.Core.Color;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;

import Rev.Primitive.Lines;

import Rev.Graphics.Canvas;

import Cam.App.Tool;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolPreviewStyle {

        Style Self = {
            .overflow = Overflow::Hide,
            .size = { .width = 100_pct, .height = 240_px },
            .background = { .color = rgba(20, 26, 38, 1.0) },
            .border = { .radius = 8_px }
        };
    }

    // 2D silhouette of the tool, drawn with the line primitive.  It mirrors the
    // exact same Tool::profile() the 3D mesh revolves, so the two previews can
    // never disagree.
    struct ToolPreview : public Box {

        Cam::App::Tool tool;

        Lines* outline = nullptr;
        Lines* axisLine = nullptr;

        std::vector<Core::Vertex> outlinePts;
        std::vector<Core::Vertex> axisPts;

        ToolPreview(Element* parent, StyleList styles = {}) : Box(parent, styles, "ToolPreview") {

            if (this->styles.styles.empty()) {
                this->styles.add(&ToolPreviewStyle::Self);
            }

            Graphics::Canvas* canvas = shared->canvas;

            axisLine = new Lines(canvas);
            outline = new Lines(canvas);
        }

        ~ToolPreview() {
            delete outline;
            delete axisLine;
        }

        void setTool(const Cam::App::Tool& t, Event& e) {
            tool = t;
            refresh(e);
        }

        // Mirror the right-hand profile into a closed 2D loop (tool space:
        // x = radius from centerline, y = axial height from the tip).
        std::vector<Core::Vertex> buildLoopToolSpace() const {

            const std::vector<Cam::App::Tool::ProfilePoint> p = tool.profile();

            std::vector<Core::Vertex> loop;

            if (p.size() < 2) { return loop; }

            loop.reserve(p.size() * 2);

            for (const auto& pt : p) {
                loop.push_back(Core::Vertex(float(pt.r), float(pt.y)));
            }

            for (size_t i = p.size() - 1; i-- > 1;) {
                loop.push_back(Core::Vertex(-float(p[i].r), float(p[i].y)));
            }

            loop.push_back(Core::Vertex(float(p.front().r), float(p.front().y)));

            return loop;
        }

        void computePrimitives(Event& e) override {

            outlinePts.clear();
            axisPts.clear();

            const float panelW = rect.w > 1.0f ? rect.w : 180.0f;
            const float panelH = rect.h > 1.0f ? rect.h : 220.0f;
            const float originX = rect.x;
            const float originY = rect.y;

            const std::vector<Core::Vertex> loop = buildLoopToolSpace();

            if (loop.size() >= 2) {

                float minX = loop[0].x, maxX = loop[0].x;
                float minY = loop[0].y, maxY = loop[0].y;

                for (const Core::Vertex& v : loop) {
                    minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
                    minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
                }

                const float spanX = std::max(maxX - minX, 1e-3f);
                const float spanY = std::max(maxY - minY, 1e-3f);

                const float margin = 16.0f;
                const float availW = std::max(panelW - 2.0f * margin, 1.0f);
                const float availH = std::max(panelH - 2.0f * margin, 1.0f);

                const float scale = std::min(availW / spanX, availH / spanY);

                const float usedH = spanY * scale;
                const float topY = originY + (panelH - usedH) * 0.5f;
                const float cx = originX + panelW * 0.5f;

                // Tool y grows from tip upward; screen y grows downward, so the
                // tip lands at the bottom and the shank at the top.
                auto toScreen = [&](float x, float y, Core::Color c) {
                    return Core::Vertex(cx + x * scale, topY + (maxY - y) * scale, c);
                };

                const Core::Color outlineColor = { 0.62f, 0.78f, 1.0f, 1.0f };
                const Core::Color axisColor = { 0.45f, 0.52f, 0.66f, 0.7f };

                for (const Core::Vertex& v : loop) {
                    outlinePts.push_back(toScreen(v.x, v.y, outlineColor));
                }

                axisPts.push_back(toScreen(0.0f, minY, axisColor));
                axisPts.push_back(toScreen(0.0f, maxY, axisColor));
            }

            updateLine(axisLine, axisPts, { 0.45f, 0.52f, 0.66f, 0.7f }, 1.0f);
            updateLine(outline, outlinePts, { 0.62f, 0.78f, 1.0f, 1.0f }, 2.0f);

            Box::computePrimitives(e);
        }

        void updateLine(
            Lines* lines,
            std::vector<Core::Vertex>& points,
            const Core::Color& color,
            float strokeWidth
        ) {
            if (!lines) { return; }

            if (points.size() >= 2) {
                lines->color = color;
                lines->strokeWidth = strokeWidth;
                lines->smoothing = 1.0f;
                lines->lines = {{
                    .pPoints = &points,
                    .color = color,
                    .strokeWidth = strokeWidth,
                    .smoothing = 1.0f
                }};
            }
            else {
                lines->lines.clear();
            }

            lines->compute();
        }

        void draw(Event& e) override {

            Box::draw(e);

            if (axisLine && axisPts.size() >= 2) { axisLine->draw(); }
            if (outline && outlinePts.size() >= 2) { outline->draw(); }
        }
    };
}
