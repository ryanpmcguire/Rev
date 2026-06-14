module;

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <dbg.hpp>

export module Sketch.App.Project;

import Geo.Strategy;

export import Sketch.App.Layer;
export import Sketch.App.Geometry;
export import Sketch.App.Chain;

export namespace Sketch::App {

    using Json = nlohmann::json;

    // A sketch workspace.
    //
    // Rather than a single flat file, a project is an IDE-style *workspace*: a
    // `<Name>.sketch` folder holding a tiered set of JSON files.
    //
    //   ProfileTest.sketch/
    //     manifest.json        # project metadata + the layer index
    //     layers/
    //       sketch.json        # a Geometry layer (profiles / chains / segments)
    //       fill.json          # a Fill layer (lawnmower / hatch output, ...)
    //     scripts/             # (future) user python that lives in the sketch
    //
    // The manifest is the "project per se": identity, format version, and the
    // index of layers it owns. Each layer round-trips to its own file so the
    // pieces can be inspected, diffed, and regenerated independently.
    struct Project {

        // On-disk constants
        //--------------------------------------------------

        static constexpr const char* Extension     = ".sketch";
        static constexpr const char* ManifestName   = "manifest.json";
        static constexpr const char* LayersDirName  = "layers";
        static constexpr int FormatVersion          = 1;

        // Identity
        //--------------------------------------------------

        std::string name = "Untitled Sketch";

        // Absolute path to the `<Name>.sketch` workspace folder. Empty until the
        // workspace has been written to disk for the first time.
        std::string path;

        bool dirty = false;

        // Contents
        //--------------------------------------------------

        std::vector<Layer> layers;

        // The sketch geometry. Source of truth in memory; serialized into the
        // geometry layer's JSON on save and re-read on load.
        SketchGeometry geometry;

        // Generated geometry layers drawn *alongside* the sketch -- the offset/inset
        // produced by "i". In-memory seed of the "one sketch, many layers" idea.
        // Each layer's stoicheia partition into named groups by their own `group`
        // tag (the offset loops, the "intersections" dots, ...).
        std::vector<SketchGeometry> offsetLayers;

        // The profile series: profiles[0] is the chains as drawn; profiles[n+1] is
        // profiles[n] offset by one step -- the whole method behind one call
        // (Profile::offsetBy). Regenerated on each rebuild while the inset is live.
        std::vector<Profile> profiles;

        // Debug artifacts mirroring the FIRST offset step (the debug views inspect
        // one step's anatomy): the raw blind offsets and their source chains,
        // index-aligned, plus the offset distance.
        std::vector<Chain> offsetChains;
        std::vector<Chain> sourceChains;
        float lastOffsetAmount = 0.0f;

        // The crossing method's fragments: the blind offset split at every crossing
        // and regrouped into sub-chains of contiguous same-crossing-number pieces.
        // The number IS the category (anchored absolute depth: 0 = borders the
        // unbounded outside); the final step simply keeps the zero fragments.
        std::vector<Chain::NumberedChain> crossingFragments;

        // Which offset views to emit (mirrored from the app's view options by the
        // sketch view before each rebuild).
        bool viewValid = true;        // production: the final valid chains
        bool viewWinding = false;     // debug: level colours + magenta + marks
        bool viewDiscarded = false;   // debug: include non-minimum fragments
        bool viewToolpath = true;     // toolpath rules applied over the method's output
        bool reverseToolpath = false; // chain ORDER reversed (execution sequence only)
        bool climbMilling = true;     // chain HANDEDNESS: climb keeps the method's travel, conventional flips it
        bool leadIn = false;          // weave lead-in/out chains around retract steps
        float leadInset = 0.25f;      // the lead's "safe offset" inset, fraction of tool radius
        float cuttingDepth = 4.0f;    // depth the lead ramp descends (model units)
        float plungeSlope = 23.0f;    // lead-in ramp angle off horizontal (deg); shallower = longer
        float retractSlope = 75.0f;   // lead-out ramp angle (deg); steep -- it climbs, not plunges
        float stepover = 1.0f;        // ring advance as a fraction of the tool radius
        int  iterations = 32;         // max offset generations per strategy run

