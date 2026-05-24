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

            Entry(
                const Chain& chain,
                ChainRole role = ChainRole::Unknown
            ) {
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

        std::vector<Entry> entries;

        // Create
        //--------------------------------------------------

        Profile() {}

        Profile(
            const std::vector<Chain>& chains
        ) {
            build(chains);
        }

        Profile(
            const std::vector<Segment>& segments,
            float eps = 1e-4f
        ) {
            build(
                Chain::BuildAll(
                    segments,
                    eps
                )
            );
        }

        static Profile From(
            const std::vector<Chain>& chains
        ) {
            return Profile(chains);
        }

        static Profile FromSegments(
            const std::vector<Segment>& segments,
            float eps = 1e-4f
        ) {
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

        void push(
            const Chain& chain,
            ChainRole role = ChainRole::Unknown
        ) {
            entries.push_back({
                chain,
                role
            });
        }

        // Build / classify
        //--------------------------------------------------

        void build(
            const std::vector<Chain>& chains
        ) {
            clear();

            for (const Chain& chain : chains) {

                ChainRole role = classifyRole(
                    chain
                );

                push(
                    chain,
                    role
                );
            }

            classifyContainment();
        }

        ChainRole classifyRole(
            const Chain& chain
        ) const {
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
                entry.role = (
                    (containingClosedChains % 2) == 0
                    ? ChainRole::Outer
                    : ChainRole::Hole
                );
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

        static bool pointInChain(
            const Pos& p,
            const Chain& chain
        ) {
            if (!chain.closed()) { return false; }

            int crossings = 0;

            for (const Segment& segment : chain.segments) {

                std::vector<Segment::YHit> hits;

                segment.hitsAtY(
                    p.y,
                    hits
                );

                for (const Segment::YHit& hit : hits) {

                    if (hit.point.x > p.x) {
                        crossings += 1;
                    }
                }
            }

            return (crossings % 2) == 1;
        }

        bool contains(
            const Pos& p
        ) const {
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

        Chain offsetTowardMaterial(
            const Entry& entry,
            float amount
        ) const {
            if (entry.role == ChainRole::Outer) {
                return entry.chain.inset(amount);
            }

            if (entry.role == ChainRole::Hole) {
                return entry.chain.outset(amount);
            }

            return entry.chain;
        }

        Chain offsetAwayFromMaterial(
            const Entry& entry,
            float amount
        ) const {
            if (entry.role == ChainRole::Outer) {
                return entry.chain.outset(amount);
            }

            if (entry.role == ChainRole::Hole) {
                return entry.chain.inset(amount);
            }

            return entry.chain;
        }

        Profile inset(
            float amount
        ) const {
            Profile out;

            for (const Entry& entry : entries) {

                if (entry.open()) {
                    out.push(
                        entry.chain,
                        entry.role
                    );

                    continue;
                }

                out.push(
                    offsetTowardMaterial(
                        entry,
                        amount
                    ),
                    entry.role
                );
            }

            return out;
        }

        Profile outset(
            float amount
        ) const {
            Profile out;

            for (const Entry& entry : entries) {

                if (entry.open()) {
                    out.push(
                        entry.chain,
                        entry.role
                    );

                    continue;
                }

                out.push(
                    offsetAwayFromMaterial(
                        entry,
                        amount
                    ),
                    entry.role
                );
            }

            return out;
        }

        // Scanline crossings
        //--------------------------------------------------

        struct XHit {
            float x = 0.0f;
            size_t entry = 0;
            size_t segment = 0;
        };

        void hitsAtY(
            float y,
            std::vector<XHit>& out
        ) const {
            out.clear();

            for (size_t i = 0; i < entries.size(); i++) {

                const Entry& entry = entries[i];

                for (size_t j = 0; j < entry.chain.segments.size(); j++) {

                    const Segment& segment = entry.chain.segments[j];

                    std::vector<Segment::YHit> hits;

                    segment.hitsAtY(
                        y,
                        hits
                    );

                    for (const Segment::YHit& hit : hits) {
                        out.push_back({
                            .x = hit.point.x,
                            .entry = i,
                            .segment = j
                        });
                    }
                }
            }

            std::sort(
                out.begin(),
                out.end(),
                [](const XHit& a, const XHit& b) {
                    return a.x < b.x;
                }
            );

            // Merge near-duplicates caused by shared vertices or sampled curves.
            std::vector<XHit> unique;

            float eps = 1e-4f;

            for (const XHit& hit : out) {

                if (
                    !unique.empty() &&
                    std::abs(unique.back().x - hit.x) < eps
                ) {
                    continue;
                }

                unique.push_back(hit);
            }

            out = unique;
        }

        // Sampling
        //--------------------------------------------------

        void sample(
            std::vector<Pos>& out,
            int samplesPerSegment = 8
        ) const {
            out.clear();

            for (const Entry& entry : entries) {

                std::vector<Pos> chainPoints;

                entry.chain.sample(
                    chainPoints,
                    samplesPerSegment
                );

                out.insert(
                    out.end(),
                    chainPoints.begin(),
                    chainPoints.end()
                );
            }
        }

        void appendSegments(
            std::vector<Segment>& out
        ) const {
            for (const Entry& entry : entries) {

                for (const Segment& s : entry.chain.segments) {
                    out.push_back(s);
                }
            }
        }

        // Debug
        //--------------------------------------------------

        static const char* RoleName(
            ChainRole role
        ) {
            switch (role) {

                case ChainRole::Outer: { return "Outer"; }
                case ChainRole::Hole: { return "Hole"; }
                case ChainRole::Open: { return "Open"; }

                default: {
                    return "Unknown";
                }
            }
        }

        void debug(
            const char* label = "[Profile]"
        ) const {
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