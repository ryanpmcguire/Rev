module;

#include <cstddef>
#include <string>
#include <vector>
#include <algorithm>

#include <dbg.hpp>

export module Cam.App.Stage;

import Rev.OS.File;
import Rev.Core.Pos3;
import Rev.Core.Vertex3;

import Cam.App.Model;
import Cam.App.Operation;
import Cam.App.Probe;
import Cam.App.Tool;
import Cam.App.ToolLibrary;
import Cam.App.ToolPath;

export namespace Cam::App {

    // A Stage is one node in the machining history tree. It owns the operation
    // that produced it, the resulting material state (model), the removed-volume
    // delta, and the toolpath that cuts that delta. The prior material state is
    // the parent's model.
    struct Stage {

        // Operation
        //--------------------------------------------------

        // How this stage was produced from its parent. Owned.
        Operation* operation = nullptr;

        // Geometry
        //--------------------------------------------------

        // The resulting material state.
        Model model;

        // Removed material from parent -> this state.
        //
        // delta = parent.model - model
        Model delta;
        bool hasDelta = false;

        // Family
        //--------------------------------------------------

        Stage* parent = nullptr;
        std::vector<Stage*> children;

        // State
        //--------------------------------------------------

        bool committed = false;
        bool working = false;

        // True for the auto-generated stock-definition stages (the four faces
        // that grow the final prism out to the defined raw stock).  These are
        // regenerated when stock parameters change, but remain fully editable
        // (tool / toolpath settings) like any other stage.
        bool stockGenerated = false;

        std::string name = "";

        // Component visibility
        //--------------------------------------------------

        // Per-component show/hide, surfaced as the collapsible stage tree in the
        // left panel and honoured by the 3D world view. These only ever hide a
        // component that the view's selection policy would otherwise show.
        // These are visibility *requests* ("show me, if the view allows it"), not
        // absolute commands — the world view further filters them by the current
        // selection (e.g. only the primary selected stage shows its models).
        // By default a stage shows its prior model (the stock it cuts into) plus
        // the removed-material delta; the resulting model is hidden because it is
        // implied by prior − delta.
        struct ComponentVisibility {
            bool priorModel = true;   // parent stage's resulting model (the stock)
            bool model      = false;  // this stage's resulting model (implied)
            bool operation  = true;   // the operation (no geometry yet — stub)
            bool delta      = true;   // removed-volume delta model
            bool toolPath   = true;   // the toolpath
            bool probe      = true;   // the probe sub-component (targets + result)
        };

        ComponentVisibility visible;

        // Highlight
        //--------------------------------------------------

        // Transient (not serialized): faces in the prior model to highlight in
        // the world view when this stage's operation is hovered in the tree.
        std::vector<std::size_t> highlightedOperationFaces;

        // Tool Path
        //--------------------------------------------------

        ToolPath toolPath;
        bool hasToolPath = false;

        // Probe
        //--------------------------------------------------

        // Optional probe sub-component: a break in the machining path (before
        // this stage's own cut) where the machine task-switches to the probe
        // tool, touches the listed targets, and fits a persistent frame
        // correction.  Disabled by default; surfaced as the "Probe" row in the
        // stage tree.  See Cam.App.Probe.
        ProbeSpec probe;

        bool hasProbe() const { return probe.enabled; }

        // Preview-only: the probe operation's motion (rapid-to-standoff, slow
        // plunge-to-expected-contact, retract) as a timed CAD-space path, so the
        // scrub timeline animates the probe exactly like the cut.  Rebuilt by the
        // world view (it knows the correction); empty when the stage has no
        // probe.  NOT used for execution (Air builds probe moves itself).
        ToolPath probePreviewPath;

        // Construction
        //--------------------------------------------------

        static Stage* FromImport(Rev::OS::File& file) {

            Stage* stage = new Stage();

            ImportOperation* op = new ImportOperation();
            op->sourcePath = file.string();
            stage->operation = op;

            stage->model = Model::FromStep(file);
            stage->model.clearSelection();

            stage->delta.clear();
            stage->hasDelta = false;

            stage->parent = nullptr;
            stage->children.clear();

            stage->committed = true;
            stage->working = false;

            stage->name = "Final State";

            return stage;
        }

        static Stage* FromPrior(Stage* prior) {

            if (!prior) { return nullptr; }

            Stage* stage = new Stage();

            // Operation is assigned once an operation is applied to the
            // working stage (defeature / extend / ...).
            stage->operation = nullptr;

            stage->model = prior->model;
            stage->model.clearSelection();
            stage->model.changed = false;

            stage->delta.clear();
            stage->hasDelta = false;

            stage->parent = prior;
            stage->children.clear();

            stage->committed = false;
            stage->working = true;

            stage->name = "Working State";

            prior->children.push_back(stage);

            return stage;
        }


        // Selection
        //--------------------------------------------------

        void clearSelection() {

            model.clearSelection();
        }

        // Family management
        //--------------------------------------------------

        bool isRoot() const {

            return parent == nullptr;
        }

        bool hasChild(Stage* stage) const {

            return std::find(children.begin(), children.end(), stage) != children.end();
        }

        void addChild(Stage* stage) {

            if (!stage) { return; }
            if (hasChild(stage)) { return; }

            children.push_back(stage);
            stage->parent = this;
        }

        void removeChild(Stage* stage) {

            if (!stage) { return; }

            auto it = std::find(children.begin(), children.end(), stage);

            if (it == children.end()) { return; }

            children.erase(it);

            if (stage->parent == this) {
                stage->parent = nullptr;
            }
        }