        // The slice strategy's outputs, stored verbatim. The strategy (Geo.Strategy
        // -- the same module CAM will consume, with zero modification) returns the
        // generation series, the step stats/verdicts, and the final toolpath; the
        // app keeps them only to DISPLAY them.
        using ToolpathStep = Geo::SliceStep;
        std::vector<ToolpathStep> toolpathSteps;
        std::vector<Chain> toolpath;

        // Create
        //--------------------------------------------------

        Project() = default;

        explicit Project(std::string name) : name(std::move(name)) {
            // A fresh workspace starts with one empty geometry layer.
            layers.emplace_back("sketch", LayerKind::Geometry);
            ensureDatums();
        }

        // Layers
        //--------------------------------------------------

        Layer* findLayer(const std::string& layerName) {
            for (Layer& layer : layers) {
                if (layer.name == layerName) { return &layer; }
            }
            return nullptr;
        }

        Layer& addLayer(const std::string& layerName, LayerKind kind) {

            if (Layer* existing = findLayer(layerName)) {
                existing->kind = kind;
                return *existing;
            }

            layers.emplace_back(layerName, kind);
            dirty = true;
            return layers.back();
        }

        // The layer that holds the sketch geometry (first Geometry-kind layer,
        // falling back to the first layer, creating one if the list is empty).
        Layer* geometryLayer() {
            for (Layer& layer : layers) {
                if (layer.kind == LayerKind::Geometry) { return &layer; }
            }
            if (!layers.empty()) { return &layers.front(); }
            return &addLayer("sketch", LayerKind::Geometry);
        }

        // Geometry
        //--------------------------------------------------

        // Append any entity, returning its index (so a tool can keep extending it,
        // e.g. the line tool growing a polyline in place).
        size_t add(std::unique_ptr<Stoicheion> entity) {
            size_t index = geometry.add(std::move(entity));
            dirty = true;
            return index;
        }

        void replace(size_t index, std::unique_ptr<Stoicheion> entity) {
            if (index >= geometry.entities.size()) { return; }
            geometry.entities[index] = std::move(entity);
            dirty = true;
        }

        size_t addRelation(std::unique_ptr<Relation> relation) {
            size_t index = geometry.addRelation(std::move(relation));
            dirty = true;
            return index;
        }

        void clearGeometry() {
            geometry.clear();
            dirty = true;
        }

        // Offset every chain by `amount`, blindly: each edge slides left of its own
        // travel direction and every corner is bridged by the known join arc. The
        // pure blind offset is PRESERVED in offsetChains (never displayed); what the
        // layer shows is the extraction: the offset split at every self-crossing into
        // loops, each coloured strictly by its own ABSOLUTE chirality -- every CW
        // loop blue, every CCW loop red, never relative to anything -- plus the
        // "intersections" dot group on top.
        // Toolpathing -- the BLIND CALLER's side of the contract. The slice
        // strategy lives in Geo.Strategy (the very module CAM will consume,
        // unmodified); this app's only jobs are (1) hand it PREPARED geometry --
        // here the user's drawn directions and open-air marks ARE the
        // preparation -- and (2) display the result verbatim. The strategy has
        // no idea this app exists.
        //--------------------------------------------------

