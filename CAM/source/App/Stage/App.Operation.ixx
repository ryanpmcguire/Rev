module;

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

export module Cam.App.Operation;

import Cam.App.Model;

export namespace Cam::App {

    using Json = nlohmann::json;

    enum class OperationType {
        Import,         // root seed: geometry imported from a STEP file
        Defeature,      // remove selected feature(s)
        ExtendFeature,  // extend selected face(s) outward
        Extrude         // extrude a profile face up to an end face
    };

    // A named, editable single-face reference exposed by an operation. The GUI
    // renders these as selectable rows; whichever is "active" gets filled when
    // the user ctrl+clicks a face in the world view. `face` points into the
    // operation's own storage (-1 = unset).
    struct FaceSlot {
        std::string name;
        int* face = nullptr;

        // An optional numeric offset paired with the face. When `offset` is
        // non-null the GUI shows a small numeric input beside the face picker —
        // the face defines a reference plane and the offset displaces the result
        // from it along the profile normal.
        double* offset = nullptr;
    };

    // An operation transforms a prior material state (Model) into a new one.
    // A Stage owns exactly one Operation describing how it was produced.
    struct Operation {

        virtual ~Operation() = default;

        // Faces in the prior model that this operation referenced (the selection
        // that drove it). Used to highlight the participating faces in the world
        // view when the operation is hovered/selected in the stage tree.
        std::vector<std::size_t> referencedFaces;

        virtual OperationType type() const = 0;
        virtual const char* typeName() const = 0;     // serialization key
        virtual std::string displayName() const = 0;  // GUI label

        // Editable single-face references this operation exposes (default none).
        // Operations driven by named faces (e.g. extrude) override this so the
        // tree can present and fill them.
        virtual std::vector<FaceSlot> faceSlots() { return {}; }

        // True once the operation has everything it needs to produce geometry.
        virtual bool ready() const { return true; }

        // Transform `model` in place (it has already been seeded as a copy of
        // the prior state's model). Returns true on geometric success.
        // The default (Import) is a no-op: the model is the imported seed.
        virtual bool apply(Model& model) { return true; }

        // Operation-specific parameters. Base writes the type tag and the
        // referenced faces; subclasses extend via getState()/setState().
        virtual Json getState() const {
            Json json;
            json["type"] = typeName();
            json["referencedFaces"] = referencedFaces;
            return json;
        }

        virtual void setState(const Json& json) {
            if (json.is_object() &&
                json.contains("referencedFaces") &&
                json["referencedFaces"].is_array()) {
                referencedFaces =
                    json["referencedFaces"].get<std::vector<std::size_t>>();
            }
        }

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
            Operation::setState(json);
            if (json.contains("distance") && json["distance"].is_number()) {
                distance = json["distance"].get<double>();
            }
        }
    };

    // Extrude a profile face normal to itself up to an end face's plane, then
    // fuse the swept solid into the part. Both faces are picked on the prior
    // model; -1 means "not yet picked".
    struct ExtrudeOperation : Operation {
        int profileFace = -1;

        // The face whose plane the offset is measured from. Defaults to the
        // profile face itself (-1 means "same as profile"), so a fresh extrude
        // is simply "offset mm from the profile". The user may instead pick a
        // different face to offset from.
        int offsetFace = -1;
        double offset = 0.0;

        OperationType type() const override { return OperationType::Extrude; }
        const char* typeName() const override { return "Extrude"; }
        std::string displayName() const override { return "Extrude"; }

        // The effective origin face: the picked offset face, or the profile.
        int originFace() const { return offsetFace >= 0 ? offsetFace : profileFace; }

        std::vector<FaceSlot> faceSlots() override {
            return {
                { "Profile face", &profileFace },
                { "Offset face", &offsetFace, &offset }
            };
        }

        bool ready() const override {
            return profileFace >= 0;
        }

        bool apply(Model& model) override {
            if (!ready()) { return false; }

            return model.extrudeToFaceOffset(
                static_cast<std::size_t>(profileFace),
                static_cast<std::size_t>(originFace()),
                offset
            );
        }

        Json getState() const override {
            Json json = Operation::getState();
            json["profileFace"] = profileFace;
            json["offsetFace"] = offsetFace;
            json["offset"] = offset;
            return json;
        }

        void setState(const Json& json) override {
            Operation::setState(json);
            if (json.contains("profileFace") && json["profileFace"].is_number_integer()) {
                profileFace = json["profileFace"].get<int>();
            }
            if (json.contains("offsetFace") && json["offsetFace"].is_number_integer()) {
                offsetFace = json["offsetFace"].get<int>();
            }
            if (json.contains("offset") && json["offset"].is_number()) {
                offset = json["offset"].get<double>();
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
        else if (type == "Extrude") { op = new ExtrudeOperation(); }
        else { op = new ImportOperation(); }

        op->setState(json);

        return op;
    }
}
