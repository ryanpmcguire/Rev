module;

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

export module Cam.App.Operation;

import Cam.App.Model;
import Rev.OS.File;

export namespace Cam::App {

    using Json = nlohmann::json;

    enum class OperationType {
        Import,         // root seed: geometry imported from a STEP file
        Defeature,      // remove selected feature(s)
        ExtendFeature,  // extend selected face(s) outward
        Extrude,        // extrude a profile face up to an end face
        ThreadMill,     // reduce a hole to its pre-thread bore; thread cut by the toolpath
        Chamfer         // remove a chamfer (defeature) leaving the sharp edge; chamfer cut by the toolpath
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

    // The root seed of a material-state tree: imports geometry from a STEP file
    // and (optionally) scales it. Lives in the final state and is the single
    // place the source model enters the project. scaleLocked ties the three axes
    // together so editing one scales uniformly (the default).
    struct ImportOperation : Operation {
        std::string sourcePath;             // the STEP file this state imported
        double scaleX = 1.0, scaleY = 1.0, scaleZ = 1.0;
        bool scaleLocked = true;

        OperationType type() const override { return OperationType::Import; }
        const char* typeName() const override { return "Import"; }
        std::string displayName() const override { return "Import"; }

        // Re-seed the model from the source file at the current scale. When no
        // source is recorded (legacy projects whose geometry is already baked
        // into the stage), this is a no-op and the existing model is kept.
        bool apply(Model& model) override {
            if (sourcePath.empty()) { return true; }

            try {
                Rev::OS::File file({ .pathname = sourcePath });
                model.loadStepScaled(file, scaleX, scaleY, scaleZ);
                return !model.shape.IsNull();
            }
            catch (...) {
                return false;
            }
        }

        Json getState() const override {
            Json json = Operation::getState();
            json["sourcePath"] = sourcePath;
            json["scaleX"] = scaleX;
            json["scaleY"] = scaleY;
            json["scaleZ"] = scaleZ;
            json["scaleLocked"] = scaleLocked;
            return json;
        }

        void setState(const Json& json) override {
            Operation::setState(json);
            if (json.contains("sourcePath") && json["sourcePath"].is_string()) {
                sourcePath = json["sourcePath"].get<std::string>();
            }
            if (json.contains("scaleX") && json["scaleX"].is_number()) { scaleX = json["scaleX"].get<double>(); }
            if (json.contains("scaleY") && json["scaleY"].is_number()) { scaleY = json["scaleY"].get<double>(); }
            if (json.contains("scaleZ") && json["scaleZ"].is_number()) { scaleZ = json["scaleZ"].get<double>(); }
            if (json.contains("scaleLocked") && json["scaleLocked"].is_boolean()) {
                scaleLocked = json["scaleLocked"].get<bool>();
            }
        }
    };

    struct DefeatureOperation : Operation {
        OperationType type() const override { return OperationType::Defeature; }
        const char* typeName() const override { return "Defeature"; }
        std::string displayName() const override { return "Defeature"; }

        bool apply(Model& model) override {
            return model.defeatureSelected();
        }
    };

    // Chamfer = a geometry-aware DEFEATURE.  In the reverse-process model the
    // final part HAS the chamfer; this operation removes the selected chamfer
    // face(s) -- leaving the pre-chamfer sharp edge -- so the resulting delta
    // volume IS the chamfer wedge the toolpath then cuts with a chamfer bit.  The
    // only extra datum over a plain defeature is the chamfer ANGLE (inferred from
    // the selected face at creation), which seeds the toolpath + tool selection.
    struct ChamferOperation : Operation {
        double chamferAngle = 45.0;   // degrees from horizontal

        OperationType type() const override { return OperationType::Chamfer; }
        const char* typeName() const override { return "Chamfer"; }
        std::string displayName() const override { return "Chamfer"; }

        bool apply(Model& model) override {
            // Restore the selection this op was created from -- a recompute clears
            // it -- so we always remove exactly the chamfer faces we referenced.
            model.clearSelection();
            for (std::size_t id : referencedFaces) { model.selectFace(id); }
            return model.defeatureSelected();
        }

        Json getState() const override {
            Json json = Operation::getState();
            json["chamferAngle"] = chamferAngle;
            return json;
        }

        void setState(const Json& json) override {
            Operation::setState(json);
            if (json.contains("chamferAngle") && json["chamferAngle"].is_number()) {
                chamferAngle = json["chamferAngle"].get<double>();
            }
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

    // Reduce a selected cylindrical hole to its PRE-THREAD bore, leaving the
    // thread itself to the Thread Mill toolpath strategy (the geometry is never
    // modeled -- a thread is fully described by its callout).  The hole as
    // drawn is the thread's major-diameter cylinder; this fills it inward to
    // `preBoreDiameter` so a later step can drill that plain bore.
    //
    //   majorDiameter / pitch  -- the thread callout the toolpath cuts.
    //   preBoreDiameter        -- the bore left for the mill (and drilled
    //                             later); defaults to the 60-degree minor
    //                             diameter (major - 1.0825 * pitch), overridable.
    struct ThreadMillOperation : Operation {
        int holeFace = -1;
        double majorDiameter = 2.0;     // M2 default
        double pitch = 0.4;
        double preBoreDiameter = 1.6;
        bool internal = true;           // internal (tapped hole) vs external thread

        OperationType type() const override { return OperationType::ThreadMill; }
        const char* typeName() const override { return "ThreadMill"; }
        std::string displayName() const override { return "Thread Mill"; }

        // The 60-degree thread minor diameter for a callout -- a sensible
        // default pre-bore when the user hasn't dialed one in.
        static double minorDiameter(double major, double p) {
            return major - 1.0825 * p;
        }

        std::vector<FaceSlot> faceSlots() override {
            return { { "Thread hole", &holeFace } };
        }

        bool ready() const override {
            return holeFace >= 0 && preBoreDiameter > 0.0 && preBoreDiameter < majorDiameter;
        }

        bool apply(Model& model) override {
            if (!ready()) { return false; }
            return model.threadMillInfill(
                static_cast<std::size_t>(holeFace),
                preBoreDiameter
            );
        }

        Json getState() const override {
            Json json = Operation::getState();
            json["holeFace"] = holeFace;
            json["majorDiameter"] = majorDiameter;
            json["pitch"] = pitch;
            json["preBoreDiameter"] = preBoreDiameter;
            json["internal"] = internal;
            return json;
        }

        void setState(const Json& json) override {
            Operation::setState(json);
            if (json.contains("holeFace") && json["holeFace"].is_number_integer()) {
                holeFace = json["holeFace"].get<int>();
            }
            if (json.contains("majorDiameter") && json["majorDiameter"].is_number()) {
                majorDiameter = json["majorDiameter"].get<double>();
            }
            if (json.contains("pitch") && json["pitch"].is_number()) {
                pitch = json["pitch"].get<double>();
            }
            if (json.contains("preBoreDiameter") && json["preBoreDiameter"].is_number()) {
                preBoreDiameter = json["preBoreDiameter"].get<double>();
            }
            if (json.contains("internal") && json["internal"].is_boolean()) {
                internal = json["internal"].get<bool>();
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
        else if (type == "ThreadMill") { op = new ThreadMillOperation(); }
        else if (type == "Chamfer") { op = new ChamferOperation(); }
        else { op = new ImportOperation(); }

        op->setState(json);

        return op;
    }
}