        void runStrategy(const std::string& strategy, float radius) {
            lastOffsetAmount = radius;

            Geo::SliceParams params;
            params.kind = (strategy == "hatch") ? Geo::StrategyKind::Hatch
                                                : Geo::StrategyKind::Profile;
            params.toolRadius = radius;
            params.stepover = stepover;
            params.maxGenerations = std::max(1, iterations);
            params.reverse = reverseToolpath;
            params.climb = climbMilling;
            params.lead = leadIn;
            params.leadInset = leadInset;
            params.cuttingDepth = cuttingDepth;
            params.plungeSlope = plungeSlope;
            params.retractSlope = retractSlope;

            Geo::SliceResult sliced =
                Geo::runSliceStrategy(Profile::fromEntities(geometry.entities), params);

            // Stored verbatim, displayed blindly.
            profiles = std::move(sliced.profiles);
            toolpathSteps = std::move(sliced.steps);
            toolpath = std::move(sliced.toolpath);

            // Debug artifacts mirror the first step (display-only instruments).
            offsetChains.clear();
            sourceChains.clear();
            if (!profiles.empty()) {
                for (const Chain& chain : profiles.front().chains) {
                    offsetChains.push_back(chain.offsetRaw(radius));
                    sourceChains.push_back(chain.clone());
                }
            }

            // Trace: the total signed area of every generation (a healthy inset
            // shrinks monotonically toward zero; a jump the wrong way fingers the
            // first bad generation).
            for (size_t g = 0; g < profiles.size(); g++) {
                float area = 0.0f;
                for (const Chain& c : profiles[g].chains) { area += c.signedArea(); }
                dbg("[profile %zu] %zu chains, total signed area %.4f\n",
                    g, profiles[g].chains.size(), area);
            }

            // Compose the selected views (debug first, so production fragments own
            // crossingFragments when both are shown). The prepared profile zero
            // draws beneath everything, in yellow.
            SketchGeometry result;
            profileZeroView(result);
            if (viewWinding || viewDiscarded) { crossingMethod(result); addIntersectionMarks(result); }
            if (viewValid) { validMethod(result); }

            result.normalize();
            offsetLayers.clear();
            offsetLayers.push_back(std::move(result));
            dirty = true;
        }

        // DEBUG: the PREPARED profile zero, in yellow -- the drawn chains after
        // open-air preparation (open sides displaced outward, junctions healed
        // with linking segments). This is the exact seed the recursion consumes;
        // if the yellow chain looks wrong, the bug is in preparation, not in the
        // method.
        void profileZeroView(SketchGeometry& result) {
            if (profiles.empty()) { return; }
            for (const Chain& c : profiles.front().chains) {
                for (const auto& e : c.edges) {
                    std::unique_ptr<Stoicheion> copy = e->clone();
                    copy->group = DisplayGroup::Profile0;
                    result.add(std::move(copy));
                }
            }
        }

        // PRODUCTION -- the complete method, in two stages.
        //
        // STAGE A (each source chain in its own universe): Chain::offsetValid does
        // everything per chain -- blind offset, crossing walk, minimum-level
        // extraction, stitching, the handedness law.
        //
        // STAGE B (the universes meet): the validated chains from SEPARATE source
        // profiles are clustered by interaction and mitosed against each other --
        // a shrinking outer meeting a growing island carves and splits; two
        // expanding traces merge into one enclosing trace; disjoint chains and
        // open chains pass untouched (Chain::mitose).
        //
        // The final classification is by exact signed area: positive ->
        // "valid/pos" (red), negative -> "valid/neg" (green).
        void validMethod(SketchGeometry& result) {
            crossingFragments.clear();

            // Every generated profile (1..n) -- the whole recursive series, each
            // chain coloured by its exact signed area. With the toolpath view on,
            // steps the sanity rule skipped render as ghosts instead.
            for (size_t g = 1; g < profiles.size(); g++) {
                bool skipped = viewToolpath
                            && g - 1 < toolpathSteps.size()
                            && !toolpathSteps[g - 1].included;
                for (const Chain& m : profiles[g].chains) {
                    DisplayGroup tag = skipped ? DisplayGroup::ToolpathSkip
                                     : (m.signedArea() >= 0.0f) ? DisplayGroup::ValidPos
                                                                : DisplayGroup::ValidNeg;
                    for (const auto& e : m.edges) {
                        std::unique_ptr<Stoicheion> copy = e->clone();
                        copy->group = tag;
                        result.add(std::move(copy));
                    }
                    crossingFragments.push_back(Chain::NumberedChain{ m.clone(), 0 });
                }
            }
        }

