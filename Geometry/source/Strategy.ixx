module;

#include <vector>
#include <memory>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <functional>
#include <unordered_set>

export module Geo.Strategy;

export import Geo.Chain;

// =====================================================================
// The 2D SLICE STRATEGY -- toolpathing for one planar slice. Prepared
// chains in, final toolpath out, and nothing else in the universe.
//
// The contract:
//
//   * The CALLER prepares the input. Doctrine orientation (CCW bounds the
//     region to clear, CW bounds keep-islands), open-air marks on free
//     edges -- all established before the strategy sees anything. In the
//     Sketch app the user's drawn directions are that preparation; in the
//     CAM app, orientByNesting + markOpenAir condition the mesh section.
//     The strategy neither knows nor cares which.
//
//   * The STRATEGY returns everything: the generation series (profiles),
//     per-step stats and sanity verdicts (steps), and the final toolpath --
//     whose chain order and travel directions ARE the tool's motion, with
//     direction (order) and milling sense (handedness) already resolved.
//
//   * The caller displays / executes the result BLINDLY. Any further
//     processing it performs is strictly presentational.
// =====================================================================
export namespace Geo {

    enum class StrategyKind { Profile, Hatch };

    struct SliceParams {

        StrategyKind kind = StrategyKind::Profile;

        float toolRadius = 1.0f;      // the offset step
        int maxGenerations = 32;      // recursion cap (outsets grow forever)

        // Post-processing axes (independent):
        bool reverse = false;         // chain ORDER: execute the path backwards
        bool climb = true;            // chain HANDEDNESS: climb keeps the method's
                                      // travel (material on the left); conventional
                                      // reverses every chain
    };

    // One generation's stats and its sanity verdict.
    struct SliceStep {
        size_t profile = 0;           // index into SliceResult::profiles
        size_t chains = 0;
        float area = 0.0f;            // signed area of the whole system at this step
        float cleared = 0.0f;         // |area| removed since the previous step
        float length = 0.0f;          // total cutting length
        bool included = true;         // negative (unbounded-claim) systems are skipped
    };

    struct SliceResult {

        // The generation series: profiles[0] is the prepared seed (after the
        // strategy's own preparation, e.g. the open-air push); profiles[n+1] is
        // profiles[n] offset by one step.
        std::vector<Profile> profiles;

        // One step per generated profile (1..n).
        std::vector<SliceStep> steps;

        // The final toolpath: flattened, ordered, ACTUAL tool motion.
        std::vector<Chain> toolpath;

        SliceResult() = default;
        SliceResult(SliceResult&&) = default;
        SliceResult& operator=(SliceResult&&) = default;

        void clear() { profiles.clear(); steps.clear(); toolpath.clear(); }
    };

    // The strategy MANAGER: configure it (params), feed it (ingest), run it.
    // It stores the ingested seed and the full result, and run() also RETURNS
    // the result -- by reference into the manager's own storage, so the caller
    // may read it in place or std::move the pieces out.
    struct SliceStrategy {

        // Configuration -- set before run().
        SliceParams params;

        // Ingested input: the prepared seed (the caller's half of the contract).
        Profile seed;

        // Stored output: everything run() produced.
        SliceResult result;

        SliceStrategy() = default;
        explicit SliceStrategy(SliceParams p) : params(p) {}

        // Feed the prepared chains. The seed is consumed by run() (the strategy
        // applies its own preparation to it, e.g. the open-air push).
        void ingest(Profile prepared) {
            seed = std::move(prepared);
        }

        // Run the whole pipeline: prepare -> generations -> steps -> toolpath.
        // The result is stored on the manager AND returned.
        SliceResult& run() {

            result.clear();

            // The strategy's own preparation: open-air edges pre-pushed into the
            // free region (full corner coverage; 1.333R sits clear of the
            // exact-tangency degeneracy). Toolpathy logic -- it belongs here, not
            // in the method.
            if (params.kind == StrategyKind::Profile) {
                for (Chain& c : seed.chains) {
                    c = c.withOpenAirPushed(params.toolRadius * 1.333f);
                }
            }

            result.profiles.push_back(std::move(seed));
            seed = Profile();

            if (params.kind == StrategyKind::Hatch) { hatchGenerations(); }
            else                                    { profileGenerations(); }

            buildSteps();
            buildToolpath();

            return result;
        }

        // Stages
        //--------------------------------------------------