        void detach() {

            Stage* owner = parent;

            if (!owner) { return; }

            parent = nullptr;
            owner->removeChild(this);
        }

        bool contains(Stage* stage) {

            if (this == stage) { return true; }

            for (Stage* child : children) {
                if (child && child->contains(stage)) { return true; }
            }

            return false;
        }

        void collectSubtree(std::vector<Stage*>& out) {

            out.push_back(this);

            for (Stage* child : children) {
                if (child) { child->collectSubtree(out); }
            }
        }

        // Destruction
        //--------------------------------------------------

        void remove() {

            detach();

            std::vector<Stage*> oldChildren = children;
            children.clear();

            for (Stage* child : oldChildren) {

                if (!child) { continue; }

                child->parent = nullptr;
                child->remove();
            }

            model.clear();
            clearDelta();

            delete operation;
            operation = nullptr;

            delete this;
        }

        // Delta
        //--------------------------------------------------

        void clearDelta() {

            delta.clear();
            hasDelta = false;

            clearToolPath();
        }

        void computeDelta(const ToolLibrary& library, const std::string& activeToolName) {

            clearDelta();

            if (!parent) { return; }
            if (!parent->model.loaded) { return; }
            if (!model.loaded) { return; }

            // Removed material = parent state minus current state.
            delta = Model::Difference(model, parent->model);

            hasDelta = delta.loaded;

            if (hasDelta) {
                computeToolPath(library, activeToolName);
            }
        }

        // Tool Path
        //--------------------------------------------------

        void clearToolPath() {

            toolPath.clear();
            hasToolPath = false;
        }

        void computeToolPath(const ToolLibrary& library, const std::string& activeToolName) {

            toolPath.clearPathData();
            hasToolPath = false;

            if (!hasDelta) { return; }
            if (!delta.loaded) { return; }
            if (!parent->model.loaded) { return; }

            // A toolpath with no tool yet is being created for the first time --
            // we'll auto-pick the largest sensible tool once the feature's
            // requirements are known (below).
            const bool autoSelect = toolPath.toolName.empty();

            if (toolPath.toolName.empty()) {
                toolPath.toolName = activeToolName;
            }

            const Tool* tool = library.find(toolPath.toolName);

            if (!tool && !activeToolName.empty()) {
                toolPath.toolName = activeToolName;
                tool = library.find(toolPath.toolName);
            }

            if (!tool) {
                // Last resort: any tool, just to derive the feature requirements.
                tool = library.at(0);
                if (tool) { toolPath.toolName = tool->name; }
            }

            if (!tool) {
                dbg(
                    "[Stage] Tool \"%s\" not in library",
                    toolPath.toolName.c_str()
                );
                return;
            }

            hasToolPath = toolPath.compute(delta, parent->model, *tool);

            // First creation: now that compute() has established the feature's
            // requirements (toolPath.implied), select the LARGEST tool that can
            // actually perform the operation, and recompute with it.
            if (autoSelect) {

                const Tool* best = nullptr;

                for (size_t i = 0; i < library.size(); i++) {
                    const Tool* t = library.at(i);
                    if (!t || !toolPath.accepts(*t)) { continue; }
                    if (!best || t->diameter > best->diameter) { best = t; }
                }

                if (best && best->name != toolPath.toolName) {
                    toolPath.toolName = best->name;
                    hasToolPath = toolPath.compute(delta, parent->model, *best);
                }
            }
        }

        bool link(Stage* nextStage) {

            if (!nextStage) { return false; }

            if (!hasToolPath || !nextStage->hasToolPath) {
                return false;
            }

            if (toolPath.points.empty() || nextStage->toolPath.points.empty()) {
                return false;
            }

            // Pass the user-defined stock centre and rotary axis so the link
            // generates a proper polar arc rather than a straight-line move.
            // Both are propagated to all stages, so reading from this model is fine.
            if (model.hasAxisOrigin && model.hasAxisX) {
                return toolPath.link(
                    nextStage->toolPath,
                    model.axisOrigin,
                    model.axisXDirection,
                    safeArcRadius()
                );
            }

            return toolPath.link(nextStage->toolPath);
        }

        // The clearance radius for a 4th-axis link arc: the farthest any part
        // point reaches from the rotary axis (a wide prism's corners reach far
        // past its faces), plus a solid-but-modest margin so the tool clears
        // the part as it is flipped.  The arc bulges out to this radius.
        float safeArcRadius() const {

            Rev::Core::Pos3 axis = model.axisXDirection;
            const float al = axis.pythag();
            if (al < 1e-6f) { return 0.0f; }
            axis = axis / al;

            const Rev::Core::Pos3 origin = model.axisOrigin;

            float maxPerp = 0.0f;
            for (const Rev::Core::Vertex3& vtx : model.render.triangles) {
                const Rev::Core::Pos3 v = vtx;
                const Rev::Core::Pos3 rel = v - origin;
                const float axial = rel.dot(axis);
                const float perp = (rel - axis * axial).pythag();
                if (perp > maxPerp) { maxPerp = perp; }
            }

            // A nice solid margin, not extreme: the configured retract
            // clearance plus the tool's own radius (so the cutter body, not
            // just the tip, clears the part).
            const float margin =
                toolPath.retractHeight + static_cast<float>(toolPath.toolDiameter) * 0.5f;

            return maxPerp + margin;
        }

        bool needsToolPathComputation() const {

            if (!hasDelta || !delta.loaded || !parent) {
                return false;
            }

            return !hasToolPath || !toolPath.computed || toolPath.points.empty();
        }
    };
}