        // DEBUG VIEW (kept, not called in production -- swap it in inside
        // insetIntoNewLayer to inspect the method's anatomy: level colouring,
        // magenta extraction, intersection dots, start markers).
        //
        // The crossing method, in two strictly separated stages.
        //
        // STAGE A (each chain in its own universe): split the blind offset at its
        // OWN self-crossings only -- no other chain exists yet -- walk the sub-edges
        // in original order accumulating the signed crossing number, fragment into
        // same-number sub-chains, and keep the measured level (the fragment holding
        // exactly |amount| of clearance from its source names it). The output is
        // each chain's fully validated offset, computed in isolation.
        //
        // STAGE B (the universes meet): only the validated results are then
        // considered against each other -- clustered by interaction and mitosed,
        // each cluster judged by its own measured orientation. Same-handed traces
        // merge into one enclosing trace; an opposite-handed island carves and can
        // split the pocket. Disjoint chains, and all open chains, pass untouched.
        //
        // The final chains live whole in crossingFragments; the display copy is
        // stamped "crossing/<number>/<fragment>". offsetChains is untouched.
        // DISCARD LOGIC DISCONNECTED. Back to the naive instrument: every fragment
        // of every chain is shown, coloured purely by its RELATIVE accumulated
        // crossing number (0 at the chain's start, differences only). No prune, no
        // mitosis -- the true discard rule (some combination of handedness and
        // crossing number) is being worked out by inspection first.
        void crossingMethod(SketchGeometry& result) {
            crossingFragments.clear();
            for (size_t ci = 0; ci < offsetChains.size(); ci++) {
                const Chain& raw = offsetChains[ci];
                std::vector<Chain::NumberedChain> frags = raw.fragmentByCrossingNumber();
                if (frags.empty()) { continue; }

                // The invariant: the valid path is composed of the fragments at the
                // chain's MINIMUM accumulated crossing number -- with one final
                // check. The candidate's chirality must MATCH the source chain's
                // (both measured, from their own stoicheia): a true offset always
                // inherits its source's sense of travel, so a minimum-level loop
                // travelling OPPOSITE its source is an inverted profile -- the
                // collapse case -- and then there is NO valid chain at all.
                int minLevel = frags.front().number;
                for (const Chain::NumberedChain& nc : frags) { minLevel = std::min(minLevel, nc.number); }

                // 1. EXTRACT first, and STITCH: the minimum-level segments arrive
                //    as several open fragments (the walk's excursions interrupt
                //    them), but they meet end-to-start at the crossing vertices --
                //    at each crossing the min-level pool has exactly one end and
                //    one start, so stitching is unambiguous and never reverses an
                //    edge. The stitched chains ARE the magenta candidates.
                std::vector<std::unique_ptr<Stoicheion>> pool;
                for (const Chain::NumberedChain& nc : frags) {
                    if (nc.number != minLevel) { continue; }
                    for (const auto& e : nc.chain.edges) { pool.push_back(e->clone()); }
                }
                std::vector<Chain> magenta;
                while (!pool.empty()) {
                    Chain m = Chain::buildOne(pool, 1e-3f);
                    if (m.edges.empty()) { break; }
                    magenta.push_back(std::move(m));
                }

                // 2. THEN judge -- and the verdict is about the NUMBER: if any
                //    stitched magenta chain travels OPPOSITE the source path we set
                //    out to offset, the only candidate crossing number turned out
                //    bad, and there are no valid chains at all. (The raw offset
                //    chain as a WHOLE always matches its source, which is why the
                //    test must be applied to the stitched extraction, never to the
                //    whole or to the unstitched fragments.)
                const int srcChir = (ci < sourceChains.size()) ? sourceChains[ci].turningSign() : 0;
                bool numberIsBad = false;
                for (const Chain& m : magenta) {
                    if (!m.closed || srcChir == 0) { continue; }
                    int chir = m.turningSign();
                    if (chir != 0 && chir != srcChir) { numberIsBad = true; break; }
                }

                // 3. emit: the level colouring always; the magenta only if the
                //    number survived its trial.
                for (Chain::NumberedChain& nc : frags) {
                    // Non-minimum (discarded) fragments only when asked for.
                    if (nc.number == minLevel || viewDiscarded) {
                        for (const auto& e : nc.chain.edges) {
                            std::unique_ptr<Stoicheion> copy = e->clone();
                            copy->group = DisplayGroup::Crossing;
                            copy->groupLevel = nc.number;
                            result.add(std::move(copy));
                        }
                    }
                    crossingFragments.push_back(std::move(nc));
                }
                if (!numberIsBad) {
                    for (const Chain& m : magenta) {
                        for (const auto& e : m.edges) {
                            std::unique_ptr<Stoicheion> copy = e->clone();
                            copy->group = DisplayGroup::Minimum;
                            result.add(std::move(copy));
                        }
                    }
                }
            }
        }

