module;

#include <string>

#include <nlohmann/json.hpp>

export module Cam.App.Operation;

import Cam.App.Model;

export namespace Cam::App {

    using Json = nlohmann::json;

    enum class OperationType {
        Import,         // root seed: geometry imported from a STEP file
        Defeature,      // remove selected feature(s)
        ExtendFeature   // extend selected face(s) outward
    };

    // An operation transforms a prior material state (Model) into a new one.
    // A Stage owns exactly one Operation describing how it was produced.
    struct Operation {

        virtual ~Operation() = default;

        virtual OperationType type() const = 0;
        virtual const char* typeName() const = 0;     // serialization key
        virtual std::string displayName() const = 0;  // GUI label

        // Transform `model` in place (it has already been seeded as a copy of
        // the prior state's model). Returns true on geometric success.
        // The default (Import) is a no-op: the model is the imported seed.
        virtual bool apply(Model& model) { return true; }

        // Operation-specific parameters. Base writes only the type tag.
        virtual Json getState() const {
            Json json;
            json["type"] = typeName();
            return json;
        }

        virtual void setState(const Json&) {}

        static Operation* fromState(const Json& json);
    };

    struct ImportOperation : Operation {
        OperationType type() const override { return OperationType::Import; }
        const char* typeName() const override { return "Import"; }
        std::string displayName() const override { return "Import"; }
    };

    struct DefeatureOperation : Operation {
        OperationType type() const override { return OperationType::Defeature; }
        const char* typeName() const override { return "Defeature"; }
        std::string displayName() const override { return "Defeature"; }

        bool apply(Model& model) override {
            return model.defeatureSelected();
        }
    };

    struct ExtendFeatureOperation : Operation {
        double distance = 10.0;

        OperationType type() const override { return OperationType::ExtendFeature; }
        const char* typeName() const override { return "ExtendFeature"; }
        std::string displayName() const override { return "Extend Feature"; }

        bool apply(Model& model) override {
            return model.extendSelected(distance);
        }

        Json getState() const override {
            Json json = Operation::getState();
            json["distance"] = distance;
            return json;
        }

        void setState(const Json& json) override {
            if (json.contains("distance") && json["distance"].is_number()) {
                distance = json["distance"].get<double>();
            }
        }
    };

    // Factory: reconstruct an operation from its serialized form. Unknown or
    // missing types fall back to a plain Import (no-op) operation, since the
    // stage's resulting model is already baked into its own serialized geometry.
    Operation* Operation::fromState(const Json& json) {

        std::string type = "Import";

        if (json.is_object() && json.contains("type") && json["type"].is_string()) {
            type = json["type"].get<std::string>();
        }

        Operation* op = nullptr;

        if (type == "Defeature") { op = new DefeatureOperation(); }
        else if (type == "ExtendFeature") { op = new ExtendFeatureOperation(); }
        else { op = new ImportOperation(); }

        op->setState(json);

        return op;
    }
}
