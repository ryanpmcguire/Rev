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
            datum(std::make_unique<Segment2>(Pos(-AX, 0.0f), Pos(AX, 0.0f)));   // X axis
            datum(std::make_unique<Segment2>(Pos(0.0f, -AX), Pos(0.0f, AX)));   // Y axis

            // The origin is not locked by fiat -- it is locked to mathematical (0,0)
            // by an explicit Lock relation, the root of all authority.
            Id originId = geometry.entities[originIndex]->id;
            geometry.addRelation(std::make_unique<Lock>(PointRef{ originId, 0 }, Pos(0.0f, 0.0f)));
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
