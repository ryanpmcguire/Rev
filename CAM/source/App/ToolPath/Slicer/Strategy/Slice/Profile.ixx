module;

#include <vector>
#include <cstddef>
#include <cmath>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.Slicer.Strategy.Slice.Profile;

import Rev.Core.Pos;

import Cam.App.Slicer.Strategy.Slice.Segment2;
import Cam.App.Slicer.Strategy.Slice.Chain;

export namespace Cam::App::Slicer::Strategy::Slice {

    using namespace Rev::Core;

    struct Profile {

        enum class ChainRole {
            Unknown,
            Outer,
            Hole,
            Open
        };

        struct Entry {

            Chain chain;
            ChainRole role = ChainRole::Unknown;

            Entry() {}

            Entry(const Chain& chain, ChainRole role = ChainRole::Unknown) {
                this->chain = chain;
                this->role = role;
            }

            bool closed() const {
                return chain.closed();
            }

            bool outer() const {
                return role == ChainRole::Outer;
            }

            bool hole() const {
                return role == ChainRole::Hole;
            }

            bool open() const {
                return role == ChainRole::Open;
            }
        };

        // Chains classified as outer boundaries, holes, or open contours.
        std::vector<Entry> entries;

        // Create
        //--------------------------------------------------

        Profile() {}

        Profile(const std::vector<Chain>& chains) {
            build(chains);
        }

        Profile(const std::vector<Segment>& segments, float eps = 1e-4f) {
            build(Chain::BuildAll(segments, eps));
        }

        static Profile From(const std::vector<Chain>& chains) {
            return Profile(chains);
        }

        static Profile FromSegments(const std::vector<Segment>& segments, float eps = 1e-4f) {
            return Profile(segments, eps);
        }

        // State
        //--------------------------------------------------

        void clear() {
            entries.clear();
        }

        bool empty() const {
            return entries.empty();
        }

        size_t size() const {
            return entries.size();
        }

        void push(const Chain& chain, ChainRole role = ChainRole::Unknown) {
            entries.push_back({
                chain,
                role
            });
        }

        // Build / classify
        //--------------------------------------------------

        void build(const std::vector<Chain>& chains) {
            clear();

            for (const Chain& chain : chains) {

                ChainRole role = classifyRole(chain);
                push(chain, role);
            }

            classifyContainment();
        }

        ChainRole classifyRole(const Chain& chain) const {
            if (!chain.closed()) {
                return ChainRole::Open;
            }

            if (chain.zeroWinding()) {
                return ChainRole::Unknown;
            }

            return ChainRole::Outer;
        }

        void classifyContainment() {

            for (Entry& entry : entries) {

                if (!entry.closed()) {
                    entry.role = ChainRole::Open;
                    continue;
                }

                Pos p = entry.chain.start();

                int containingClosedChains = 0;

                for (const Entry& other : entries) {

                    if (&entry == &other) { continue; }
                    if (!other.closed()) { continue; }

                    if (other.chain.boundsArea() <= entry.chain.boundsArea()) {
                        continue;
                    }

                    if (pointInChain(p, other.chain)) {
                        containingClosedChains += 1;
                    }
                }

                // Even nesting depth: outer island.
                // Odd nesting depth: hole.
                entry.role = (containingClosedChains % 2) == 0 ? ChainRole::Outer : ChainRole::Hole;
            }
        }

        // Inspect
        //--------------------------------------------------

        Pos min() const {

            if (empty()) { return Pos::Invalid(); }

            Pos out = entries.front().chain.min();

            for (const Entry& entry : entries) {
                out = Pos::min(out, entry.chain.min());
            }

            return out;
        }

        Pos max() const {

            if (empty()) { return Pos::Invalid(); }

            Pos out = entries.front().chain.max();

            for (const Entry& entry : entries) {
                out = Pos::max(out, entry.chain.max());
            }

            return out;
        }

        size_t outerCount() const {

            size_t count = 0;

            for (const Entry& entry : entries) {
                if (entry.outer()) { count += 1; }
            }

            return count;
        }

        size_t holeCount() const {

            size_t count = 0;

            for (const Entry& entry : entries) {
                if (entry.hole()) { count += 1; }
            }

            return count;
        }

        size_t openCount() const {

            size_t count = 0;

            for (const Entry& entry : entries) {
                if (entry.open()) { count += 1; }
            }

            return count;
        }

        // Point containment
        //--------------------------------------------------

        static bool pointInChain(const Pos& p, const Chain& chain) {
            if (!chain.closed()) { return false; }

            int crossings = 0;

            for (const Segment& segment : chain.segments) {

                std::vector<Segment::YHit> hits;

                segment.hitsAtY(p.y, hits);

                for (const Segment::YHit& hit : hits) {

                    if (hit.point.x > p.x) {
                        crossings += 1;
                    }
                }
            }

            return (crossings % 2) == 1;
        }

        bool contains(const Pos& p) const {
            bool inside = false;

            for (const Entry& entry : entries) {

                if (!entry.closed()) { continue; }

                if (!pointInChain(p, entry.chain)) {
                    continue;
                }

                if (entry.outer()) {
                    inside = true;
                }

                else if (entry.hole()) {
                    inside = false;
                }

                else {
                    inside = !inside;
                }
            }

            return inside;
        }

