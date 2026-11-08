module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

export module Rev.Primitive.SampledCurve;

import Rev.Primitive.FastLines;
import Rev.Core.Vertex;
import Rev.Core.Rect;

import Rev.Graphics.Canvas;

export namespace Rev::Primitives {

    using namespace Rev::Core;

    // A FastLines that draws y = f(x) by asking its owner for exactly the points
    // the screen needs: rect.w / density samples across the visible domain.
    // It requeries only when its Stamp changes; otherwise the last vertices stand.
    // A NaN y lifts the pen, so gaps in the function draw as gaps.
    struct SampledCurve : public FastLines {

        // Owner's units -> one sample. The callable belongs to the owner, who must
        // keep everything it captures alive while it is set, and clear() it in the
        // owner's destructor.
        using Query = std::function<double(double x)>;

        // A visible span of the owner's units.
        struct Domain {
            double from = 0.0;
            double to = 1.0;

            bool operator==(const Domain&) const = default;
        };

        // Everything the samples depend on. Sampled once per distinct Stamp.
        struct Stamp {
            uint64_t revision = 0;   // the owner's data revision; bump() when f changes
            Domain x;                // visible x span
            Domain y;                // visible y span (maps to rect, top = y.to)
            Rect rect;               // on-screen pixels
            float density = 6.0f;    // pixels per sample

            bool operator==(const Stamp&) const = default;
        };

        Query query;                 // owner's f(x); empty draws nothing
        Stamp stamp;                 // what the owner wants shown
        Stamp sampled;               // what the current vertices were sampled for
        bool hasSampled = false;     // no vertices yet

        std::vector<std::vector<Vertex>> runs;   // pen-down runs, storage reused

        // Create
        SampledCurve(Canvas* canvas) : FastLines(canvas) {}

        // The owner's data changed: the next compute() requeries.
        void bump() { stamp.revision++; }

        // Owner's setters; each only edits the stamp.
        void set(Query q) { query = std::move(q); bump(); }
        void clear() { query = nullptr; bump(); }
        void setDomain(Domain x, Domain y) { stamp.x = x; stamp.y = y; }
        void setRect(Rect rect) { stamp.rect = rect; }
        void setDensity(float density) { stamp.density = density; }

        // Requery only when the stamp moved, then hand the runs to FastLines.
        void compute() override {

            if (hasSampled && stamp == sampled) { return; }

            sample();
            sampled = stamp;
            hasSampled = true;

            FastLines::compute();
        }

        // Walk the visible domain at density, splitting runs at NaN.
        void sample() {

            size_t used = 0;

            const Rect& r = stamp.rect;
            const double spanX = stamp.x.to - stamp.x.from;
            const double spanY = stamp.y.to - stamp.y.from;

            if (query && r.w > 0 && stamp.density > 0 && spanX != 0 && spanY != 0) {

                const size_t count = static_cast<size_t>(std::ceil(r.w / stamp.density)) + 1;
                bool penDown = false;

                for (size_t i = 0; i < count; i++) {

                    const double t = static_cast<double>(i) / static_cast<double>(count - 1);
                    const double y = query(stamp.x.from + t * spanX);

                    if (!std::isfinite(y)) { penDown = false; continue; }

                    if (!penDown) {
                        if (runs.size() <= used) { runs.emplace_back(); }
                        runs[used].clear();
                        used++;
                        penDown = true;
                    }

                    Vertex v;
                    v.x = static_cast<float>(r.x + t * r.w);
                    v.y = static_cast<float>(r.y + r.h * (1.0 - (y - stamp.y.from) / spanY));
                    runs[used - 1].push_back(v);
                }
            }

            // Point FastLines at the runs (pointers are stable: runs only grows).
            while (lines.size() < runs.size()) { lines.push_back({}); }
            for (size_t i = 0; i < runs.size(); i++) { lines[i].pPoints = &runs[i]; }
            activeLineCount = used;
        }
    };
};
