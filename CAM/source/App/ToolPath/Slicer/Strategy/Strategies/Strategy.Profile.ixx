module;

#include <vector>
#include <memory>
#include <string>
#include <cmath>

export module Cam.App.Slicer.Strategy.Strategies.Profile;

import Geo.Chain;   // the path-theoretic offsetting engine (chains, profiles)

import Rev.Core.Pos;

import Cam.App.Model;
import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.SliceSource;
import Cam.App.Slicer.Strategy.Slice.Slice;
import Cam.App.Slicer.Strategy.Slice.Segment2;
import Cam.App.Slicer.Strategy.Slice.Chain;
import Cam.App.Slicer.Strategy.Slice.Profile;

export namespace Cam::App::Slicer::Strategy::Strategies {

    using Rev::Core::Pos;

    using SliceLayer = Slice::Slice;
    using SliceProfile = Slice::Profile;
    using Segment = Slice::Segment;

    // The PROFILE strategy, powered (secretly) by the Geo path-theoretic engine:
    // blind left-offsetting, crossing-number extraction, the handedness law, and
    // inter-chain mitosis -- the same method, end to end, that drives the Sketch
    // app. This strategy owns NO geometric logic of its own: it conditions the
    // slice section into doctrine-oriented Geo chains (CCW bounds the region to
    // clear, CW bounds keep-islands), marks free-space edges open-air, and then
    // simply asks Geo::Profile::offsetBy for generation after generation.
    struct Profile : Strategy {

        static constexpr const char* name() { return "Profile"; }

        static bool detect(const Model& model) {
            return false;
        }

        // Slice -> Geo conditioning
        //--------------------------------------------------

        // Slice source segments -> Geo stoicheia, at face value (lines stay
        // lines, arcs stay arcs; anything else tessellates).
        static std::vector<std::unique_ptr<Geo::Stoicheion>> toGeo(const std::vector<Segment>& src) {

            std::vector<std::unique_ptr<Geo::Stoicheion>> out;

            for (const Segment& s : src) {

                if (s.kind == Segment::Kind::Line) {
                    if ((s.p1 - s.p0).pythag() > 1e-6f) {
                        out.push_back(std::make_unique<Geo::Segment2>(s.p0, s.p1));
                    }
                }
                else if (s.kind == Segment::Kind::Arc) {
                    Pos a = s.start(), b = s.end(), m = s.pointAt(0.5f);
                    out.push_back(std::make_unique<Geo::Arc2>(Geo::Arc2::ThreePoint(a, m, b)));
                }
                else {
                    std::vector<Pos> pts;
                    s.sample(pts, 16);
                    for (size_t i = 0; i + 1 < pts.size(); i++) {
                        if ((pts[i + 1] - pts[i]).pythag() > 1e-6f) {
                            out.push_back(std::make_unique<Geo::Segment2>(pts[i], pts[i + 1]));
                        }
                    }
                }
            }

            return out;
        }

        // A Geo chain -> a slice chain (lines stay lines, arcs stay arcs with
        // their travel sense in the f1/f2 order; exotic edges tessellate).
        static Slice::Chain toSlice(const Geo::Chain& g) {

            Slice::Chain c;

            for (const auto& e : g.edges) {

                std::string k = e->kind();

                if (k == "segment") {
                    const Geo::Segment2* s = static_cast<const Geo::Segment2*>(e.get());
                    c.segments.push_back(Segment::Line(s->a, s->b));
                }
                else if (k == "arc") {
                    const Geo::Arc2* a = static_cast<const Geo::Arc2*>(e.get());
                    float a0, sweep; a->range(a0, sweep);
                    float angA = (a->a - a->c).angle();
                    float f2 = (a->chirality() > 0) ? angA + sweep : angA - sweep;
                    c.segments.push_back(Segment::Arc(a->c, a->radius(), angA, f2));
                }
                else if (k == "circle") {
                    const Geo::Circle2* k2 = static_cast<const Geo::Circle2*>(e.get());
                    float angA = (k2->a - k2->c).angle();
                    float f2 = (k2->chirality() > 0) ? angA + Geo::TAU : angA - Geo::TAU;
                    c.segments.push_back(Segment::Arc(k2->c, k2->radius(), angA, f2));
                }
                else {
                    std::vector<Pos> pts;
                    e->tessellate(pts);
                    for (size_t i = 0; i + 1 < pts.size(); i++) {
                        if ((pts[i + 1] - pts[i]).pythag() > 1e-6f) {
                            c.segments.push_back(Segment::Line(pts[i], pts[i + 1]));
                        }
                    }
                }
            }

            return c;
        }

        static SliceProfile::ChainRole roleOf(const Geo::Chain& g) {
            if (!g.closed) { return SliceProfile::ChainRole::Open; }
            float area = g.signedArea();
            if (area > 0.0f) { return SliceProfile::ChainRole::Outer; }
            if (area < 0.0f) { return SliceProfile::ChainRole::Hole; }
            return SliceProfile::ChainRole::Unknown;
        }

        static SliceProfile toSliceProfile(const Geo::Profile& g) {
            SliceProfile out;
            for (const Geo::Chain& c : g.chains) { out.push(toSlice(c), roleOf(c)); }
            return out;
        }