        // Offsetting
        //--------------------------------------------------

        Chain offsetTowardMaterial(const Entry& entry, float amount) const {
            if (entry.role == ChainRole::Outer) {
                return entry.chain.inset(amount);
            }

            if (entry.role == ChainRole::Hole) {
                return entry.chain.outset(amount);
            }

            return entry.chain;
        }

        Chain offsetAwayFromMaterial(const Entry& entry, float amount) const {
            if (entry.role == ChainRole::Outer) {
                return entry.chain.outset(amount);
            }

            if (entry.role == ChainRole::Hole) {
                return entry.chain.inset(amount);
            }

            return entry.chain;
        }

        // Split an offset chain into simple sub-loops and add the ones that are
        // genuine regions: a lobe whose signed area has the OPPOSITE sign to its
        // source is an inverted pinch (negative volume) and is dropped, as is a
        // ~zero-area scrap.  This is the "fractalization" — a tangled offset
        // bubbles off into independent children, each carried forward.
        void addOffsetLoops(Profile& out, const Entry& entry, const Chain& offset, float eps = 1e-4f) const {

            const int sourceSign = entry.chain.windingSign();

            for (const Chain& loop : offset.splitSimpleLoops(eps)) {

                // No segment-count gate: a valid loop may be just two arcs
                // (a circle).  Area alone decides degeneracy.
                const float area = loop.signedArea();

                if (std::abs(area) <= eps) { continue; }                 // degenerate scrap

                const int sign = area > 0.0f ? 1 : -1;

                if (sourceSign != 0 && sign != sourceSign) { continue; }  // inverted pinch lobe

                out.push(loop, entry.role);
            }
        }

        Profile inset(float amount) const {
            Profile out;

            for (const Entry& entry : entries) {

                if (entry.open()) {
                    out.push(entry.chain, entry.role);

                    continue;
                }

                addOffsetLoops(out, entry, offsetTowardMaterial(entry, amount));
            }

            return out;
        }

        Profile outset(float amount) const {
            Profile out;

            for (const Entry& entry : entries) {

                if (entry.open()) {
                    out.push(entry.chain, entry.role);

                    continue;
                }

                addOffsetLoops(out, entry, offsetAwayFromMaterial(entry, amount));
            }

            return out;
        }

        // Directional boundary offset
        //--------------------------------------------------

        // True when a source edge lies *along* the keep-out section, not merely
        // touching it at a corner: we sample the segment and require a majority
        // of those samples to fall within `eps` of some keep-out segment.  A
        // shared endpoint alone (one near sample) is not coincidence.
        static bool segmentCoincident(
            const Segment& s,
            const std::vector<Segment>& keepOut,
            float eps,
            int samples = 8
        ) {
            if (keepOut.empty()) { return false; }

            int near = 0;

            for (int i = 0; i <= samples; i++) {

                Pos p = s.pointAt(float(i) / float(samples));

                float best = 1e30f;

                for (const Segment& o : keepOut) {
                    best = std::min(best, o.distanceTo(p));
                }

                if (best <= eps) { near += 1; }
            }

            return near * 2 >= (samples + 1);
        }

        // For an Outer ring the part's material is on the interior side, so
        // moving "toward material" is an inward (interior) offset.  For a Hole
        // the material wraps the outside, so toward material is exterior.
        // (Matches offsetTowardMaterial / offsetAwayFromMaterial.)
        static bool materialIsInterior(ChainRole role) {
            return role != ChainRole::Hole;
        }

        // Build the initial boundary profile.  Every edge is pushed by `amount`
        // (the tool radius), but the direction is decided per segment: an edge
        // that is coincident with / near the negative keep-out section is pushed
        // *toward* material (so the tool stays clear of the don't-touch model),
        // while an edge facing free space is pushed *away* from material (so the
        // tool fully clears it).  `keepOut` is the negative model's section at
        // this slice; when it is empty every edge faces free space and this
        // degenerates to a uniform outset.
        Profile boundaryOffset(
            float amount,
            const std::vector<Segment>& keepOut,
            float nearEps = 1e-3f
        ) const {

            Profile out;

            for (const Entry& entry : entries) {

                if (entry.open() || entry.role == ChainRole::Unknown) {
                    out.push(entry.chain, entry.role);

                    continue;
                }

                const bool materialInterior = materialIsInterior(entry.role);

                // insetPerSegment takes a signed amount per segment: positive
                // moves toward the chain interior, negative toward the exterior.
                std::vector<float> amounts(entry.chain.segments.size());

                for (size_t i = 0; i < entry.chain.segments.size(); i++) {

                    const bool nearKeepOut = segmentCoincident(
                        entry.chain.segments[i],
                        keepOut,
                        nearEps
                    );

                    // Near keep-out -> toward material; free space -> away.
                    const bool towardInterior = nearKeepOut
                        ? materialInterior
                        : !materialInterior;

                    amounts[i] = towardInterior ? amount : -amount;
                }

                addOffsetLoops(out, entry, entry.chain.insetPerSegment(amounts));
            }

            return out;
        }