        // The intersection marks, staged exactly like the pipeline. Stage A: each
        // raw offset chain's SELF-crossings -- found in its own universe, blind to
        // every other chain (these are the cuts the per-chain crossing numbers
        // resolved). Stage B: the MUTUAL crossings between the validated result
        // chains -- the only inter-chain intersections that mean anything, since
        // mitosis only ever sees validated chains.
        void addIntersectionMarks(SketchGeometry& result) {
            std::vector<Pos> crossings;
            for (const Chain& c : offsetChains) {
                for (const Pos& p : c.allSelfIntersections()) { crossings.push_back(p); }
            }
            for (size_t i = 0; i < crossingFragments.size(); i++) {
                for (size_t j = i + 1; j < crossingFragments.size(); j++) {
                    for (const auto& ea : crossingFragments[i].chain.edges) {
                        for (const auto& eb : crossingFragments[j].chain.edges) {
                            Chain::edgeCross(*ea, *eb, crossings);
                        }
                    }
                }
            }
            for (const Pos& p : crossings) {
                std::unique_ptr<Point2> dot = std::make_unique<Point2>(p);
                dot->group = DisplayGroup::Intersections;
                result.add(std::move(dot));
            }

            // The walk's starting point of every offset chain -- where the relative
            // count begins at 0 -- so the start-dependence of the colouring is
            // visible at a glance.
            for (const Chain& c : offsetChains) {
                if (c.edges.empty()) { continue; }
                std::unique_ptr<Point2> dot = std::make_unique<Point2>(Chain::eStart(*c.edges.front()));
                dot->group = DisplayGroup::Start;
                result.add(std::move(dot));
            }
        }

        // Ensure the datum geometry exists: the locked origin point and the two
        // world axes. They are construction + locked (can't move or delete) and act
        // as the root authority everything else can be related to.
        void ensureDatums() {

            for (const auto& e : geometry.entities) { if (e && e->locked) { return; } }

            constexpr float AX = 1.0e5f;   // axes as long, locked construction segments

            auto datum = [&](std::unique_ptr<Stoicheion> e) {
                e->construction = true;
                e->locked = true;
                return geometry.add(std::move(e));
            };

            size_t originIndex = datum(std::make_unique<Point2>(Pos(0.0f, 0.0f)));
            size_t xIndex = datum(std::make_unique<Segment2>(Pos(-AX, 0.0f), Pos(AX, 0.0f)));   // X axis
            size_t yIndex = datum(std::make_unique<Segment2>(Pos(0.0f, -AX), Pos(0.0f, AX)));   // Y axis

            // Nothing is locked by fiat -- the origin and both axis endpoints are each
            // pinned to their mathematical position by an explicit Lock relation. The
            // axes thus *represent* the world directions in the relation graph: any
            // Parallel relation reads their (locked) endpoints to get its direction.
            Id originId = geometry.entities[originIndex]->id;
            Id xId      = geometry.entities[xIndex]->id;
            Id yId      = geometry.entities[yIndex]->id;
            geometry.addRelation(std::make_unique<Lock>(PointRef{ originId, 0 }, Pos(0.0f, 0.0f)));
            geometry.addRelation(std::make_unique<Lock>(PointRef{ xId, 0 }, Pos(-AX, 0.0f)));
            geometry.addRelation(std::make_unique<Lock>(PointRef{ xId, 1 }, Pos(AX, 0.0f)));
            geometry.addRelation(std::make_unique<Lock>(PointRef{ yId, 0 }, Pos(0.0f, -AX)));
            geometry.addRelation(std::make_unique<Lock>(PointRef{ yId, 1 }, Pos(0.0f, AX)));
        }

        // Write the in-memory geometry into the geometry layer's JSON payload.
        void flushGeometryToLayer() {

            Layer* layer = geometryLayer();
            if (!layer) { return; }

            layer->data = geometry.toJson();
        }

        // Rebuild the in-memory geometry from the geometry layer's JSON payload.
        void parseGeometryFromLayer() {

            geometry.clear();

            Layer* layer = geometryLayer();
            if (!layer) { return; }

            geometry = SketchGeometry::fromJson(layer->data);
        }

        bool removeLayer(const std::string& layerName) {

            auto it = std::find_if(layers.begin(), layers.end(),
                [&](const Layer& layer) { return layer.name == layerName; });

            if (it == layers.end()) { return false; }

            layers.erase(it);
            dirty = true;
            return true;
        }