        // Doctrine orientation: a mesh section's winding is arbitrary, so chains
        // are conditioned AT THE DOOR -- nesting depth by exact winding around
        // each chain's own extreme point; even depth = region boundary (CCW),
        // odd depth = keep-island (CW). After this, the engine never reorients
        // anything: handedness is the meaning.
        static void orientByNesting(Geo::Profile& p) {

            for (Geo::Chain& c : p.chains) {

                if (!c.closed || c.edges.empty()) { continue; }

                Pos probe = Geo::Chain::loopMaxXPoint(c);
                int depth = 0;

                for (const Geo::Chain& other : p.chains) {
                    if (&other == &c || !other.closed) { continue; }
                    if (other.windingAround(probe) != 0) { depth += 1; }
                }

                int desired = (depth % 2 == 0) ? 1 : -1;
                if (c.turningSign() != desired) { c = c.reversed(); }
            }
        }

        // True when an edge lies ALONG the keep-out section (majority of its
        // samples within eps), not merely touching it at a corner.
        static bool edgeCoincident(
            const Geo::Stoicheion& e,
            const std::vector<Segment>& keepOut,
            float eps,
            int samples = 8
        ) {
            if (keepOut.empty()) { return false; }

            int near = 0;

            for (int i = 0; i <= samples; i++) {

                Pos p = Geo::Chain::edgePointAt(e, float(i) / float(samples));

                float best = 1e30f;
                for (const Segment& o : keepOut) { best = std::min(best, o.distanceTo(p)); }

                if (best <= eps) { near += 1; }
            }

            return near * 2 >= (samples + 1);
        }

        // Mark the free-space edges of region boundaries open-air. A keep-island
        // (CW) hugs keep-material all the way round, so it is never open-air --
        // the engine grows it away from the island naturally.
        static void markOpenAir(Geo::Profile& p, const std::vector<Segment>& keepOut, float nearEps = 1e-3f) {

            for (Geo::Chain& c : p.chains) {

                if (!c.closed || c.turningSign() <= 0) { continue; }   // outer (CCW) chains only

                for (auto& e : c.edges) {
                    if (!edgeCoincident(*e, keepOut, nearEps)) { e->openAir = true; }
                }
            }
        }

        // Profiles
        //--------------------------------------------------

        void processSlice(SliceLayer& slice, const StrategyContext& ctx) {

            slice.resetProfiles();
            slice.geometricProfile = SliceProfile(slice.source);

            if (slice.geometricProfile.empty()) {
                return;
            }

            // Section the negative "don't touch" model at this same height so we
            // know which boundary edges hug keep-out material and which face
            // free space.
            std::vector<Segment> keepOut = keepOutSection(ctx, slice.z);

            const float radius = toolRadius(ctx);
            const float step = stepoverDistance(ctx);

            // Profile 0: the section, doctrine-oriented, free edges marked open-air
            // and pre-pushed (1.333R: full corner coverage in the free region, and
            // safely clear of exact-tangency degeneracy).
            Geo::Profile p0;
            {
                std::vector<std::unique_ptr<Geo::Stoicheion>> ents = toGeo(slice.source);
                p0.chains = Geo::Chain::build(ents);
            }
            orientByNesting(p0);
            markOpenAir(p0, keepOut);
            for (Geo::Chain& c : p0.chains) { c = c.withOpenAirPushed(radius * 1.333f); }

            // The generations: a boundary pass at the tool radius (keep-out edges
            // retreat by R, islands grow by R, open edges end up traced), then
            // concentric stepover passes until extinction.
            std::vector<Geo::Profile> gens;
            Geo::Profile boundary = p0.offsetBy(radius);

            if (!boundary.empty()) {
                gens.push_back(std::move(boundary));

                constexpr size_t MaxRings = 512;
                while (gens.size() < MaxRings) {
                    Geo::Profile next = gens.back().offsetBy(step);
                    if (next.empty()) { break; }
                    gens.push_back(std::move(next));
                }
            }

            // Emit: [0] the geometric section, [1] the boundary pass (display),
            // [2..] every clearing ring, one profile per chain (buildPaths cuts
            // from index 2 on, so the boundary ring is also the first cut).
            slice.boundaryProfile = gens.empty() ? SliceProfile() : toSliceProfile(gens.front());

            slice.profiles.push_back(slice.geometricProfile);
            slice.profiles.push_back(slice.boundaryProfile);

            for (const Geo::Profile& g : gens) {
                for (const Geo::Chain& ch : g.chains) {
                    SliceProfile profile;
                    profile.push(toSlice(ch), roleOf(ch));
                    slice.profiles.push_back(profile);
                }
            }
        }

        // Section the negative keep-out model at `z`, returning its raw edges
        // in slice (u,v).  Empty when there is no negative model or it does not
        // intersect this height.
        static std::vector<Segment> keepOutSection(const StrategyContext& ctx, float z) {

            if (!ctx.negative) { return {}; }

            SliceLayer negSlice;

            if (!SliceSource::build(*ctx.negative, ctx.frame, z, negSlice)) {
                return {};
            }

            return negSlice.source;
        }

        // Paths
        //--------------------------------------------------

        void buildPaths(const StrategyContext& ctx) {

            paths_.clear();

            for (const SliceLayer& slice : slices_) {

                LayerPath layer;
                layer.z = slice.z;

                for (size_t i = 2; i < slice.profiles.size(); i++) {
                    slice.profiles[i].appendSegments(layer.segments, ctx.climbMilling);
                }

                if (layer.segments.empty()) { continue; }

                paths_.push_back(layer);
            }
        }
    };
}
