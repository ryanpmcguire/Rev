module;

#include <algorithm>
#include <cmath>
#include <utility>
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
            .size = { 100_pct, Grow(), .min = { .height = 160_px } },
            .background = { .color = rgba(20, 26, 38, 1.0) },
            .border = { .radius = 8_px }
        };
    }

    struct ToolPreview : public Box {

        struct Geometry {
            double cuttingRadius = 0.5;
            double totalLength = 100.0;
            double cuttingTaperAngle = 0.0;
            double cuttingLength = 20.0;
            double shoulderDiameter = 0.0;
            double shoulderLength = 0.0;
            double shoulderTaperAngle = 45.0;
        };

        Geometry geometry;

        Lines* outline = nullptr;
        Lines* axis = nullptr;

        std::vector<Core::Vertex> outlinePts;
        std::vector<Core::Vertex> axisPts;

        ToolPreview(Element* parent, StyleList styles = {}) : Box(parent, styles, "ToolPreview") {

            if (styles.styles.empty()) {
                this->styles.add(&ToolPreviewStyle::Self);
            }

            Graphics::Canvas* canvas = shared->canvas;

            axis = new Lines(canvas);
            outline = new Lines(canvas);
        }

        ~ToolPreview() {
            delete outline;
            delete axis;
        }

        void setGeometry(const Geometry& g, Event& e) {
            geometry = g;
            refresh(e);
        }

        static float panelDimension(float rectDim, float resolvedDim) {

            if (rectDim > 1.0f) { return rectDim; }
            if (resolvedDim > 1.0f) { return resolvedDim; }
            return 180.0f;
        }

        std::vector<Core::Vertex> buildProfileToolSpace() const {

            std::vector<std::pair<float, float>> upper;

            const float cuttingR = static_cast<float>(std::max(geometry.cuttingRadius, 0.0));
            const float len = static_cast<float>(std::max(geometry.totalLength, 0.0));

            if (cuttingR <= 1e-4f || len <= 1e-4f) { return {}; }

            const float shoulderR = static_cast<float>(Cam::App::Tool::effectiveShoulderRadius(
                cuttingR,
                geometry.shoulderDiameter
            ));

            float tipH = static_cast<float>(Cam::App::Tool::tipTaperHeight(
                cuttingR,
                geometry.cuttingTaperAngle
            ));

            tipH = std::min(tipH, len);

            float cutEnd = static_cast<float>(geometry.cuttingLength);
            cutEnd = std::clamp(cutEnd, tipH, len);

            const float transitionH = static_cast<float>(Cam::App::Tool::shoulderTransitionHeight(
                cuttingR,
                shoulderR,
                geometry.shoulderTaperAngle
            ));

            float shoulderShankLen = static_cast<float>(std::max(geometry.shoulderLength, 0.0));

            const float maxShoulderBlock = std::max(len - cutEnd, 0.0f);
            const float shoulderBlock = transitionH + shoulderShankLen;

            if (shoulderBlock > maxShoulderBlock + 1e-4f) {
                shoulderShankLen = std::max(0.0f, maxShoulderBlock - transitionH);
            }

            const float taperEnd = cutEnd + transitionH;
            const float shankEnd = std::min(taperEnd + shoulderShankLen, len);

            const bool hasShoulderTaper =
                transitionH > 1e-4f &&
                std::fabs(shoulderR - cuttingR) > 1e-4f;

            const bool hasShoulderShank =
                shoulderShankLen > 1e-4f &&
                std::fabs(shoulderR - cuttingR) > 1e-4f;

            upper.push_back({ 0.0f, 0.0f });
            upper.push_back({ tipH, cuttingR });
            upper.push_back({ cutEnd, cuttingR });

            if (hasShoulderTaper) {
                upper.push_back({ taperEnd, shoulderR });
            }

            if (hasShoulderShank) {
                upper.push_back({ shankEnd, shoulderR });
            }

            if (shankEnd < len - 1e-4f) {
                const float tailR = hasShoulderShank || hasShoulderTaper
                    ? shoulderR
                    : cuttingR;

                upper.push_back({ len, tailR });
            }
            else if (!hasShoulderShank && !hasShoulderTaper) {
                upper.push_back({ len, cuttingR });
            }

            upper.push_back({ len, 0.0f });

            std::vector<Core::Vertex> loop;
            loop.reserve(upper.size() * 2);

            for (const auto& p : upper) {
                loop.push_back(Core::Vertex(p.first, p.second));
            }

            for (size_t i = upper.size(); i-- > 1;) {
                if (i == 0) { break; }
                loop.push_back(Core::Vertex(upper[i - 1].first, -upper[i - 1].second));
            }

            loop.push_back(Core::Vertex(upper.front().first, upper.front().second));

            return loop;
        }

        void updateLinePrimitive(
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

        void computePrimitives(Event& e) override {

            outlinePts.clear();
            axisPts.clear();

            const float panelW = panelDimension(rect.w, resolved.size.w.val);
            const float panelH = panelDimension(rect.h, resolved.size.h.val);
            const float originX = rect.w > 1.0f ? rect.x : 0.0f;
            const float originY = rect.h > 1.0f ? rect.y : 0.0f;

            std::vector<Core::Vertex> tool = buildProfileToolSpace();

            if (tool.size() >= 2) {

                float minX = tool[0].x, maxX = tool[0].x;
                float minY = tool[0].y, maxY = tool[0].y;

                for (const Core::Vertex& v : tool) {
                    minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
                    minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
                }

                const float spanX = std::max(maxX - minX, 1e-3f);
                const float spanY = std::max(maxY - minY, 1e-3f);

                const float margin = 14.0f;
                const float availW = std::max(panelW - 2.0f * margin, 1.0f);
                const float availH = std::max(panelH - 2.0f * margin, 1.0f);

                const float scale = std::min(availW / spanX, availH / spanY);

                const float usedW = spanX * scale;
                const float leftX = originX + (panelW - usedW) * 0.5f;
                const float midY = originY + panelH * 0.5f;

                auto toScreen = [&](const Core::Vertex& p, Core::Color c) {
                    return Core::Vertex(
                        leftX + (p.x - minX) * scale,
                        midY - p.y * scale,
                        c
                    );
                };

                const Core::Color outlineColor = { 0.62f, 0.78f, 1.0f, 1.0f };
                const Core::Color axisColor = { 0.45f, 0.52f, 0.66f, 0.7f };

                for (const Core::Vertex& v : tool) {
                    outlinePts.push_back(toScreen(v, outlineColor));
                }

                axisPts.push_back(toScreen(Core::Vertex(minX, 0.0f), axisColor));
                axisPts.push_back(toScreen(Core::Vertex(maxX, 0.0f), axisColor));
            }

            updateLinePrimitive(
                axis,
                axisPts,
                { 0.45f, 0.52f, 0.66f, 0.7f },
                1.0f
            );

            updateLinePrimitive(
                outline,
                outlinePts,
                { 0.62f, 0.78f, 1.0f, 1.0f },
                2.0f
            );

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            if (axis && axis->numVerts > 0) { axis->draw(); }
            if (outline && outline->numVerts > 0) { outline->draw(); }
        }
    };
}
