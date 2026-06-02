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

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolPreviewStyle {

        Style Self = {
            .overflow = Overflow::Hide,
            .size = { .width = Grow(), .height = Grow() },
            .background = { .color = rgba(20, 26, 38, 1.0) },
            .border = { .radius = 8_px }
        };
    }

    // A 2D silhouette of the tool's revolved cross-section, drawn with the
    // line primitive (SolidWorks-style tool preview).  The window feeds it the
    // current geometry; it fits the profile to its own rect.
    struct ToolPreview : public Box {

        struct Geometry {
            double radius = 0.5;
            double length = 100.0;
            double taperAngle = 0.0;      // degrees from horizontal
            double shoulderLength = 20.0;
            double collarRadius = 0.0;
            double collarDepth = 0.0;
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

            axis = new Lines(canvas, { &axisPts });
            outline = new Lines(canvas, { &outlinePts });
        }

        ~ToolPreview() {
            delete outline;
            delete axis;
        }

        void setGeometry(const Geometry& g, Event& e) {
            geometry = g;
            refresh(e);
        }

        // Build the right-half silhouette (x = radius from axis, y = up from
        // tip), bottom -> top, then mirror it into a closed loop.
        //
        // Profile (per Cam::App::Tool):
        //   tip + taper, cutting flutes to shoulderLength at `radius`,
        //   optional neck at cutting radius, then collarRadius over the top
        //   `collarDepth` (or shank at collarRadius from shoulder when depth=0).
        std::vector<Core::Vertex> buildProfileToolSpace() const {

            std::vector<std::pair<float, float>> right;  // (x, y) in mm

            const float r = static_cast<float>(std::max(geometry.radius, 0.0));
            const float len = static_cast<float>(std::max(geometry.length, 0.0));

            if (r <= 1e-4f || len <= 1e-4f) { return {}; }

            const float taper = static_cast<float>(geometry.taperAngle);
            float tipH = 0.0f;

            if (taper > 0.0f && taper < 89.9f) {
                tipH = r * std::tan(taper * 3.14159265358979f / 180.0f);
            }

            tipH = std::min(tipH, len);

            float shoulder = static_cast<float>(geometry.shoulderLength);
            shoulder = std::clamp(shoulder, tipH, len);

            // 0 collar radius means "same as cutting radius" (Cam::App::Tool).
            const float collarR = geometry.collarRadius > 1e-6
                ? static_cast<float>(geometry.collarRadius)
                : r;

            const bool useCollarRadius = std::abs(collarR - r) > 1e-4f;

            float topCollarDepth = static_cast<float>(std::max(geometry.collarDepth, 0.0));
            topCollarDepth = std::min(topCollarDepth, std::max(len - shoulder, 0.0f));

            float collarStart = len - topCollarDepth;

            if (collarStart < shoulder) {
                collarStart = shoulder;
                topCollarDepth = len - collarStart;
            }

            const bool topCollarBand =
                useCollarRadius &&
                topCollarDepth > 1e-4f &&
                collarStart > shoulder + 1e-4f;

            const bool shankAtCollarRadius =
                useCollarRadius &&
                !topCollarBand &&
                (len - shoulder) > 1e-4f;

            // Tip center.
            right.push_back({ 0.0f, 0.0f });

            // Tip corner / taper end.
            right.push_back({ r, tipH });

            // Flutes up to the shoulder.
            right.push_back({ r, shoulder });

            if (topCollarBand) {
                // Neck at cutting radius, then step out to the top collar/shank.
                right.push_back({ r, collarStart });
                right.push_back({ collarR, collarStart });
                right.push_back({ collarR, len });
            }
            else if (shankAtCollarRadius) {
                // No top band: shank at collar radius from the shoulder up.
                right.push_back({ collarR, shoulder });
                right.push_back({ collarR, len });
            }
            else {
                right.push_back({ r, len });
            }

            // Top center.
            right.push_back({ 0.0f, len });

            // Closed loop: right side then mirrored left side.
            std::vector<Core::Vertex> loop;
            loop.reserve(right.size() * 2);

            for (const auto& p : right) {
                loop.push_back(Core::Vertex(p.first, p.second));
            }

            for (size_t i = right.size(); i-- > 1;) {
                if (i == 0) { break; }
                loop.push_back(Core::Vertex(-right[i - 1].first, right[i - 1].second));
            }

            // Close back onto the tip.
            loop.push_back(Core::Vertex(right.front().first, right.front().second));

            return loop;
        }

        void computePrimitives(Event& e) override {

            outlinePts.clear();
            axisPts.clear();

            std::vector<Core::Vertex> tool = buildProfileToolSpace();

            if (tool.size() >= 2 && rect.w > 1.0f && rect.h > 1.0f) {

                float minX = tool[0].x, maxX = tool[0].x;
                float minY = tool[0].y, maxY = tool[0].y;

                for (const Core::Vertex& v : tool) {
                    minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
                    minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
                }

                const float spanX = std::max(maxX - minX, 1e-3f);
                const float spanY = std::max(maxY - minY, 1e-3f);

                const float margin = 16.0f;
                const float availW = std::max(rect.w - 2.0f * margin, 1.0f);
                const float availH = std::max(rect.h - 2.0f * margin, 1.0f);

                const float scale = std::min(availW / spanX, availH / spanY);

                const float usedH = spanY * scale;
                const float topY = rect.y + (rect.h - usedH) * 0.5f;
                const float cx = rect.x + rect.w * 0.5f;

                // Tool y grows from tip upward; screen y grows downward, so the
                // tip (y=0) lands at the bottom and the shank (y=max) at the top.
                auto toScreen = [&](const Core::Vertex& p, Core::Color c) {
                    return Core::Vertex(
                        cx + p.x * scale,
                        topY + (maxY - p.y) * scale,
                        c
                    );
                };

                const Core::Color outlineColor = { 0.62f, 0.78f, 1.0f, 1.0f };
                const Core::Color axisColor = { 0.45f, 0.52f, 0.66f, 0.7f };

                for (const Core::Vertex& v : tool) {
                    outlinePts.push_back(toScreen(v, outlineColor));
                }

                // Centerline (tool axis).
                axisPts.push_back(toScreen(Core::Vertex(0.0f, minY), axisColor));
                axisPts.push_back(toScreen(Core::Vertex(0.0f, maxY), axisColor));
            }

            if (axis) {
                axis->lines = {{
                    .pPoints = &axisPts,
                    .color = { 0.45f, 0.52f, 0.66f, 0.7f },
                    .strokeWidth = 1.0f,
                    .smoothing = 1.0f
                }};
                axis->compute();
            }

            if (outline) {
                outline->lines = {{
                    .pPoints = &outlinePts,
                    .color = { 0.62f, 0.78f, 1.0f, 1.0f },
                    .strokeWidth = 2.0f,
                    .smoothing = 1.0f
                }};
                outline->compute();
            }

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            if (axis && axisPts.size() >= 2) { axis->draw(); }
            if (outline && outlinePts.size() >= 2) { outline->draw(); }
        }
    };
}
