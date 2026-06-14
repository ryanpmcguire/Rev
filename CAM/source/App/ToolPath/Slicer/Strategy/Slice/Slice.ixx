module;

#include <vector>
#include <memory>

export module Cam.App.Slicer.Strategy.Slice.Slice;

import Rev.Core.Pos;

import Geo.Strategy;

export namespace Cam::App::Slicer::Strategy::Slice {

    using namespace Rev::Core;

    // One planar slice of the model. ALL of its 2D geometry is Geo stoicheia
    // and chains: the OpenCASCADE section is converted once at ingestion
    // (SliceSource) and nothing downstream ever represents it any other way.
    struct Slice {

        float z = 0.0f;

        // Emit the outer (wall) profile at this slice? Roughing slices set this
        // false to DEFER the wall to the finishing pass; the finishing slice sets
        // it true. Forwarded to Geo::SliceParams::emitOuter by the strategy.
        bool emitOuter = true;

        // Bounds of source edges in XY.
        Pos min = {};
        Pos max = {};
        bool valid = false;

        // Raw section edges at this Z height, at face value (lines stay lines,
        // arcs stay arcs, full circles stay circles).
        std::vector<std::unique_ptr<Geo::Stoicheion>> source;

        // The slice strategy's complete result: display profiles per
        // generation, per-step sanity verdicts, and the slice's FINAL toolpath
        // -- ordered, link-tagged chains whose sequence and travel direction
        // ARE the tool's motion at this Z (climb sense, execution order and
        // entry points already resolved). This is what the 3D layer consumes.
        Geo::SliceResult result;

        Slice() = default;
        Slice(Slice&&) = default;
        Slice& operator=(Slice&&) = default;

        // State
        //--------------------------------------------------

        void clear() {

            source.clear();
            resetProfiles();

            min = {};
            max = {};
            valid = false;
        }

        void resetProfiles() {
            result.clear();
        }

        bool empty() const {
            return source.empty() && !hasProfiles();
        }

        bool hasProfiles() const {
            return !result.profiles.empty() || !result.toolpath.empty();
        }

        // Bounds
        //--------------------------------------------------

        void includePoint(const Pos& p) {

            if (!p) { return; }

            if (!valid) {
                min = p;
                max = p;
                valid = true;
                return;
            }

            min = Pos::min(min, p);
            max = Pos::max(max, p);
        }

        void addEdge(std::unique_ptr<Geo::Stoicheion> e) {

            if (!e) { return; }

            std::vector<Pos> pts;
            e->tessellate(pts);

            for (const Pos& p : pts) { includePoint(p); }

            source.push_back(std::move(e));
        }

        void addLine(const Pos& a, const Pos& b) {

            if ((b - a).pythag() <= 1e-6f) { return; }

            addEdge(std::make_unique<Geo::Segment2>(a, b));
        }
    };
}
