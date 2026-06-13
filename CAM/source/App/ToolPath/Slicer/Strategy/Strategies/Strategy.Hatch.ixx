module;

#include <vector>
#include <memory>
#include <cmath>
#include <cstddef>
#include <algorithm>

export module Cam.App.Slicer.Strategy.Strategies.Hatch;

import Rev.Core.Pos;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using namespace Rev::Core;

    using SliceLayer = Slice::Slice;

    // The HATCH strategy: the shared Geo::SliceStrategy produces the boundary
    // (one generation, exactly the tool radius, keep-out aware, climb sense
    // and links resolved), then a scanline raster fill mows the interior.
    // All 2D geometry is Geo stoicheia end to end.
    struct Hatch : Strategy {

        static constexpr const char* name() { return "Hatch"; }

        static bool detect(const Model& model) {
            return model.loaded;
        }

        // Profiles
        //--------------------------------------------------

        void processSlice(SliceLayer& slice, const StrategyContext& ctx) {

            slice.resetProfiles();

            if (slice.source.empty()) { return; }

            std::vector<std::unique_ptr<Geo::Stoicheion>> keepOut = keepOutSection(ctx, slice.z);

            const float radius = toolRadius(ctx);

            // Condition exactly like the Profile strategy: doctrine
            // orientation, keep-out coincidence, open-air marks.
            Geo::Profile seed;
            seed.chains = Geo::Chain::build(slice.source);

            condition(seed);
            markOpenAir(seed, keepOut);

            // ONE generation: the boundary pass at exactly the tool radius.
            Geo::SliceParams params;
            params.kind = Geo::StrategyKind::Profile;
            params.toolRadius = radius;
            params.stepover = 1.0f;
            params.maxGenerations = 1;
            params.reverse = ctx.insideOut;
            params.climb = ctx.climbMilling;

            Geo::SliceStrategy strategy(params);
            strategy.ingest(std::move(seed));
            strategy.run();

            slice.result = std::move(strategy.result);
        }

        // Paths
        //--------------------------------------------------

        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            const float stepover = stepoverDistance(ctx);

            for (const SliceLayer& slice : slices_) {

                // The boundary: generation 1 of the slice strategy.
                if (slice.result.profiles.size() < 2) { continue; }

                const Geo::Profile& boundary = slice.result.profiles[1];

                LayerPath layer;
                layer.z = slice.z;

                // Inner roughing: scanline raster fill.
                lawnmower(boundary, stepover, layer.chains);

                // Always trace the boundary itself (outer + holes), exactly as
                // the slice strategy resolved it: climb sense, entries, links.
                for (const Geo::Chain& c : slice.result.toolpath) {
                    layer.chains.push_back(c.clone());
                }

                if (layer.chains.empty()) { continue; }

                paths_.push_back(std::move(layer));
            }
        }

        // Scanline crossings
        //==================================================

        static constexpr size_t NPOS = ~static_cast<size_t>(0);

        struct XHit {
            float x = 0.0f;
            size_t chain = NPOS;   // index into boundary.chains
        };

        // All crossings of one edge with the horizontal line at `y`, appended
        // as x coordinates. Tangent kisses are skipped -- only transversal
        // crossings partition the scan ray into inside/outside spans.
        static void edgeHitsAtY(const Geo::Stoicheion& e, float y, std::vector<float>& xs) {

            Geo::SKind k = e.type();

            if (k == Geo::SKind::Segment) {

                const Geo::Segment2* s = static_cast<const Geo::Segment2*>(&e);

                // Half-open straddle: each vertex belongs to exactly one of
                // its two edges, so a scan ray through a vertex counts once.
                if ((s->a.y <= y) == (s->b.y <= y)) { return; }

                float t = (y - s->a.y) / (s->b.y - s->a.y);
                xs.push_back(s->a.x + (s->b.x - s->a.x) * t);

                return;
            }

            Pos c; float r, a0, sweep; int chir;

            if (Geo::Chain::circularOf(e, c, r, a0, sweep, chir)) {

                float dy = y - c.y;

                // A grazing kiss (|dy| ~ r) is not a crossing.
                if (std::fabs(dy) >= r - 1e-4f) { return; }

                float dx = std::sqrt(std::max(0.0f, r * r - dy * dy));

                for (float x : { c.x - dx, c.x + dx }) {

                    Pos p = { x, y };

                    bool full = std::fabs(sweep) >= Geo::TAU - 1e-4f;

                    if (full || Geo::Chain::onSpan(c, a0, sweep, p)) {
                        xs.push_back(x);
                    }
                }

                return;
            }

            // Exotic edges: certified root scan over the travel parameter.
            std::vector<double> roots;

            Geo::Chain::scanRoots(
                [&](double f) {
                    return double(Geo::Chain::edgePointAt(e, float(f)).y) - double(y);
                },
                64,
                roots
            );

            for (double f : roots) {
                xs.push_back(Geo::Chain::edgePointAt(e, float(f)).x);
            }
        }

        // All boundary crossings at `y`, sorted by x and deduplicated, each
        // remembering which chain it lies on (for boundary-following links).
        static void hitsAtY(const Geo::Profile& boundary, float y, std::vector<XHit>& hits) {

            hits.clear();

            for (size_t ci = 0; ci < boundary.chains.size(); ci++) {

                const Geo::Chain& ch = boundary.chains[ci];

                if (!ch.closed) { continue; }

                std::vector<float> xs;

                for (const auto& e : ch.edges) {
                    edgeHitsAtY(*e, y, xs);
                }

                for (float x : xs) {
                    hits.push_back({ x, ci });
                }
            }

            std::sort(hits.begin(), hits.end(), [](const XHit& a, const XHit& b) { return a.x < b.x; });

            // Coincident crossings (a vertex shared by two chains, a doubled
            // root) collapse to one.
            constexpr float eps = 1e-4f;

            for (size_t i = 1; i < hits.size(); ) {
                if (hits[i].x - hits[i - 1].x <= eps) { hits.erase(hits.begin() + i); }
                else { i += 1; }
            }
        }

        // Boundary tracing
        //==================================================

        static float chainPathLength(const Geo::Chain& ch) {

            float total = 0.0f;

            for (const auto& e : ch.edges) {
                total += Geo::Chain::edgeLength(*e);
            }

            return total;
        }

        // Walk a closed loop from `from` forward to `to`, returning the traced
        // sub-path as an open chain (possibly empty when the points coincide).
        static Geo::Chain walkTo(const Geo::Chain& loop, Pos from, Pos to) {

            Pos f;
            size_t kf = loop.nearestPoint(from, f);

            Geo::Chain seated = loop.startedAt(kf, f);

            Pos t;
            size_t kt = seated.nearestPoint(to, t);

            Geo::Chain out;
            out.closed = false;

            for (size_t i = 0; i < kt && i < seated.edges.size(); i++) {
                out.edges.push_back(seated.edges[i]->clone());
            }

            if (kt < seated.edges.size()) {

                const Geo::Stoicheion& last = *seated.edges[kt];

                if ((t - Geo::Chain::eStart(last)).pythag() > 1e-4f) {
                    std::unique_ptr<Geo::Stoicheion> first;
                    std::unique_ptr<Geo::Stoicheion> second;
                    Geo::Chain::splitEdge(last, t, first, second);
                    out.edges.push_back(std::move(first));
                }
            }

            return out;
        }

        // Trace along a boundary loop from `from` to `to` THE SHORTER WAY --
        // exact geometry, never crossing the boundary.
        static Geo::Chain traceAlong(const Geo::Chain& loop, Pos from, Pos to) {

            Geo::Chain fwd = walkTo(loop, from, to);
            Geo::Chain bwd = walkTo(loop.reversed(), from, to);

            if (chainPathLength(fwd) <= chainPathLength(bwd)) { return fwd; }

            return bwd;
        }

        // Lawn-mower fill
        //==================================================

        // One end of a fill lane. It always lies on a boundary chain, so we
        // record which chain so a link can be traced exactly along it.
        struct EndRef {
            Pos p;
            size_t chain = NPOS;
        };

        // A fill lane: a single scan ray clipped to inside the region. `a` is
        // the left end, `b` the right end. We mow from whichever end we enter
        // to the other, so direction is explicit.
        struct Fill {
            EndRef a;
            EndRef b;
            bool traced = false;
        };

        // The Euclidean-nearest untraced endpoint to `from`. This is the
        // virtual look-ahead: we do NOT move the tool to search, we just find
        // the closest unreached lane end and head for it.
        static bool nearestEndpoint(const std::vector<Fill>& fills, const Pos& from, size_t& outFill, bool& outIsA) {

            float bestDistance = 1e30f;
            bool found = false;

            for (size_t i = 0; i < fills.size(); i++) {

                if (fills[i].traced) { continue; }

                for (int side = 0; side < 2; side++) {

                    const bool isA = side == 0;
                    const EndRef& e = isA ? fills[i].a : fills[i].b;

                    float d = from.distanceTo(e.p);

                    if (d < bestDistance) {
                        bestDistance = d;
                        outFill = i;
                        outIsA = isA;
                        found = true;
                    }
                }
            }

            return found;
        }

        // Scanline raster fill.
        //
        // 1. Each horizontal scan ray is split by the boundary into inside
        //    spans, exactly like font rasterization: the negative area (outer
        //    exterior and every hole) "cuts" the ray.
        //
        // 2. The fills are threaded together: after mowing a lane we look
        //    ahead VIRTUALLY for the nearest unreached endpoint. If it sits on
        //    the boundary chain we just left, we link to it ALONG that chain
        //    the shorter way (a Cut-tagged trace -- the tool stays down and
        //    never crosses the boundary). Otherwise we leave a genuine break,
        //    which the point builder turns into a safe retract/rapid/plunge.
        static void lawnmower(const Geo::Profile& boundary, float stepover, std::vector<Geo::Chain>& out) {

            if (boundary.chains.empty()) { return; }
            if (stepover <= 0.0f) { return; }

            // --- Bounds of the boundary ---

            bool haveBounds = false;
            Pos mn = {};
            Pos mx = {};

            for (const Geo::Chain& ch : boundary.chains) {

                if (!ch.closed) { continue; }

                for (const auto& e : ch.edges) {

                    std::vector<Pos> pts;
                    e->tessellate(pts);

                    for (const Pos& p : pts) {
                        if (!haveBounds) { mn = p; mx = p; haveBounds = true; continue; }
                        mn = Pos::min(mn, p);
                        mx = Pos::max(mx, p);
                    }
                }
            }

            if (!haveBounds) { return; }

            // --- 1. Build fills by splitting scan rays at boundary crossings ---

            std::vector<Fill> fills;
            std::vector<XHit> hits;

            for (float y = mn.y + stepover * 0.5f; y <= mx.y; y += stepover) {

                hitsAtY(boundary, y, hits);

                const size_t pairCount = hits.size() / 2;

                for (size_t k = 0; k < pairCount; k++) {

                    Fill f;
                    f.a = { { hits[k * 2].x, y }, hits[k * 2].chain };
                    f.b = { { hits[k * 2 + 1].x, y }, hits[k * 2 + 1].chain };

                    fills.push_back(f);
                }
            }

            if (fills.empty()) { return; }

            // --- 2. Thread the fills via nearest-endpoint look-ahead ---

            size_t curFill = 0;
            bool curIsA = true;       // enter the first lane at its left end

            // Start at the lowest, then leftmost lane.
            for (size_t i = 1; i < fills.size(); i++) {
                const Pos& p = fills[i].a.p;
                const Pos& q = fills[curFill].a.p;
                if (p.y < q.y || (p.y == q.y && p.x < q.x)) { curFill = i; }
            }

            size_t tracedCount = 0;

            while (tracedCount < fills.size()) {

                Fill& f = fills[curFill];

                EndRef enter = curIsA ? f.a : f.b;
                EndRef leave = curIsA ? f.b : f.a;

                // Mow this lane.
                Geo::Chain lane;
                lane.closed = false;
                lane.edges.push_back(std::make_unique<Geo::Segment2>(enter.p, leave.p));
                out.push_back(std::move(lane));

                f.traced = true;
                tracedCount += 1;

                if (tracedCount >= fills.size()) { break; }

                // Virtual look-ahead to the nearest unreached endpoint.
                size_t nextFill = 0;
                bool nextIsA = true;

                if (!nearestEndpoint(fills, leave.p, nextFill, nextIsA)) { break; }

                const EndRef& target = nextIsA ? fills[nextFill].a : fills[nextFill].b;

                // If it lies on the chain we just left, link along the
                // boundary the shorter way; otherwise leave a break for a
                // safe retract.
                if (
                    leave.chain != NPOS &&
                    leave.chain == target.chain &&
                    leave.chain < boundary.chains.size()
                ) {
                    Geo::Chain link = traceAlong(boundary.chains[leave.chain], leave.p, target.p);

                    if (!link.edges.empty()) {
                        link.link = Geo::LinkKind::Cut;
                        out.push_back(std::move(link));
                    }
                }

                curFill = nextFill;
                curIsA = nextIsA;
            }
        }
    };
}
