module;

#include <string>

#include <nlohmann/json.hpp>

export module Sketch.App.Layer;

export namespace Sketch::App {

    using Json = nlohmann::json;

    // The role a layer plays inside a sketch workspace. Each layer is one JSON
    // file under the workspace's `layers/` folder; its kind tells the app how to
    // interpret the payload.
    enum class LayerKind {
        Geometry,   // the sketch itself: profiles / chains / segments
        Fill,       // output of a fill strategy (lawnmower, hatch, ...)
        Generic     // anything else / future use
    };

    inline std::string layerKindToString(LayerKind kind) {
        switch (kind) {
            case LayerKind::Geometry: return "geometry";
            case LayerKind::Fill:     return "fill";
            case LayerKind::Generic:  return "generic";
        }
        return "generic";
    }

    inline LayerKind layerKindFromString(const std::string& value) {
        if (value == "geometry") { return LayerKind::Geometry; }
        if (value == "fill")     { return LayerKind::Fill; }
        return LayerKind::Generic;
    }

    // A single tiered file within a sketch workspace. `data` is the raw payload
    // round-tripped to `layers/<name>.json`; the concrete geometry / fill schema
    // lives inside it and is filled in as those features land.
    struct Layer {

        std::string name = "layer";
        LayerKind kind = LayerKind::Generic;

        Json data = Json::object();

        Layer() = default;

        Layer(std::string name, LayerKind kind)
            : name(std::move(name)), kind(kind) {}

        // Path of this layer's file relative to the workspace root.
        std::string relativePath() const {
            return "layers/" + name + ".json";
        }

        // The entry recorded for this layer in the workspace manifest's index.
        Json manifestEntry() const {
            return Json{
                { "name", name },
                { "kind", layerKindToString(kind) },
                { "file", relativePath() }
            };
        }
    };
}