        // Manifest
        //--------------------------------------------------

        Json manifest() const {

            Json index = Json::array();

            for (const Layer& layer : layers) {
                index.push_back(layer.manifestEntry());
            }

            return Json{
                { "format", FormatVersion },
                { "name", name },
                { "layers", index }
            };
        }

        // Persistence
        //--------------------------------------------------

        // Derive a workspace folder name from a display name: "<Name>.sketch".
        static std::string folderNameFor(const std::string& displayName) {
            return displayName + Extension;
        }

        // Derive a display name back from a "<Name>.sketch" folder path.
        static std::string displayNameFor(const std::string& folderPath) {

            std::filesystem::path p(folderPath);
            std::string stem = p.filename().string();

            const std::string ext = Extension;
            if (stem.size() > ext.size() &&
                stem.compare(stem.size() - ext.size(), ext.size(), ext) == 0) {
                stem = stem.substr(0, stem.size() - ext.size());
            }

            return stem;
        }

        static bool writeJsonFile(const std::filesystem::path& file, const Json& json) {

            std::ofstream out(file, std::ios::binary | std::ios::trunc);

            if (!out) {
                dbg("[Project] failed to open for write: %s", file.string().c_str());
                return false;
            }

            out << json.dump(2);
            return static_cast<bool>(out);
        }

        static bool readJsonFile(const std::filesystem::path& file, Json& outJson) {

            std::ifstream in(file, std::ios::binary);

            if (!in) { return false; }

            try {
                in >> outJson;
            }
            catch (const std::exception& ex) {
                dbg("[Project] failed to parse %s: %s", file.string().c_str(), ex.what());
                return false;
            }

            return true;
        }

        // Write the whole workspace folder structure to `folder` (a path ending
        // in `.sketch`). Creates the folder, manifest, and per-layer files.
        bool writeToFolder(const std::string& folder) {

            // Make sure the geometry layer reflects the current geometry.
            flushGeometryToLayer();

            std::error_code ec;
            std::filesystem::path root(folder);

            std::filesystem::create_directories(root, ec);
            if (ec) {
                dbg("[Project] could not create workspace dir: %s", folder.c_str());
                return false;
            }

            std::filesystem::create_directories(root / LayersDirName, ec);

            if (!writeJsonFile(root / ManifestName, manifest())) {
                return false;
            }

            for (const Layer& layer : layers) {
                if (!writeJsonFile(root / layer.relativePath(), layer.data)) {
                    return false;
                }
            }

            return true;
        }

        // Save to the current workspace path. Returns false if the project has
        // never been saved (no path yet) — the caller should fall back to saveAs.
        bool save() {

            if (path.empty()) { return false; }

            if (!writeToFolder(path)) { return false; }

            dirty = false;
            return true;
        }

        // Save to a brand-new workspace folder, adopting its name and path.
        bool saveAs(const std::string& folder) {

            if (folder.empty()) { return false; }

            if (!writeToFolder(folder)) { return false; }

            path = folder;
            name = displayNameFor(folder);
            dirty = false;
            return true;
        }

        // Load a workspace from a `<Name>.sketch` folder, replacing contents.
        bool loadFromFolder(const std::string& folder) {

            std::filesystem::path root(folder);

            Json manifestJson;
            if (!readJsonFile(root / ManifestName, manifestJson)) {
                dbg("[Project] missing/unreadable manifest in %s", folder.c_str());
                return false;
            }

            std::vector<Layer> loaded;

            const Json& index = manifestJson.value("layers", Json::array());

            for (const Json& entry : index) {

                Layer layer(
                    entry.value("name", std::string("layer")),
                    layerKindFromString(entry.value("kind", std::string("generic")))
                );

                std::string rel = entry.value("file", layer.relativePath());

                Json body;
                if (readJsonFile(root / rel, body)) {
                    layer.data = std::move(body);
                }

                loaded.push_back(std::move(layer));
            }

            layers = std::move(loaded);
            path = folder;
            name = manifestJson.value("name", displayNameFor(folder));

            parseGeometryFromLayer();
            ensureDatums();

            dirty = false;
            return true;
        }
    };
}
