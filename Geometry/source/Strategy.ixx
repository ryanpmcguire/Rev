module;

#include <vector>
#include <memory>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <functional>
#include <optional>
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

        float toolRadius = 1.0f;      // the boundary clearance: generation 1 offsets by exactly this
        float stepover = 1.0f;        // generation advance as a FRACTION of the tool radius
                                      // (gen 2+ offset by toolRadius * stepover; <= 2.0 for full
                                      // coverage, typically 0.5 .. 1.0)
        int maxGenerations = 32;      // recursion cap (outsets grow forever)

        // Post-processing axes (independent):
        bool reverse = false;         // chain ORDER: execute the path backwards
        bool climb = true;            // chain HANDEDNESS: climb keeps the method's
                                      // travel (material on the left); conventional
                                      // reverses every chain

        // Lead-in / lead-out: engagement moves woven around RETRACT steps so the
        // tool eases off the cut before retracting and eases back on after arriving,
        // never landing cold on the cut path. Each lead follows a small INSET of the
        // target itself (the same offset method the generations run on), preserving
        // the target's own motion as it approaches; the consumer also reads a lead as
        // a DESCENT, so the inset loop doubles as a helix ramp.
        bool  lead = false;           // weave lead-in/out chains around retract steps
        float leadInset = 0.25f;      // the "safe offset" inset, as a FRACTION of the
                                      // tool radius (the lead rides this far inside the cut)

        // How LONG the lead runs along the inset. The lead is the horizontal run of
        // the ramp the consumer will descend: to drop `cuttingDepth` at `plungeSlope`
        // degrees off horizontal, the run is cuttingDepth / tan(slope). That arc
        // length of inset is what we emit; the CAM app extrudes it into the 3D slope.
        float cuttingDepth = 4.0f;    // depth the lead ramp descends (model units)
        float plungeSlope = 23.0f;    // ramp angle off horizontal (deg); shallower = longer
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

            // CLOSED CHAINS ONLY: offsetting has no concept of an open chain. Open
            // chains in the seed are dropped at the door (the only open chains in
            // a RESULT are the link segments the strategy itself weaves).
            std::erase_if(seed.chains, [](const Chain& c) { return !c.closed; });

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
        // (or the cap). Generation 1 offsets by EXACTLY one tool radius -- the
        // boundary clearance pass -- and every generation after by
        // toolRadius * stepover, the ring advance.
        void profileGenerations() {

            if (std::fabs(params.toolRadius) <= 1e-6f) { return; }

            const size_t cap = static_cast<size_t>(std::max(1, params.maxGenerations));
            const float step = params.toolRadius * std::max(0.05f, params.stepover);

            while (result.profiles.size() <= cap) {
                const float amount = (result.profiles.size() == 1) ? params.toolRadius : step;
                Profile next = result.profiles.back().offsetBy(amount);
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
        // ORDER only; milling = chain HANDEDNESS only. Then the LINKING pass
        // weaves tagged connector chains between consecutive cutting chains.
        //
        // Whether the tool may stay down for a hop is MEASURED, never assumed
        // (ancestry proved nothing: a parent's other children can lie anywhere).
        // A cut link requires BOTH:
        //
        //   * REACH -- a circle of one tool radius at the tool's position must
        //     intersect the next chain: the hop distance to the nearest entry
        //     is at most ONE TOOL RADIUS. That is the whole budget.
        //
        //   * CLEARANCE -- the straight (x, y) hop must not cross ANY of the
        //     source geometry (the prepared seed). Crossing the source means
        //     cutting through a wall; that hop must retract instead.
        //
        // Cut link: the next loop is re-seated to begin at its point NEAREST
        // the tool, keeping the flow of motion. Retract link: with the start
        // free, the next loop is re-seated to the middle of its longest edge
        // (the most stable entry).
        //
        // Links carry no Z -- they are planar segments with a TAG; what retracting
        // means in 3D is the consumer's business. From here on, chain travel IS
        // tool motion.
        void buildToolpath() {

            result.toolpath.clear();

            std::vector<Chain> run;
            for (const Chain* c : flattenDepthFirst()) { run.push_back(c->clone()); }

            if (params.reverse) {
                std::reverse(run.begin(), run.end());
            }

            if (!params.climb) {
                for (Chain& c : run) { c = c.reversed(); }
            }

            // Does the straight hop a -> b cross any source chain geometry?
            auto crossesSource = [&](Pos a, Pos b) -> bool {

                if ((b - a).pythag() <= 1e-4f) { return false; }
                if (result.profiles.empty()) { return false; }

                Segment2 hop(a, b);

                for (const Chain& c : result.profiles.front().chains) {
                    for (const auto& e : c.edges) {
                        std::vector<Pos> hits;
                        Chain::edgeCross(hop, *e, hits);
                        if (!hits.empty()) { return true; }
                    }
                }

                return false;
            };

            // The "safe offset" inset of a single chain: feed it through the very
            // same offset method the generations run on, so the inset PRESERVES the
            // target's topology (a circle insets to a smaller circle). Returns the
            // inset chain nearest `near`, with the foot point and its edge. Empty when
            // the target is too small to inset at all (it collapses) -- the caller
            // then falls back to the target itself.
            auto insetNear = [&](const Chain& target, Pos near,
                                 Pos& insPt, size_t& insK) -> std::optional<Chain> {
                const float safe = params.toolRadius * std::max(0.02f, params.leadInset);
                Profile p; p.chains.push_back(target.clone());
                Profile off = p.offsetBy(safe);
                const Chain* best = nullptr; float bestD = 1e30f; Pos bestPt; size_t bestK = 0;
                for (const Chain& c : off.chains) {
                    if (!c.closed || c.edges.empty()) { continue; }
                    Pos pt; size_t k = c.nearestPoint(near, pt);
                    float d = (pt - near).pythag();
                    if (d < bestD) { bestD = d; best = &c; bestPt = pt; bestK = k; }
                }
                if (!best) { return {}; }
                insPt = bestPt; insK = bestK;
                return best->clone();
            };

            // A sub-portion of a CLOSED loop by ARC LENGTH, measured along travel:
            // head=true returns the first `len` from the loop's seam (its start);
            // head=false returns the last `len`, ending at the seam. `len` past the
            // perimeter spirals AROUND: the lead wraps the inset as many full times
            // as the run demands (the small-island helix), plus a partial wrap for the
            // remainder. This is how much of the inset topology the lead actually
            // covers.
            auto portion = [&](const Chain& seated, float len, bool head)
                                 -> std::vector<std::unique_ptr<Stoicheion>> {
                std::vector<std::unique_ptr<Stoicheion>> out;
                if (seated.edges.empty() || len <= 1e-4f) { return out; }

                float total = 0.0f;
                for (const auto& e : seated.edges) { total += Chain::edgeLength(*e); }
                if (total <= 1e-4f) { return out; }

                // Split the run into whole laps + a leftover partial lap (capped, so
                // pathological depth/slope can't spawn unbounded geometry).
                const int maxLaps = 256;
                int laps = std::min(maxLaps, static_cast<int>(len / total));
                const float rem = len - static_cast<float>(laps) * total;

                auto wholeLap = [&]() {
                    for (const auto& e : seated.edges) { out.push_back(e->clone()); }
                };

                // One sub-perimeter run: the first `plen` (head) or the last `plen`
                // (tail) of a single seam-seated lap.
                auto partial = [&](float plen, bool h) {
                    if (plen <= 1e-4f) { return; }
                    const float skip = h ? 0.0f : (total - plen);
                    float acc = 0.0f;
                    for (const auto& e : seated.edges) {
                        const float el = Chain::edgeLength(*e);
                        const float endAcc = acc + el;
                        if (h) {
                            if (endAcc <= plen + 1e-4f) { out.push_back(e->clone()); }
                            else {
                                std::unique_ptr<Stoicheion> a, b;
                                Chain::splitEdge(*e, Chain::edgePointAt(*e, (plen - acc) / el), a, b);
                                out.push_back(std::move(a));
                                break;
                            }
                        }
                        else {
                            if (endAcc <= skip + 1e-4f) { /* before the kept region */ }
                            else if (acc >= skip - 1e-4f) { out.push_back(e->clone()); }
                            else {
                                std::unique_ptr<Stoicheion> a, b;
                                Chain::splitEdge(*e, Chain::edgePointAt(*e, (skip - acc) / el), a, b);
                                out.push_back(std::move(b));
                            }
                        }
                        acc = endAcc;
                    }
                };

                // Head leads OUT of the seam: whole laps then the partial. Tail leads
                // INTO the seam: the partial first, then the whole laps -- every lap is
                // seam-to-seam, so the joins are continuous either way.
                if (head) {
                    for (int i = 0; i < laps; i++) { wholeLap(); }
                    partial(rem, true);
                }
                else {
                    partial(rem, false);
                    for (int i = 0; i < laps; i++) { wholeLap(); }
                }
                return out;
            };

            // The lead-IN: come down onto the target's inset (or, if the inset
            // collapsed, the target itself -- a valid in-place helix), follow `run`
            // of that topology, then merge out onto the actual cut entry. The covered
            // run is the consumer's descent ramp. Returns the open Lead chain;
            // `startOut` is where the rapid should deliver the tool.
            auto buildLeadIn = [&](const Chain& target, Pos entry, float run, Pos& startOut) -> Chain {
                Chain lead; lead.closed = false; lead.id = newId(); lead.link = LinkKind::Lead;
                Pos insPt; size_t insK;
                std::optional<Chain> ins = insetNear(target, entry, insPt, insK);
                if (ins) {
                    Chain seated = ins->startedAt(insK, insPt);    // inset loop from insPt
                    auto tail = portion(seated, run, /*head=*/false);   // the run ENDING at insPt
                    for (auto& e : tail) { lead.edges.push_back(std::move(e)); }
                    if ((entry - insPt).pythag() > 1e-4f) {        // merge out onto the cut
                        lead.edges.push_back(std::make_unique<Segment2>(insPt, entry));
                    }
                    startOut = lead.edges.empty() ? insPt : Chain::eStart(*lead.edges.front());
                }
                else {
                    Chain copy = target.clone();                   // descend the cut itself
                    for (const auto& e : copy.edges) { lead.edges.push_back(e->clone()); }
                    startOut = entry;
                }
                return lead;
            };

            // The lead-OUT: the mirror -- merge off the cut at the exit onto the
            // inset, follow `run` of it, and leave the tool there so the retract lifts
            // AWAY from the cut surface, never up it. `endOut` is where the retract
            // begins.
            auto buildLeadOut = [&](const Chain& source, Pos exitPt, float run, Pos& endOut) -> Chain {
                Chain lead; lead.closed = false; lead.id = newId(); lead.link = LinkKind::Lead;
                Pos insPt; size_t insK;
                std::optional<Chain> ins = insetNear(source, exitPt, insPt, insK);
                if (ins) {
                    if ((insPt - exitPt).pythag() > 1e-4f) {       // merge off the cut
                        lead.edges.push_back(std::make_unique<Segment2>(exitPt, insPt));
                    }
                    Chain seated = ins->startedAt(insK, insPt);
                    auto headRun = portion(seated, run, /*head=*/true);   // the run STARTING at insPt
                    for (auto& e : headRun) { lead.edges.push_back(std::move(e)); }
                    endOut = lead.edges.empty() ? exitPt : Chain::eEnd(*lead.edges.back());
                }
                else {
                    Chain copy = source.clone();
                    for (const auto& e : copy.edges) { lead.edges.push_back(e->clone()); }
                    endOut = exitPt;
                }
                return lead;
            };

            // How far each lead runs along the inset: the horizontal run of a ramp
            // that descends `cuttingDepth` at `plungeSlope` off horizontal.
            const float slopeRad =
                std::clamp(params.plungeSlope, 1.0f, 89.0f) * 3.14159265358979f / 180.0f;
            const float leadRun =
                (params.cuttingDepth > 0.0f) ? params.cuttingDepth / std::tan(slopeRad) : 0.0f;

            Pos cursor;
            bool haveCursor = false;
            std::optional<Chain> prevCut;   // the cut just emitted, for its lead-out

            for (Chain& next : run) {

                if (next.edges.empty()) { continue; }

                // Decide the link kind by measurement, and re-seat the entry.
                bool cutLink = false;

                if (next.closed) {

                    if (haveCursor) {

                        Pos entry;
                        size_t k = next.nearestPoint(cursor, entry);

                        const bool reachable =
                            (entry - cursor).pythag() <= params.toolRadius + 1e-4f;

                        if (reachable && !crossesSource(cursor, entry)) {
                            cutLink = true;
                            next = next.startedAt(k, entry);
                        }
                    }

                    if (!cutLink) {
                        size_t k = next.longestEdgeIndex();
                        next = next.startedAt(k, Chain::edgeMidpoint(*next.edges[k]));
                    }
                }

                // The cut begins at `entry`. Leads are woven ONLY at retract steps (a
                // cut-link already rides down into the next ring -- it needs none).
                // Such a step is ALWAYS paired: a lead-OUT eases off the previous cut
                // before the retract lifts, and a lead-IN eases onto this one after it
                // lands -- so the tool moves AWAY from a chain before retracting and
                // TOWARD a chain after arriving, never up or down the cut wall.
                Pos entry = Chain::eStart(*next.edges.front());
                Pos linkTarget = entry;
                const bool doLead = params.lead && next.closed && !cutLink;

                // 1. Lead-OUT of the previous cut, easing the tool off it before the
                //    retract. This advances the cursor to the lift point.
                if (doLead && haveCursor && prevCut) {
                    Pos outEnd;
                    Chain lo = buildLeadOut(*prevCut, cursor, leadRun, outEnd);
                    if (!lo.edges.empty()) {
                        result.toolpath.push_back(std::move(lo));
                        cursor = outEnd;
                    }
                }

                // 2. Lead-IN to the target, built first so the link can deliver the
                //    tool to where the lead-in begins (its inset, not the cut).
                std::optional<Chain> leadIn;
                if (doLead) {
                    Pos inStart;
                    Chain li = buildLeadIn(next, entry, leadRun, inStart);
                    if (!li.edges.empty()) { linkTarget = inStart; leadIn = std::move(li); }
                }

                // 3. The link (rapid/retract, or stay-down cut) to the link target.
                if (haveCursor && (linkTarget - cursor).pythag() > 1e-4f) {
                    Chain link;
                    link.closed = false;
                    link.id = newId();
                    link.link = cutLink ? LinkKind::Cut : LinkKind::Retract;
                    link.edges.push_back(std::make_unique<Segment2>(cursor, linkTarget));
                    result.toolpath.push_back(std::move(link));
                }

                // 4. The lead-in, then the cut itself.
                if (leadIn) {
                    result.toolpath.push_back(std::move(*leadIn));
                }

                cursor = Chain::eEnd(*next.edges.back());
                haveCursor = true;
                prevCut = next.clone();

                result.toolpath.push_back(std::move(next));
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