        // The PROFILE strategy's generations: recursively offset until extinction
        // (or the cap). Open chains ride along for one generation but do not seed
        // further ones (an open chain's offset never terminates).
        void profileGenerations() {

            if (std::fabs(params.toolRadius) <= 1e-6f) { return; }

            const size_t cap = static_cast<size_t>(std::max(1, params.maxGenerations));

            while (result.profiles.size() <= cap) {
                Profile next;
                {
                    Profile s;
                    if (result.profiles.size() == 1) { s = result.profiles.back().clone(); }
                    else {
                        for (const Chain& c : result.profiles.back().chains) {
                            if (c.closed) { s.chains.push_back(c.clone()); }
                        }
                    }
                    if (s.empty()) { break; }
                    next = s.offsetBy(params.toolRadius);
                }
                if (next.empty()) { break; }
                result.profiles.push_back(std::move(next));
            }
        }

        // The HATCH strategy: placeholder -- one boundary generation, so the
        // plumbing can be exercised end to end until the real hatch lands.
        void hatchGenerations() {

            if (std::fabs(params.toolRadius) <= 1e-6f) { return; }

            Profile boundary = result.profiles.front().offsetBy(params.toolRadius);
            if (!boundary.empty()) { result.profiles.push_back(std::move(boundary)); }
        }

        // Per-generation stats and the FINAL SANITY RULE: a step whose total
        // signed area is negative claims an unbounded region (an outside grown
        // past an inside without touching -- or a lone outer trace); it is
        // recorded, measured, and skipped by the toolpath.
        void buildSteps() {

            result.steps.clear();
            if (result.profiles.empty()) { return; }

            float prevArea = 0.0f;
            for (const Chain& c : result.profiles.front().chains) { prevArea += c.signedArea(); }

            for (size_t g = 1; g < result.profiles.size(); g++) {
                SliceStep s;
                s.profile = g;
                s.chains = result.profiles[g].chains.size();
                for (const Chain& c : result.profiles[g].chains) {
                    s.area += c.signedArea();
                    for (const auto& e : c.edges) { s.length += Chain::edgeLength(*e); }
                }
                s.cleared = std::fabs(prevArea) - std::fabs(s.area);
                s.included = s.area >= 0.0f;
                prevArea = s.area;
                result.steps.push_back(s);
            }
        }

        // Flatten the ancestral forest DEPTH-FIRST: once a chain splits, one
        // child lineage is followed to extinction before backing up -- just far
        // enough to the nearest unaccounted sibling. Skipped generations are
        // traversed but not emitted; an orphan sweep appends anything whose
        // lineage broke.
        std::vector<const Chain*> flattenDepthFirst() const {

            std::vector<const Chain*> out;
            if (result.profiles.size() < 2) { return out; }

            auto included = [&](size_t g) {
                return (g - 1 < result.steps.size()) ? result.steps[g - 1].included : true;
            };

            std::unordered_set<const Chain*> visited;

            std::function<void(const Chain&, size_t)> visit = [&](const Chain& c, size_t g) {
                visited.insert(&c);
                if (included(g)) { out.push_back(&c); }
                if (g + 1 >= result.profiles.size()) { return; }
                for (const Chain& child : result.profiles[g + 1].chains) {
                    if (child.parent == c.id) { visit(child, g + 1); }
                }
            };

            for (const Chain& c : result.profiles[1].chains) { visit(c, 1); }

            for (size_t g = 1; g < result.profiles.size(); g++) {
                if (!included(g)) { continue; }
                for (const Chain& c : result.profiles[g].chains) {
                    if (!visited.count(&c)) { out.push_back(&c); }
                }
            }

            return out;
        }

        // The final toolpath: pure post-processing on clones. Direction = chain
        // ORDER only; milling = chain HANDEDNESS only. From here on, chain travel
        // IS tool motion.
        void buildToolpath() {

            result.toolpath.clear();
            for (const Chain* c : flattenDepthFirst()) { result.toolpath.push_back(c->clone()); }

            if (params.reverse) {
                std::reverse(result.toolpath.begin(), result.toolpath.end());
            }

            if (!params.climb) {
                for (Chain& c : result.toolpath) { c = c.reversed(); }
            }
        }
    };

    // Convenience: one-shot form for callers with nothing to hold onto.
    inline SliceResult runSliceStrategy(Profile seed, const SliceParams& params) {
        SliceStrategy strategy(params);
        strategy.ingest(std::move(seed));
        strategy.run();
        return std::move(strategy.result);
    }
}