        // Degeneracy
        //--------------------------------------------------

        // Sum of signed areas of all closed chains.  An inward offset that
        // flips a region inside-out shows up as a sign change here.
        float signedAreaSum() const {

            float sum = 0.0f;

            for (const Entry& entry : entries) {
                if (!entry.closed()) { continue; }
                sum += entry.chain.signedArea();
            }

            return sum;
        }

        bool hasDegenerateChain(float eps = 1e-4f) const {

            for (const Entry& entry : entries) {

                if (entry.chain.degenerate(eps)) { return true; }
            }

            return false;
        }

        // Any chain (open OR closed) that folds over itself.  Such a ring is an
        // offset artifact (a bowtie / spike) and is unfit to cut, but it may be
        // transient — the next inset can recover a clean ring.
        bool hasSelfIntersectingChain(float eps = 1e-4f) const {

            for (const Entry& entry : entries) {
                if (entry.chain.selfIntersects(eps)) { return true; }
            }

            return false;
        }

        // A simple ring has nothing meaningful left when a closed chain has
        // collapsed to ~zero area (or a chain went empty).  Terminal.
        bool hasCollapsedChain(float eps = 1e-4f) const {

            for (const Entry& entry : entries) {

                if (entry.chain.empty()) { return true; }

                if (entry.closed() && std::abs(entry.chain.signedArea()) <= eps) {
                    return true;
                }
            }

            return false;
        }

        // True when the total signed area flipped sign between two offsets
        // (negative-area inversion), ignoring the collapse-to-zero case which
        // hasDegenerateChain already covers.
        static bool signFlipped(float a, float b, float eps = 1e-4f) {

            if (std::abs(a) <= eps || std::abs(b) <= eps) { return false; }

            return (a > 0.0f) != (b > 0.0f);
        }

        // Scanline crossings
        //--------------------------------------------------

        struct XHit {
            float x = 0.0f;
            size_t entry = 0;
            size_t segment = 0;
        };

        void hitsAtY(float y, std::vector<XHit>& out) const {
            out.clear();

            for (size_t i = 0; i < entries.size(); i++) {

                const Entry& entry = entries[i];

                for (size_t j = 0; j < entry.chain.segments.size(); j++) {

                    const Segment& segment = entry.chain.segments[j];

                    std::vector<Segment::YHit> hits;

                    segment.hitsAtY(y, hits);

                    for (const Segment::YHit& hit : hits) {
                        out.push_back({
                            .x = hit.point.x,
                            .entry = i,
                            .segment = j
                        });
                    }
                }
            }

            std::sort(out.begin(), out.end(), [](const XHit& a, const XHit& b) { return a.x < b.x; });

            // Merge near-duplicates caused by shared vertices or sampled curves.
            std::vector<XHit> unique;

            float eps = 1e-4f;

            for (const XHit& hit : out) {

                if (!unique.empty() && std::abs(unique.back().x - hit.x) < eps) { continue; }

                unique.push_back(hit);
            }

            out = unique;
        }

        // Sampling
        //--------------------------------------------------

        void sample(std::vector<Pos>& out, int samplesPerSegment = 8) const {
            out.clear();

            for (const Entry& entry : entries) {

                std::vector<Pos> chainPoints;

                entry.chain.sample(chainPoints, samplesPerSegment);
                out.insert(out.end(), chainPoints.begin(), chainPoints.end());
            }
        }

        void appendSegments(
            std::vector<Segment>& out,
            bool climbMilling = true
        ) const {

            for (const Entry& entry : entries) {

                Chain chain = entry.chain;

                if (!entry.open() && chain.closed()) {

                    if (entry.role == ChainRole::Outer) {
                        if (climbMilling) { chain.forceCounterClockwise(); }
                        else { chain.forceClockwise(); }
                    }
                    else if (entry.role == ChainRole::Hole) {
                        if (climbMilling) { chain.forceClockwise(); }
                        else { chain.forceCounterClockwise(); }
                    }
                }

                for (const Segment& s : chain.segments) {
                    out.push_back(s);
                }
            }
        }

        // Debug
        //--------------------------------------------------

        static const char* RoleName(ChainRole role) {
            switch (role) {

                case ChainRole::Outer: { return "Outer"; }
                case ChainRole::Hole: { return "Hole"; }
                case ChainRole::Open: { return "Open"; }

                default: {
                    return "Unknown";
                }
            }
        }

        void debug(const char* label = "[Profile]") const {
            dbg(
                "%s entries=%zu outer=%zu holes=%zu open=%zu",
                label,
                entries.size(),
                outerCount(),
                holeCount(),
                openCount()
            );

            for (size_t i = 0; i < entries.size(); i++) {

                const Entry& entry = entries[i];

                dbg(
                    "%s entry=%zu role=%s segs=%zu closed=%i area=%.6f winding=%i",
                    label,
                    i,
                    RoleName(entry.role),
                    entry.chain.size(),
                    int(entry.chain.closed()),
                    entry.chain.signedArea(),
                    entry.chain.windingSign()
                );
            }
        }
    };
}