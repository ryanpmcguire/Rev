module;

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <dbg.hpp>

export module Sketch.App.Project;

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

        // The pure blind offset, kept as well-formed chains (not the flattened
        // display copy): the artifact the valid/invalid chain extraction consumes.
        std::vector<Chain> offsetChains;

        // The crossing method's fragments: the blind offset split at every crossing
        // and regrouped into sub-chains of contiguous same-crossing-number pieces.
        // The number IS the category (anchored absolute depth: 0 = borders the
        // unbounded outside); the final step simply keeps the zero fragments.
        std::vector<Chain::NumberedChain> crossingFragments;

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
        void insetIntoNewLayer(float amount) {
            offsetChains.clear();
            for (Chain& chain : Chain::build(geometry.entities)) {
                offsetChains.push_back(chain.offsetRaw(amount));
            }

            SketchGeometry result;
            crossingMethod(result);           // the other strategy: signedAreaMethod
            addIntersectionMarks(result);     // appended last, so the dots draw on top

            result.normalize();
            offsetLayers.clear();
            offsetLayers.push_back(std::move(result));
            dirty = true;
        }

        // The absolute-chirality group label of a loop, read straight off its own
        // signed area: CW and CCW are facts of the geometry, not comparisons.
        static const char* chiralityGroup(const Chain& loop) {
            int w = loop.windingSign();
            return (w < 0) ? "chirality/cw" : (w > 0) ? "chirality/ccw" : "chirality/none";
        }

        // Method ONE -- the signed-area method: fracture each blind-offset chain at
        // every self-crossing into simple loops (direction-preserving splits only),
        // then stamp each loop with its own absolute chirality, read from its exact
        // signed area. offsetChains is untouched.
        void signedAreaMethod(SketchGeometry& result) {
            for (const Chain& raw : offsetChains) {
                for (Chain& loop : raw.extractLoopsByFracture()) {
                    const char* label = chiralityGroup(loop);
                    for (auto& e : loop.edges) { e->group = label; result.add(std::move(e)); }
                }
            }
        }

        // Method TWO -- the crossing method: split each blind-offset chain at every
        // self-crossing, walk the sub-edges in their original order accumulating the
        // signed crossing number (the other strand crossing from our positive side
        // to our negative side counts -1, negative to positive +1), fragment the run
        // into sub-chains of contiguous same-number pieces, then PRUNE: the keep
        // level is decided by the whole chain's accumulated turning -- a
        // counterclockwise chain keeps its 0 (green) fragments, a clockwise chain
        // keeps its -1 (blue) fragments -- and everything else is discarded. The
        // kept fragments live whole in crossingFragments; the display copy is
        // stamped "crossing/<number>/<fragment>". offsetChains is untouched.
        void crossingMethod(SketchGeometry& result) {
            crossingFragments.clear();
            for (const Chain& raw : offsetChains) {
                const int keep = (raw.turningSign() < 0) ? -1 : 0;   // CCW keeps green, CW keeps blue
                for (Chain::NumberedChain& nc : raw.fragmentByCrossingNumber()) {
                    if (nc.number != keep) { continue; }
                    std::string label = "crossing/" + std::to_string(nc.number)
                                      + "/" + std::to_string(crossingFragments.size());
                    for (const auto& e : nc.chain.edges) {
                        std::unique_ptr<Stoicheion> copy = e->clone();
                        copy->group = label;
                        result.add(std::move(copy));
                    }
                    crossingFragments.push_back(std::move(nc));
                }
            }
        }

        // Every intersection of the pure blind offset -- each chain crossed with
        // itself, and each pair of chains with each other -- as Point2 stoicheia in
        // the "intersections" group.
        void addIntersectionMarks(SketchGeometry& result) {
            std::vector<Pos> crossings;
            for (const Chain& c : offsetChains) {
                for (const Pos& p : c.allSelfIntersections()) { crossings.push_back(p); }
            }
            for (size_t i = 0; i < offsetChains.size(); i++) {
                for (size_t j = i + 1; j < offsetChains.size(); j++) {
                    for (const auto& ea : offsetChains[i].edges) {
                        for (const auto& eb : offsetChains[j].edges) {
                            Chain::edgeCross(*ea, *eb, crossings);
                        }
                    }
                }
            }
            for (const Pos& p : crossings) {
                std::unique_ptr<Point2> dot = std::make_unique<Point2>(p);
                dot->group = "intersections";
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
