module;

#include <cstddef>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS.hxx>

#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>

#include <TopTools_IndexedMapOfShape.hxx>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <gp_GTrsf.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>

#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>

#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>

#include <dbg.hpp>

#include <GeomAbs_SurfaceType.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

export module Cam.App.Model;

import Rev.OS.File;
import Rev.Core.Color;
import Rev.Core.Pos3;
import Rev.Core.Vertex3;

export namespace Cam::App {

    struct Model {

        struct RenderCache {
            std::vector<Rev::Core::Vertex3> triangles;
            std::vector<size_t> triangleFaceIds;
            bool valid = false;

            void clear() {
                triangles.clear();
                triangleFaceIds.clear();
                valid = false;
            }
        };

        struct ShapeStats {
            int compounds = 0;
            int solids = 0;
            int shells = 0;
            int faces = 0;
            int edges = 0;
            int vertices = 0;
            bool valid = false;
            double volume = 0.0;
        };

        // A lightweight, copyable handle to one face of this model: the owning
        // model plus the face index. External code (selection, operations, the
        // GUI) holds these BY VALUE and rebuilds them each frame from the current
        // model, so they never dangle across geometry rebuilds. A Face* is only
        // ever used internally, pointing into faceHandles, which is rebuilt in
        // lockstep with the geometry. Nested in Model so faces (which always
        // belong to a model) avoid any circular import.
        struct Face {
            Model* model = nullptr;
            size_t id = static_cast<size_t>(-1);

            bool valid() const { return model && id < model->faces.size(); }
            Rev::Core::Pos3 normal() const { return valid() ? model->faceNormal(id) : Rev::Core::Pos3{}; }
            Rev::Core::Pos3 point() const { return valid() ? model->facePoint(id) : Rev::Core::Pos3{}; }
            bool selected() const { return model && model->isFaceSelected(id); }

            bool operator==(const Face& other) const {
                return model == other.model && id == other.id;
            }
            bool operator!=(const Face& other) const { return !(*this == other); }
        };

        TopoDS_Shape shape;
        std::vector<TopoDS_Face> faces;

        // The model's own face handles, one per entry in `faces`, rebuilt with
        // the geometry (see collectFaces). The canonical per-face store.
        std::vector<Face> faceHandles;

        RenderCache render;

        std::set<size_t> selectedFaceIds;

        static constexpr size_t NoAxisPickFaceId = static_cast<size_t>(-1);
        static constexpr size_t NoAxisPickCandidateIndex = static_cast<size_t>(-1);

        size_t axisPickFaceId = NoAxisPickFaceId;
        std::vector<Rev::Core::Pos3> axisPickCandidates;
        std::vector<Rev::Core::Pos3> axisPickSelectedPoints;

        Rev::Core::Pos3 axisOrigin = {};
        Rev::Core::Pos3 axisXDirection = { 1.0f, 0.0f, 0.0f };
        Rev::Core::Pos3 axisYDirection = { 0.0f, 1.0f, 0.0f };
        Rev::Core::Pos3 axisZDirection = { 0.0f, 0.0f, 1.0f };
        bool hasAxisOrigin = false;
        bool hasAxisX = false;
        bool hasAxisY = false;
        bool hasAxisZ = false;

        bool loaded = false;
        bool changed = false;

        // Human-readable debug history for recent modeling events.
        std::vector<std::string> history;
        size_t operationSerial = 0;

        // Construction
        //--------------------------------------------------

        static Model FromStep(Rev::OS::File& file) {
            Model model;
            model.loadStep(file);
            return model;
        }

        static Model Difference(const Model& a, const Model& b) {
            Model result;

            if (a.shape.IsNull() || b.shape.IsNull()) {
                dbg("[Difference] failed: input shape was null (aNull=%d bNull=%d)", a.shape.IsNull(), b.shape.IsNull());
                return result;
            }

            try {
                dbg("[Difference] requested");
                a.logShapeStatsStatic("[Difference] input A", a.shape);
                b.logShapeStatsStatic("[Difference] input B", b.shape);

                BRepAlgoAPI_Cut cut(a.shape, b.shape);
                cut.Build();

                if (!cut.IsDone()) {
                    dbg("[Difference] failed: BRepAlgoAPI_Cut IsDone=false");
                    return result;
                }

                TopoDS_Shape cutShape = cut.Shape();

                if (cutShape.IsNull()) {
                    dbg("[Difference] failed: cut result is null");
                    return result;
                }

                TopoDS_Shape healed = result.healShape(cutShape);

                if (healed.IsNull()) {
                    dbg("[Difference] failed: healed result is null");
                    return result;
                }

                result.adoptShape(healed, false, "Difference");
            }
            catch (const Standard_Failure& failure) {
                dbg("[Difference] exception: %s", safeFailureMessage(failure));
                return Model();
            }
            catch (...) {
                dbg("[Difference] unknown exception");
                return Model();
            }

            return result;
        }

        // State / serialization
        //--------------------------------------------------

        void clear() {
            logEvent("[State] clear requested");

            shape = TopoDS_Shape();
            faces.clear();
            faceHandles.clear();
            render.clear();
            selectedFaceIds.clear();
            axisPickFaceId = NoAxisPickFaceId;
            axisPickCandidates.clear();
            axisPickSelectedPoints.clear();
            hasAxisOrigin = false;
            hasAxisX = false;
            hasAxisY = false;
            hasAxisZ = false;

            loaded = false;
            changed = false;

            logEvent("[State] clear completed");
        }

        std::string getState() const {
            if (shape.IsNull()) {
                dbg("[State] getState: shape is null");
                return "";
            }

            std::ostringstream stream;
            BRepTools::Write(shape, stream);

            std::string state = stream.str();

            dbg(
                "[State] getState: bytes=%zu faces=%zu selected=%zu loaded=%d changed=%d",
                state.size(),
                faces.size(),
                selectedFaceIds.size(),
                loaded,
                changed
            );

            return state;
        }

        bool setState(const std::string& state) {
            logEvent(format(
                "[State] setState requested: bytes=%zu currentFaces=%zu currentSelected=%zu",
                state.size(),
                faces.size(),
                selectedFaceIds.size()
            ));

            clear();

            if (state.empty()) {
                logEvent("[State] setState failed: state string is empty");
                return false;
            }

            std::istringstream stream(state);

            if (!stream.good()) {
                logEvent("[State] setState failed: stream not good");
                return false;
            }

            try {
                BRep_Builder builder;
                BRepTools::Read(shape, stream, builder);
            }
            catch (const Standard_Failure& failure) {
                logEvent(format("[State] setState exception: %s", safeFailureMessage(failure)));
                clear();
                return false;
            }
            catch (...) {
                logEvent("[State] setState unknown exception");
                clear();
                return false;
            }

            if (shape.IsNull()) {
                logEvent("[State] setState failed: read produced null shape");
                clear();
                return false;
            }

            refreshTopologyAndRender("setState");

            loaded = true;
            changed = false;

            logShapeStats("[State] setState result", shape);
            logEvent(format(
                "[State] setState succeeded: faces=%zu triangles=%zu",
                faces.size(),
                render.triangleFaceIds.size()
            ));

            return true;
        }

        // Selection
        //--------------------------------------------------

        void clearSelection() {
            if (!selectedFaceIds.empty()) {
                logEvent(format("[Selection] clearSelection: clearing %zu selected face id(s): %s",
                    selectedFaceIds.size(),
                    selectedFaceIdList().c_str()
                ));
            }

            selectedFaceIds.clear();
        }

        void clearAllSelections() {

            clearSelection();

            if (hasAxisPickFace()) {
                logEvent(format(
                    "[AxisPick] cleared face id=%zu",
                    axisPickFaceId
                ));
            }

            axisPickFaceId = NoAxisPickFaceId;
            axisPickCandidates.clear();
            clearAxisPickPointSelection();

            logEvent("[Selection] clearAllSelections");
        }

        void selectFace(size_t faceId) {
            if (faceId >= faces.size()) {
                logEvent(format(
                    "[Selection] selectFace: stale/out-of-range face id %zu ignored? faceCount=%zu",
                    faceId,
                    faces.size()
                ));
            }

            selectedFaceIds.insert(faceId);

            logEvent(format(
                "[Selection] selectFace: id=%zu selectedCount=%zu selected={%s}",
                faceId,
                selectedFaceIds.size(),
                selectedFaceIdList().c_str()
            ));
        }

        void deselectFace(size_t faceId) {
            selectedFaceIds.erase(faceId);

            logEvent(format(
                "[Selection] deselectFace: id=%zu selectedCount=%zu selected={%s}",
                faceId,
                selectedFaceIds.size(),
                selectedFaceIdList().c_str()
            ));
        }

        bool isFaceSelected(size_t faceId) const {
            return selectedFaceIds.contains(faceId);
        }

        void toggleFace(size_t faceId) {
            if (isFaceSelected(faceId)) {
                deselectFace(faceId);
            }
            else {
                selectFace(faceId);
            }
        }

        // Axis-definition face pick (alt+click); separate from defeature selection.
        //--------------------------------------------------

        bool hasAxisPickFace() const {
            return axisPickFaceId != NoAxisPickFaceId && axisPickFaceId < faces.size();
        }

        bool isAxisPickFace(size_t faceId) const {
            return hasAxisPickFace() && axisPickFaceId == faceId;
        }

        void refreshAxisPickCandidates() {

            axisPickCandidates.clear();

            if (!hasAxisPickFace()) { return; }

            axisPickCandidates = faceAxisPickMarkers(axisPickFaceId);
        }

        void clearAxisPickPointSelection() {

            axisPickSelectedPoints.clear();
        }

        void clearAxisFrame() {

            hasAxisOrigin = false;
            hasAxisX = false;
            hasAxisY = false;
            hasAxisZ = false;
        }

        void invalidateDefinedAxes() {

            hasAxisX = false;
            hasAxisY = false;
            hasAxisZ = false;
        }

        void clearAxisPickSelection() {

            clearAxisPickPointSelection();
            clearAxisFrame();
        }

        bool isAxisPickCandidateSelected(size_t candidateIndex) const {

            if (candidateIndex >= axisPickCandidates.size()) {
                return false;
            }

            const Rev::Core::Pos3& point = axisPickCandidates[candidateIndex];
            const float tolerance = 1e-3f;

            for (const Rev::Core::Pos3& selected : axisPickSelectedPoints) {

                if ((selected - point).pythag() <= tolerance) {
                    return true;
                }
            }

            return false;
        }

        bool toggleAxisPickPoint(const Rev::Core::Pos3& point) {

            const float tolerance = 1e-3f;

            for (size_t i = 0; i < axisPickSelectedPoints.size(); i++) {

                if ((axisPickSelectedPoints[i] - point).pythag() <= tolerance) {

                    axisPickSelectedPoints.erase(
                        axisPickSelectedPoints.begin() + static_cast<std::ptrdiff_t>(i)
                    );

                    invalidateDefinedAxes();

                    logEvent(format(
                        "[AxisPick] deselected point (%.3f %.3f %.3f) count=%zu",
                        point.x, point.y, point.z,
                        axisPickSelectedPoints.size()
                    ));

                    return true;
                }
            }

            for (size_t i = 0; i < axisPickCandidates.size(); i++) {

                if ((axisPickCandidates[i] - point).pythag() > tolerance) {
                    continue;
                }

                axisPickSelectedPoints.push_back(axisPickCandidates[i]);
                invalidateDefinedAxes();

                logEvent(format(
                    "[AxisPick] selected point (%.3f %.3f %.3f) count=%zu",
                    point.x, point.y, point.z,
                    axisPickSelectedPoints.size()
                ));

                return true;
            }

            return false;
        }

        bool toggleAxisPickCandidate(size_t candidateIndex) {

            if (candidateIndex >= axisPickCandidates.size()) {
                return false;
            }

            return toggleAxisPickPoint(axisPickCandidates[candidateIndex]);
        }

        bool centerOriginFromSelectedPoints() {

            if (axisPickSelectedPoints.size() != 2) {
                logEvent(format(
                    "[AxisPick] center origin failed: need 2 points (have %zu)",
                    axisPickSelectedPoints.size()
                ));
                return false;
            }

            const Rev::Core::Pos3& a = axisPickSelectedPoints[0];
            const Rev::Core::Pos3& b = axisPickSelectedPoints[1];

            axisOrigin = a.centerTo(b);
            hasAxisOrigin = true;

            logEvent(format(
                "[AxisPick] origin centered at (%.3f %.3f %.3f) between (%.3f %.3f %.3f) and (%.3f %.3f %.3f)",
                axisOrigin.x,
                axisOrigin.y,
                axisOrigin.z,
                a.x, a.y, a.z,
                b.x, b.y, b.z
            ));

            return true;
        }

        static Rev::Core::Pos3 normalizeAxisOr(
            const Rev::Core::Pos3& direction,
            const Rev::Core::Pos3& fallback
        ) {
            const float length = direction.pythag();

            if (length <= 1e-6f) {
                return fallback;
            }

            return direction / length;
        }

        static Rev::Core::Pos3 axisPerpendicularToX(const Rev::Core::Pos3& xAxis) {

            Rev::Core::Pos3 reference = (
                std::fabs(xAxis.z) < 0.9f
                    ? Rev::Core::Pos3(0.0f, 0.0f, 1.0f)
                    : Rev::Core::Pos3(1.0f, 0.0f, 0.0f)
            );

            return normalizeAxisOr(reference.cross(xAxis), { 0.0f, 1.0f, 0.0f });
        }

        static Rev::Core::Pos3 projectOntoPlane(
            const Rev::Core::Pos3& vector,
            const Rev::Core::Pos3& planeNormal
        ) {
            return vector - planeNormal * vector.dot(planeNormal);
        }

        void getOrthonormalAxisFrame(
            Rev::Core::Pos3& xOut,
            Rev::Core::Pos3& yOut,
            Rev::Core::Pos3& zOut
        ) const {

            xOut = normalizeAxisOr(
                hasAxisX ? axisXDirection : Rev::Core::Pos3(1.0f, 0.0f, 0.0f),
                Rev::Core::Pos3(1.0f, 0.0f, 0.0f)
            );

            if (hasAxisY) {
                yOut = normalizeAxisOr(
                    projectOntoPlane(axisYDirection, xOut),
                    axisPerpendicularToX(xOut)
                );
            }
            else {
                yOut = axisPerpendicularToX(xOut);
            }

            zOut = normalizeAxisOr(xOut.cross(yOut), { 0.0f, 0.0f, 1.0f });

            if (hasAxisZ) {
                const Rev::Core::Pos3 zHint = normalizeAxisOr(
                    projectOntoPlane(
                        projectOntoPlane(axisZDirection, xOut),
                        yOut
                    ),
                    zOut
                );

                if (zHint.dot(zOut) < 0.0f) {
                    yOut = yOut * -1.0f;
                    zOut = normalizeAxisOr(xOut.cross(yOut), { 0.0f, 0.0f, 1.0f });
                }
            }
        }

        bool defineAxisFromSelectedPoints(
            const char* axisName,
            Rev::Core::Pos3& directionOut,
            bool& hasAxisOut,
            char axis
        ) {

            if (axisPickSelectedPoints.size() != 1) {
                logEvent(format(
                    "[AxisPick] define %s failed: need 1 point (have %zu)",
                    axisName,
                    axisPickSelectedPoints.size()
                ));
                return false;
            }

            const Rev::Core::Pos3& target = axisPickSelectedPoints[0];

            Rev::Core::Pos3 dir = target - axisOrigin;

            if (axis == 'Y' || axis == 'y') {

                if (hasAxisX) {
                    const Rev::Core::Pos3 xAxis = normalizeAxisOr(
                        axisXDirection,
                        Rev::Core::Pos3(1.0f, 0.0f, 0.0f)
                    );

                    dir = projectOntoPlane(dir, xAxis);
                }
            }
            else if (axis == 'Z' || axis == 'z') {

                Rev::Core::Pos3 xAxis = {};
                Rev::Core::Pos3 yAxis = {};
                Rev::Core::Pos3 zAxis = {};

                getOrthonormalAxisFrame(xAxis, yAxis, zAxis);

                dir = projectOntoPlane(projectOntoPlane(dir, xAxis), yAxis);
            }

            const float length = dir.pythag();

            if (length <= 1e-6f) {
                logEvent(format(
                    "[AxisPick] define %s failed: direction parallel to existing axes",
                    axisName
                ));
                return false;
            }

            directionOut = dir / length;
            hasAxisOut = true;

            Rev::Core::Pos3 frameX = {};
            Rev::Core::Pos3 frameY = {};
            Rev::Core::Pos3 frameZ = {};

            getOrthonormalAxisFrame(frameX, frameY, frameZ);

            if (hasAxisX) { axisXDirection = frameX; }
            if (hasAxisY) { axisYDirection = frameY; }
            if (hasAxisZ) { axisZDirection = frameZ; }

            logEvent(format(
                "[AxisPick] +%s from origin (%.3f %.3f %.3f) toward (%.3f %.3f %.3f) dir=(%.3f %.3f %.3f)",
                axisName,
                axisOrigin.x,
                axisOrigin.y,
                axisOrigin.z,
                target.x,
                target.y,
                target.z,
                directionOut.x,
                directionOut.y,
                directionOut.z
            ));

            return true;
        }

        bool defineAxisXFromSelectedPoints() {
            return defineAxisFromSelectedPoints(
                "X",
                axisXDirection,
                hasAxisX,
                'X'
            );
        }

        bool defineAxisYFromSelectedPoints() {
            return defineAxisFromSelectedPoints(
                "Y",
                axisYDirection,
                hasAxisY,
                'Y'
            );
        }

        bool defineAxisZFromSelectedPoints() {
            return defineAxisFromSelectedPoints(
                "Z",
                axisZDirection,
                hasAxisZ,
                'Z'
            );
        }

        void setAxisPickFace(size_t faceId) {

            if (faceId >= faces.size()) {
                axisPickFaceId = NoAxisPickFaceId;
                axisPickCandidates.clear();
                clearAxisPickSelection();
                return;
            }

            axisPickFaceId = faceId;
            refreshAxisPickCandidates();

            logEvent(format(
                "[AxisPick] face id=%zu candidates=%zu selected=%zu",
                faceId,
                axisPickCandidates.size(),
                axisPickSelectedPoints.size()
            ));
        }

        void clearAxisPickFace() {

            if (!hasAxisPickFace()) { return; }

            logEvent(format(
                "[AxisPick] cleared face id=%zu",
                axisPickFaceId
            ));

            axisPickFaceId = NoAxisPickFaceId;
            axisPickCandidates.clear();
            clearAxisPickSelection();
        }

        void toggleAxisPickFace(size_t faceId) {

            if (isAxisPickFace(faceId)) {
                clearAxisPickFace();
            }
            else {
                setAxisPickFace(faceId);
            }
        }

        std::vector<Rev::Core::Pos3> faceAxisPickMarkers(size_t faceId) const {

            std::vector<Rev::Core::Pos3> markers;

            if (faceId >= faces.size()) {
                return markers;
            }

            const TopoDS_Face& face = faces[faceId];

            TopoDS_Wire wire = BRepTools::OuterWire(face);

            if (wire.IsNull()) {

                TopExp_Explorer wireExp(face, TopAbs_WIRE);

                if (!wireExp.More()) {
                    markers.push_back(facePoint(faceId));
                    return markers;
                }

                wire = TopoDS::Wire(wireExp.Current());
            }

            const float dedupeTolerance = 1e-3f;

            auto appendUnique = [&](const Rev::Core::Pos3& point) {

                for (const Rev::Core::Pos3& existing : markers) {

                    if ((existing - point).pythag() <= dedupeTolerance) {
                        return;
                    }
                }

                markers.push_back(point);
            };

            auto vertexPosition = [](const TopoDS_Vertex& vertex) -> Rev::Core::Pos3 {

                gp_Pnt point = BRep_Tool::Pnt(vertex);

                gp_Trsf trsf = vertex.Location().Transformation();
                point.Transform(trsf);

                return {
                    static_cast<float>(point.X()),
                    static_cast<float>(point.Y()),
                    static_cast<float>(point.Z())
                };
            };

            for (
                TopExp_Explorer edgeExp(wire, TopAbs_EDGE);
                edgeExp.More();
                edgeExp.Next()
            ) {
                TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

                TopoDS_Vertex v1;
                TopoDS_Vertex v2;

                TopExp::Vertices(edge, v1, v2);

                if (!v1.IsNull()) {
                    appendUnique(vertexPosition(v1));
                }

                if (!v2.IsNull()) {
                    appendUnique(vertexPosition(v2));
                }
            }

            appendUnique(facePoint(faceId));

            return markers;
        }

        size_t selectedFaceCount() const {
            return selectedFaceIds.size();
        }

        size_t faceCount() const {
            return faces.size();
        }

        Rev::Core::Pos3 faceNormal(size_t faceId) const {

            if (faceId >= faces.size()) {
                return {};
            }

            const TopoDS_Face& face = faces[faceId];

            BRepAdaptor_Surface surf(face);

            gp_Dir dir;

            if (surf.GetType() == GeomAbs_Plane) {
                dir = surf.Plane().Axis().Direction();
            }
            else {

                double uMid = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
                double vMid = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;

                gp_Pnt point;
                gp_Vec du;
                gp_Vec dv;

                surf.D1(uMid, vMid, point, du, dv);

                gp_Vec normal = du.Crossed(dv);

                if (normal.Magnitude() <= 1e-12) {
                    return {};
                }

                normal.Normalize();
                dir = gp_Dir(normal);
            }

            if (face.Orientation() == TopAbs_REVERSED) {
                dir.Reverse();
            }

            return {
                static_cast<float>(dir.X()),
                static_cast<float>(dir.Y()),
                static_cast<float>(dir.Z())
            };
        }

        // Diameter of a cylindrical face (e.g. a round hole), or 0 if the face
        // is not a cylinder.  Used to read a thread hole's major diameter.
        double faceCylinderDiameter(size_t faceId) const {

            if (faceId >= faces.size()) { return 0.0; }

            BRepAdaptor_Surface surf(faces[faceId]);

            if (surf.GetType() != GeomAbs_Cylinder) { return 0.0; }

            return surf.Cylinder().Radius() * 2.0;
        }

        // The chamfer angle (degrees from horizontal) of a chamfer face -- the
        // angle between its (mid-)normal and the +Z up axis, which by construction
        // equals the face's tilt from horizontal (a flat top reads 0, a vertical
        // wall reads 90, a 45-degree chamfer reads 45).  Matches the tool's
        // taperAngle convention.
        //
        // Works for a curved edge-break (FILLET) too: faceNormal samples the
        // surface midpoint, so a symmetric fillet reads ~45 -- i.e. the fillet is
        // treated as the equivalent chamfer (a common shop practice).  The result
        // is an APPROXIMATION for a fillet (the toolpath cuts flats, not the round).
        double faceChamferAngle(size_t faceId) const {

            if (faceId >= faces.size()) { return 0.0; }

            const Rev::Core::Pos3 n = faceNormal(faceId);
            double nz = std::fabs(static_cast<double>(n.z));
            nz = std::clamp(nz, 0.0, 1.0);

            return std::acos(nz) * (180.0 / 3.14159265358979);
        }

        Rev::Core::Pos3 facePoint(size_t faceId) const {

            if (faceId >= faces.size()) {
                return {};
            }

            const TopoDS_Face& face = faces[faceId];

            BRepAdaptor_Surface surf(face);

            double uMid = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
            double vMid = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;

            gp_Pnt point = surf.Value(uMid, vMid);

            return {
                static_cast<float>(point.X()),
                static_cast<float>(point.Y()),
                static_cast<float>(point.Z())
            };
        }

        // Stock definition
        //--------------------------------------------------

        // True when the solid is a rectangular prism (box): exactly six planar
        // faces whose normals form three mutually-perpendicular antiparallel
        // pairs.  Frame-independent and exact, so it holds for boxes at any
        // orientation.
        bool isRectangularPrism() const {

            if (shape.IsNull()) { return false; }
            if (faces.size() != 6) { return false; }

            const double angTol = 0.0873;  // ~5 degrees

            std::vector<gp_Dir> axes;

            for (size_t i = 0; i < faces.size(); i++) {

                BRepAdaptor_Surface surf(faces[i]);

                if (surf.GetType() != GeomAbs_Plane) { return false; }

                Rev::Core::Pos3 normal = faceNormal(i);
                gp_Vec vec(normal.x, normal.y, normal.z);

                if (vec.Magnitude() <= 1e-9) { return false; }

                gp_Dir dir(vec);

                bool matched = false;

                for (const gp_Dir& axis : axes) {
                    if (dir.IsParallel(axis, angTol)) { matched = true; break; }
                }

                if (!matched) {
                    if (axes.size() == 3) { return false; }
                    axes.push_back(dir);
                }
            }

            if (axes.size() != 3) { return false; }

            return (
                axes[0].IsNormal(axes[1], angTol) &&
                axes[0].IsNormal(axes[2], angTol) &&
                axes[1].IsNormal(axes[2], angTol)
            );
        }

        // Adopt an arbitrary shape into a fresh Model, healing it.
        static Model FromShape(const TopoDS_Shape& input) {

            Model model;

            if (input.IsNull()) { return model; }

            TopoDS_Shape healed = model.healShape(input);

            if (!healed.IsNull() && isShapeValid(healed)) {
                model.adoptShape(healed, false, "FromShape");
            }
            else if (isShapeValid(input)) {
                model.adoptShape(input, false, "FromShape(raw)");
            }

            return model;
        }

        // Axis-frame bounds of this shape: min/max of every vertex projected
        // onto the supplied orthonormal frame (origin + x/y/z unit directions).
        void frameBounds(
            const Rev::Core::Pos3& origin,
            const Rev::Core::Pos3& xDir,
            const Rev::Core::Pos3& yDir,
            const Rev::Core::Pos3& zDir,
            double& x0, double& x1,
            double& y0, double& y1,
            double& z0, double& z1
        ) const {

            bool first = true;

            for (TopExp_Explorer ex(shape, TopAbs_VERTEX); ex.More(); ex.Next()) {

                gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(ex.Current()));

                Rev::Core::Pos3 rel = {
                    static_cast<float>(p.X()) - origin.x,
                    static_cast<float>(p.Y()) - origin.y,
                    static_cast<float>(p.Z()) - origin.z
                };

                double lx = rel.dot(xDir);
                double ly = rel.dot(yDir);
                double lz = rel.dot(zDir);

                if (first) {
                    x0 = x1 = lx;
                    y0 = y1 = ly;
                    z0 = z1 = lz;
                    first = false;
                    continue;
                }

                x0 = std::min(x0, lx); x1 = std::max(x1, lx);
                y0 = std::min(y0, ly); y1 = std::max(y1, ly);
                z0 = std::min(z0, lz); z1 = std::max(z1, lz);
            }

            if (first) {
                x0 = x1 = y0 = y1 = z0 = z1 = 0.0;
            }
        }

        // Parameters describing the desired stock cross-section (the axis is
        // always the part's X / rotary axis; length matches the part).
        struct StockParams {
            bool cylinder = false;
            double radius = 0.0;       // cylinder cross-section radius
            double halfWidth = 0.0;    // prism half extent along axis Y
            double halfHeight = 0.0;   // prism half extent along axis Z
        };

        // Build the four incremental stock-step solids that grow this part,
        // one side-face at a time (+Y, -Y, +Z, -Z around the axis), out to the
        // full stock boundary.  The last solid equals the full stock.
        //
        // Each step is the running clip of the full stock against a box that
        // has been extended on one more side — for a prism the clip is a no-op
        // (so steps are plain slabs), for a cylinder it rounds each slab to the
        // cylindrical surface.  All solids are returned in world space.
        std::vector<TopoDS_Shape> buildStockStepShapes(const StockParams& sp) const {

            std::vector<TopoDS_Shape> steps;

            if (shape.IsNull()) { return steps; }

            // Orthonormal axis frame.
            Rev::Core::Pos3 xDir, yDir, zDir;
            getOrthonormalAxisFrame(xDir, yDir, zDir);

            // Part bounds in that frame.  Use the (arbitrary) world origin as
            // the projection origin so vertex coordinates map cleanly; the
            // resulting local extents are what we build against.
            Rev::Core::Pos3 worldOrigin = { 0.0f, 0.0f, 0.0f };

            double x0, x1, y0, y1, z0, z1;
            frameBounds(worldOrigin, xDir, yDir, zDir, x0, x1, y0, y1, z0, z1);

            const double cy = (y0 + y1) * 0.5;
            const double cz = (z0 + z1) * 0.5;

            // Cross-section extents of the full stock.
            double Y0, Y1, Z0, Z1;

            if (sp.cylinder) {
                Y0 = cy - sp.radius; Y1 = cy + sp.radius;
                Z0 = cz - sp.radius; Z1 = cz + sp.radius;
            }
            else {
                Y0 = cy - sp.halfWidth;  Y1 = cy + sp.halfWidth;
                Z0 = cz - sp.halfHeight; Z1 = cz + sp.halfHeight;
            }

            const gp_Dir gx(xDir.x, xDir.y, xDir.z);
            const gp_Dir gy(yDir.x, yDir.y, yDir.z);
            const gp_Dir gz(zDir.x, zDir.y, zDir.z);

            auto worldPoint = [&](double lx, double ly, double lz) -> gp_Pnt {
                return gp_Pnt(
                    lx * xDir.x + ly * yDir.x + lz * zDir.x,
                    lx * xDir.y + ly * yDir.y + lz * zDir.y,
                    lx * xDir.z + ly * yDir.z + lz * zDir.z
                );
            };

            // Axis-aligned (in the frame) box spanning the given local ranges.
            auto makeBox = [&](
                double bx0, double bx1,
                double by0, double by1,
                double bz0, double bz1
            ) -> TopoDS_Shape {
                gp_Ax2 ax(worldPoint(bx0, by0, bz0), gz, gx);
                return BRepPrimAPI_MakeBox(
                    ax,
                    bx1 - bx0,
                    by1 - by0,
                    bz1 - bz0
                ).Shape();
            };

            // Build the bounding stock solid slightly LARGER than both the part and
            // the requested stock, so every clip box below sits STRICTLY inside it.
            // When the stock equals (or is smaller than) the part, an un-padded solid
            // is identical to a clip box and BRepAlgoAPI_Common operates on fully
            // COINCIDENT faces -- the classic OCC boolean failure, which throws a
            // Standard_Failure that (unguarded) crashed the app.  The clip boxes still
            // define the real stage geometry, so padding the bounding solid does not
            // change the result -- it just removes the coincidence.  `pad` is
            // geometric slop, far below any real tolerance.
            constexpr double pad = 0.01;

            try {
                TopoDS_Shape stockSolid;

                if (sp.cylinder) {
                    gp_Ax2 ax(worldPoint(x0 - pad, cy, cz), gx, gy);
                    stockSolid = BRepPrimAPI_MakeCylinder(
                        ax, sp.radius + pad, (x1 - x0) + 2.0 * pad).Shape();
                }
                else {
                    const double SY0 = std::min(Y0, y0) - pad, SY1 = std::max(Y1, y1) + pad;
                    const double SZ0 = std::min(Z0, z0) - pad, SZ1 = std::max(Z1, z1) + pad;
                    stockSolid = makeBox(x0 - pad, x1 + pad, SY0, SY1, SZ0, SZ1);
                }

                if (stockSolid.IsNull()) { return steps; }

                // Four progressively-extended clip boxes (the real stage shapes),
                // all strictly inside the padded solid.
                const double clip[4][6] = {
                    { x0, x1, y0, Y1, z0, z1 },  // extend +Y
                    { x0, x1, Y0, Y1, z0, z1 },  // extend -Y
                    { x0, x1, Y0, Y1, z0, Z1 },  // extend +Z
                    { x0, x1, Y0, Y1, Z0, Z1 },  // extend -Z (full)
                };

                for (int i = 0; i < 4; i++) {

                    TopoDS_Shape box = makeBox(
                        clip[i][0], clip[i][1],
                        clip[i][2], clip[i][3],
                        clip[i][4], clip[i][5]
                    );

                    if (box.IsNull()) { steps.clear(); return steps; }

                    BRepAlgoAPI_Common common(stockSolid, box);
                    common.Build();

                    if (!common.IsDone()) { steps.clear(); return steps; }

                    TopoDS_Shape result = common.Shape();

                    if (result.IsNull()) { steps.clear(); return steps; }

                    steps.push_back(result);
                }
            }
            catch (const Standard_Failure& failure) {
                // Never let an OpenCASCADE boolean failure crash the app (mirrors
                // Model::Difference) -- fail gracefully so the stock just isn't built.
                dbg("[Stock] buildStockStepShapes exception: %s", safeFailureMessage(failure));
                steps.clear();
                return steps;
            }

            return steps;
        }

        // STEP import
        //--------------------------------------------------

        static TopoDS_Shape loadStepShape(Rev::OS::File& file) {
            STEPControl_Reader reader;

            if (reader.ReadFile(file.string().c_str()) != IFSelect_RetDone) {
                throw std::runtime_error("Failed to read STEP file.");
            }

            reader.TransferRoots();

            TopoDS_Shape loadedShape = reader.OneShape();

            if (loadedShape.IsNull()) {
                throw std::runtime_error("STEP import produced null shape.");
            }

            return loadedShape;
        }

        void loadStep(Rev::OS::File& file) {
            logEvent(format("[STEP] loadStep requested: %s", file.string().c_str()));

            clear();

            try {
                TopoDS_Shape loadedShape = loadStepShape(file);
                adoptShape(loadedShape, false, "loadStep");
                logEvent("[STEP] loadStep succeeded");
            }
            catch (const std::exception& exception) {
                logEvent(format("[STEP] loadStep exception: %s", exception.what()));
                clear();
                throw;
            }
            catch (...) {
                logEvent("[STEP] loadStep unknown exception");
                clear();
                throw;
            }
        }

        // Scale a shape about the origin. A uniform scale uses a rigid gp_Trsf
        // (keeps analytic geometry); a non-uniform scale falls back to a general
        // transform. Returns the input unchanged for an ~identity scale.
        static TopoDS_Shape scaleShape(const TopoDS_Shape& shape, double sx, double sy, double sz) {

            if (shape.IsNull()) { return shape; }

            const bool identity =
                std::fabs(sx - 1.0) < 1e-9 &&
                std::fabs(sy - 1.0) < 1e-9 &&
                std::fabs(sz - 1.0) < 1e-9;

            if (identity) { return shape; }

            try {
                const bool uniform =
                    std::fabs(sx - sy) < 1e-9 && std::fabs(sy - sz) < 1e-9;

                if (uniform) {
                    gp_Trsf trsf;
                    trsf.SetScale(gp_Pnt(0, 0, 0), sx);
                    BRepBuilderAPI_Transform transform(shape, trsf, true);
                    transform.Build();
                    if (transform.IsDone()) { return transform.Shape(); }
                    return shape;
                }

                gp_GTrsf gtrsf;
                gtrsf.SetValue(1, 1, sx);
                gtrsf.SetValue(2, 2, sy);
                gtrsf.SetValue(3, 3, sz);

                BRepBuilderAPI_GTransform transform(shape, gtrsf, true);
                transform.Build();
                if (transform.IsDone()) { return transform.Shape(); }
                return shape;
            }
            catch (const Standard_Failure&) {
                return shape;
            }
        }

        // Load a STEP file and scale the result about the origin (per-axis).
        void loadStepScaled(Rev::OS::File& file, double sx, double sy, double sz) {

            logEvent(format("[STEP] loadStepScaled: %s scale=(%.4f, %.4f, %.4f)",
                file.string().c_str(), sx, sy, sz));

            clear();

            try {
                TopoDS_Shape loadedShape = loadStepShape(file);
                TopoDS_Shape scaled = scaleShape(loadedShape, sx, sy, sz);
                adoptShape(scaled, false, "loadStepScaled");
            }
            catch (const std::exception& exception) {
                logEvent(format("[STEP] loadStepScaled exception: %s", exception.what()));
                clear();
                throw;
            }
            catch (...) {
                logEvent("[STEP] loadStepScaled unknown exception");
                clear();
                throw;
            }
        }

        // Topology / render cache
        //--------------------------------------------------

        void collectFaces() {
            faces.clear();

            if (shape.IsNull()) {
                dbg("[Topology] collectFaces: shape is null");
                return;
            }

            for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
                faces.push_back(TopoDS::Face(exp.Current()));
            }

            rebuildFaceHandles();

            dbg("[Topology] collectFaces: collected %zu face(s)", faces.size());
        }

        // Rebuild the per-face handle store in lockstep with `faces`.
        void rebuildFaceHandles() {
            faceHandles.clear();
            faceHandles.reserve(faces.size());
            for (size_t i = 0; i < faces.size(); i++) {
                faceHandles.push_back(Face{ this, i });
            }
        }

        // A by-value handle to face `id`. Always carries this model, so it is
        // valid even right after a copy (faceHandles' own back-pointers may be
        // stale after a memberwise copy — always go through here, never read a
        // Face's model from faceHandles directly).
        Face face(size_t id) { return Face{ this, id }; }

        // The face that render triangle `tri` belongs to (invalid if out of range).
        Face triangleFace(size_t tri) {
            if (tri >= render.triangleFaceIds.size()) {
                return Face{ this, static_cast<size_t>(-1) };
            }
            return Face{ this, render.triangleFaceIds[tri] };
        }

        void tessellate(double tolerance = 0.1) {
            render.clear();

            if (shape.IsNull()) {
                dbg("[Tessellate] skipped: shape is null");
                return;
            }

            try {
                BRepMesh_IncrementalMesh mesher(shape, tolerance, false, 0.5, true);
                mesher.Perform();
            }
            catch (const Standard_Failure& failure) {
                dbg("[Tessellate] meshing exception: %s", safeFailureMessage(failure));
                return;
            }
            catch (...) {
                dbg("[Tessellate] unknown meshing exception");
                return;
            }

            Rev::Core::Color defaultColor = { 0.0f, 0.0f, 0.0f, 0.0f };

            for (size_t faceId = 0; faceId < faces.size(); faceId++) {
                appendFaceTriangles(faceId, defaultColor);
            }

            render.valid = true;

            dbg(
                "[Tessellate] completed: faces=%zu triangles=%zu triangleFaceIds=%zu",
                faces.size(),
                render.triangles.size() / 3,
                render.triangleFaceIds.size()
            );
        }

        // Modeling operations
        //--------------------------------------------------

        TopoDS_Shape healShape(TopoDS_Shape input) const {
            if (input.IsNull()) {
                dbg("[Heal] skipped: input is null");
                return input;
            }

            logShapeStatsStatic("[Heal] input", input);

            try {
                BRepBuilderAPI_Sewing sewer(1e-6, Standard_True, Standard_True, Standard_True, Standard_True);
                sewer.Add(input);
                sewer.Perform();

                TopoDS_Shape sewed = sewer.SewedShape();

                if (!sewed.IsNull()) {
                    input = firstSolidOrSelf(sewed);
                    logShapeStatsStatic("[Heal] after sewing", input);
                }
                else {
                    dbg("[Heal] sewing produced null shape; keeping input");
                }

                ShapeUpgrade_UnifySameDomain unifier(input, true, true, true);
                unifier.Build();

                TopoDS_Shape unified = unifier.Shape();

                if (!unified.IsNull()) {
                    input = unified;
                    logShapeStatsStatic("[Heal] after unify same domain", input);
                }
                else {
                    dbg("[Heal] unifier produced null shape; keeping previous");
                }
            }
            catch (const Standard_Failure& failure) {
                dbg("[Heal] exception: %s", safeFailureMessage(failure));
            }
            catch (...) {
                dbg("[Heal] unknown exception");
            }

            logShapeStatsStatic("[Heal] result", input);

            return input;
        }

        bool defeatureSelected() {
            operationSerial++;

            logEvent(format(
                "[Defeature #%zu] requested: loaded=%d changed=%d shapeNull=%d faceCount=%zu selectedCount=%zu selected={%s}",
                operationSerial,
                loaded,
                changed,
                shape.IsNull(),
                faces.size(),
                selectedFaceIds.size(),
                selectedFaceIdList().c_str()
            ));

            logShapeStats("[Defeature] shape before", shape);

            if (shape.IsNull()) {
                logEvent(format("[Defeature #%zu] failed: model shape is null", operationSerial));
                return false;
            }

            if (selectedFaceIds.empty()) {
                logEvent(format("[Defeature #%zu] failed: selectedFaceIds is empty", operationSerial));
                dumpRecentHistory("[Defeature] recent history because selection was empty");
                return false;
            }

            std::vector<size_t> validFaceIds;

            for (size_t faceId : selectedFaceIds) {
                if (faceId >= faces.size()) {
                    logEvent(format(
                        "[Defeature #%zu] skipping stale selected face id %zu (faceCount=%zu)",
                        operationSerial,
                        faceId,
                        faces.size()
                    ));
                    continue;
                }

                validFaceIds.push_back(faceId);
            }

            if (validFaceIds.empty()) {
                logEvent(format(
                    "[Defeature #%zu] failed: all selected face ids were stale; selected={%s}, faceCount=%zu",
                    operationSerial,
                    selectedFaceIdList().c_str(),
                    faces.size()
                ));
                dumpRecentHistory("[Defeature] recent history because all selected ids were stale");
                return false;
            }

            BRepAlgoAPI_Defeaturing defeature;
            defeature.SetShape(shape);

            size_t added = 0;

            for (size_t faceId : validFaceIds) {
                const TopoDS_Face& face = faces[faceId];

                if (face.IsNull()) {
                    logEvent(format("[Defeature #%zu] skipping null face id %zu", operationSerial, faceId));
                    continue;
                }

                double area = faceArea(face);

                logEvent(format(
                    "[Defeature #%zu] adding face id %zu area=%.9f orientation=%d",
                    operationSerial,
                    faceId,
                    area,
                    static_cast<int>(face.Orientation())
                ));

                defeature.AddFaceToRemove(face);
                added++;
            }

            if (added == 0) {
                logEvent(format("[Defeature #%zu] failed: no valid non-null faces were added to defeaturing", operationSerial));
                dumpRecentHistory("[Defeature] recent history because AddFaceToRemove received no faces");
                return false;
            }

            try {
                logEvent(format("[Defeature #%zu] Build starting: addedFaces=%zu", operationSerial, added));

                defeature.Build();

                logEvent(format("[Defeature #%zu] Build completed: IsDone=%d", operationSerial, defeature.IsDone()));

                if (!defeature.IsDone()) {
                    logEvent(format("[Defeature #%zu] failed: BRepAlgoAPI_Defeaturing IsDone=false", operationSerial));
                    dumpRecentHistory("[Defeature] recent history because IsDone=false");
                    return false;
                }

                TopoDS_Shape rawResult = defeature.Shape();

                if (rawResult.IsNull()) {
                    logEvent(format("[Defeature #%zu] failed: defeature.Shape() is null", operationSerial));
                    dumpRecentHistory("[Defeature] recent history because result was null");
                    return false;
                }

                logShapeStats("[Defeature] raw result", rawResult);

                TopoDS_Shape chosenResult = rawResult;

                if (!isShapeValid(rawResult)) {
                    logEvent(format("[Defeature #%zu] raw result is invalid; trying healShape fallback", operationSerial));

                    TopoDS_Shape healed = healShape(rawResult);

                    if (healed.IsNull()) {
                        logEvent(format("[Defeature #%zu] failed: heal fallback produced null shape", operationSerial));
                        dumpRecentHistory("[Defeature] recent history because heal fallback was null");
                        return false;
                    }

                    logShapeStats("[Defeature] healed fallback result", healed);

                    if (!isShapeValid(healed)) {
                        logEvent(format("[Defeature #%zu] failed: raw result and healed fallback are both invalid", operationSerial));
                        dumpRecentHistory("[Defeature] recent history because raw/healed were invalid");
                        return false;
                    }

                    chosenResult = healed;
                }
                else {
                    logEvent(format(
                        "[Defeature #%zu] raw result is valid; adopting raw result without heal/unify to preserve future defeature stability",
                        operationSerial
                    ));
                }

                adoptShape(chosenResult, true, format("Defeature #%zu", operationSerial));

                logEvent(format(
                    "[Defeature #%zu] succeeded: newFaceCount=%zu renderTriangles=%zu selectedAfter=%zu",
                    operationSerial,
                    faces.size(),
                    render.triangles.size() / 3,
                    selectedFaceIds.size()
                ));

                return true;
            }
            catch (const Standard_Failure& failure) {
                logEvent(format("[Defeature #%zu] exception: %s", operationSerial, safeFailureMessage(failure)));
                dumpRecentHistory("[Defeature] recent history because Standard_Failure was caught");
                return false;
            }
            catch (const std::exception& exception) {
                logEvent(format("[Defeature #%zu] std::exception: %s", operationSerial, exception.what()));
                dumpRecentHistory("[Defeature] recent history because std::exception was caught");
                return false;
            }
            catch (...) {
                logEvent(format("[Defeature #%zu] unknown exception", operationSerial));
                dumpRecentHistory("[Defeature] recent history because unknown exception was caught");
                return false;
            }
        }

        // Extend (grow) the selected face(s) outward along their normals by
        // `distance`, adding material.  Each selected face is extruded into a
        // prism and fused onto the body; the union is then healed (sewn and
        // coplanar faces unified) so the extended sides merge into single faces.
        bool extendSelected(double distance = 10.0) {
            operationSerial++;

            logEvent(format(
                "[Extend #%zu] requested: distance=%.6f loaded=%d changed=%d shapeNull=%d faceCount=%zu selectedCount=%zu selected={%s}",
                operationSerial,
                distance,
                loaded,
                changed,
                shape.IsNull(),
                faces.size(),
                selectedFaceIds.size(),
                selectedFaceIdList().c_str()
            ));

            logShapeStats("[Extend] shape before", shape);

            if (shape.IsNull()) {
                logEvent(format("[Extend #%zu] failed: model shape is null", operationSerial));
                return false;
            }

            if (selectedFaceIds.empty()) {
                logEvent(format("[Extend #%zu] failed: selectedFaceIds is empty", operationSerial));
                dumpRecentHistory("[Extend] recent history because selection was empty");
                return false;
            }

            if (distance <= 0.0) {
                logEvent(format("[Extend #%zu] failed: non-positive distance %.6f", operationSerial, distance));
                return false;
            }

            std::vector<size_t> validFaceIds;

            for (size_t faceId : selectedFaceIds) {
                if (faceId >= faces.size()) {
                    logEvent(format(
                        "[Extend #%zu] skipping stale selected face id %zu (faceCount=%zu)",
                        operationSerial,
                        faceId,
                        faces.size()
                    ));
                    continue;
                }

                validFaceIds.push_back(faceId);
            }

            if (validFaceIds.empty()) {
                logEvent(format(
                    "[Extend #%zu] failed: all selected face ids were stale; selected={%s}, faceCount=%zu",
                    operationSerial,
                    selectedFaceIdList().c_str(),
                    faces.size()
                ));
                dumpRecentHistory("[Extend] recent history because all selected ids were stale");
                return false;
            }

            try {
                // Boolean fuse requires a solid; a prior heal may have left the
                // working model as a shell, so restore solidity up front.
                TopoDS_Shape accum = ensureSolid(shape);
                size_t fused = 0;

                for (size_t faceId : validFaceIds) {
                    const TopoDS_Face& face = faces[faceId];

                    if (face.IsNull()) {
                        logEvent(format("[Extend #%zu] skipping null face id %zu", operationSerial, faceId));
                        continue;
                    }

                    Rev::Core::Pos3 normal = faceNormal(faceId);
                    gp_Vec direction(normal.x, normal.y, normal.z);

                    if (direction.Magnitude() <= 1e-9) {
                        logEvent(format("[Extend #%zu] skipping face id %zu: degenerate normal", operationSerial, faceId));
                        continue;
                    }

                    direction.Normalize();
                    direction *= distance;

                    logEvent(format(
                        "[Extend #%zu] extruding face id %zu area=%.9f along normal=(%.4f %.4f %.4f) by %.4f",
                        operationSerial,
                        faceId,
                        faceArea(face),
                        normal.x,
                        normal.y,
                        normal.z,
                        distance
                    ));

                    // Force FORWARD orientation so the swept prism is a
                    // well-oriented solid.  faceNormal already returned the true
                    // world-space outward normal, so the sweep direction is
                    // unaffected by this re-orientation.
                    TopoDS_Face forwardFace = TopoDS::Face(face.Oriented(TopAbs_FORWARD));

                    BRepPrimAPI_MakePrism prism(forwardFace, direction);
                    prism.Build();

                    if (!prism.IsDone()) {
                        logEvent(format("[Extend #%zu] failed: prism IsDone=false for face id %zu", operationSerial, faceId));
                        return false;
                    }

                    TopoDS_Shape slab = prism.Shape();

                    if (slab.IsNull()) {
                        logEvent(format("[Extend #%zu] failed: prism produced null slab for face id %zu", operationSerial, faceId));
                        return false;
                    }

                    BRepAlgoAPI_Fuse fuse(accum, slab);
                    fuse.Build();

                    if (!fuse.IsDone()) {
                        logEvent(format("[Extend #%zu] failed: fuse IsDone=false for face id %zu", operationSerial, faceId));
                        return false;
                    }

                    TopoDS_Shape fusedShape = fuse.Shape();

                    if (fusedShape.IsNull()) {
                        logEvent(format("[Extend #%zu] failed: fuse produced null shape for face id %zu", operationSerial, faceId));
                        return false;
                    }

                    accum = fusedShape;
                    fused++;
                }

                if (fused == 0) {
                    logEvent(format("[Extend #%zu] failed: no valid faces were extruded/fused", operationSerial));
                    dumpRecentHistory("[Extend] recent history because no faces were fused");
                    return false;
                }

                logShapeStats("[Extend] raw fused result", accum);

                // Merge the coplanar faces the fuse leaves at each seam so the
                // extended sides become single planar faces again.  Crucially we
                // unify WITHOUT sewing: sewing demotes the solid to a shell,
                // which makes the next fuse fail.  Keep the result a solid.
                TopoDS_Shape chosenResult = accum;

                try {
                    ShapeUpgrade_UnifySameDomain unifier(accum, true, true, true);
                    unifier.Build();

                    TopoDS_Shape unified = unifier.Shape();

                    if (!unified.IsNull() && isShapeValid(unified)) {
                        chosenResult = unified;
                    }
                }
                catch (const Standard_Failure& failure) {
                    logEvent(format(
                        "[Extend #%zu] unify exception (keeping raw fuse): %s",
                        operationSerial,
                        safeFailureMessage(failure)
                    ));
                }

                chosenResult = ensureSolid(chosenResult);

                if (!isShapeValid(chosenResult)) {
                    logEvent(format("[Extend #%zu] failed: result invalid after unify", operationSerial));
                    dumpRecentHistory("[Extend] recent history because result was invalid");
                    return false;
                }

                adoptShape(chosenResult, true, format("Extend #%zu", operationSerial));

                logEvent(format(
                    "[Extend #%zu] succeeded: newFaceCount=%zu renderTriangles=%zu selectedAfter=%zu",
                    operationSerial,
                    faces.size(),
                    render.triangles.size() / 3,
                    selectedFaceIds.size()
                ));

                return true;
            }
            catch (const Standard_Failure& failure) {
                logEvent(format("[Extend #%zu] exception: %s", operationSerial, safeFailureMessage(failure)));
                dumpRecentHistory("[Extend] recent history because Standard_Failure was caught");
                return false;
            }
            catch (const std::exception& exception) {
                logEvent(format("[Extend #%zu] std::exception: %s", operationSerial, exception.what()));
                dumpRecentHistory("[Extend] recent history because std::exception was caught");
                return false;
            }
            catch (...) {
                logEvent(format("[Extend #%zu] unknown exception", operationSerial));
                dumpRecentHistory("[Extend] recent history because unknown exception was caught");
                return false;
            }
        }

        // Extrude the profile face normal to itself and fuse the swept solid
        // into the part. The sweep ends on a plane `offset` mm from the origin
        // face (measured along the profile normal):
        //
        //   distance = (originPoint - profilePoint) . profileNormal + offset
        //
        // When the origin face IS the profile face, the projection term is ~0,
        // so the result is a plain extrude by `offset`. When offset is 0 and the
        // origin is some other face, the prism reaches that face's plane exactly.
        // This single entry point covers both "to a face" and "by a distance".
        bool extrudeToFaceOffset(size_t profileFaceId, size_t originFaceId, double offset) {
            operationSerial++;

            if (shape.IsNull()) {
                logEvent(format("[Extrude #%zu] failed: shape is null", operationSerial));
                return false;
            }

            if (profileFaceId >= faces.size() || originFaceId >= faces.size()) {
                logEvent(format(
                    "[Extrude #%zu] failed: stale face ids profile=%zu origin=%zu faceCount=%zu",
                    operationSerial, profileFaceId, originFaceId, faces.size()
                ));
                return false;
            }

            const TopoDS_Face& profile = faces[profileFaceId];

            if (profile.IsNull()) {
                logEvent(format("[Extrude #%zu] failed: profile face is null", operationSerial));
                return false;
            }

            Rev::Core::Pos3 normal = faceNormal(profileFaceId);
            gp_Vec direction(normal.x, normal.y, normal.z);

            if (direction.Magnitude() <= 1e-9) {
                logEvent(format("[Extrude #%zu] failed: degenerate profile normal", operationSerial));
                return false;
            }

            direction.Normalize();

            const Rev::Core::Pos3 p0 = facePoint(profileFaceId);
            const Rev::Core::Pos3 pOrigin = facePoint(originFaceId);

            const double base =
                (double(pOrigin.x) - p0.x) * normal.x +
                (double(pOrigin.y) - p0.y) * normal.y +
                (double(pOrigin.z) - p0.z) * normal.z;

            const double distance = base + offset;

            if (std::fabs(distance) <= 1e-7) {
                logEvent(format("[Extrude #%zu] failed: zero sweep distance", operationSerial));
                return false;
            }

            gp_Vec sweep = direction * distance;

            return extrudeProfileSweep(profileFaceId, sweep, distance);
        }

        // Shared prism + fuse core for the extrude operations. Assumes
        // profileFaceId is in range and sweep is non-degenerate (the callers
        // validate). distanceForLog is purely cosmetic in the success message.
        bool extrudeProfileSweep(size_t profileFaceId, const gp_Vec& sweep, double distanceForLog) {

            const TopoDS_Face& profile = faces[profileFaceId];

            try {
                TopoDS_Shape accum = ensureSolid(shape);

                TopoDS_Face forwardFace = TopoDS::Face(profile.Oriented(TopAbs_FORWARD));

                BRepPrimAPI_MakePrism prism(forwardFace, sweep);
                prism.Build();

                if (!prism.IsDone()) {
                    logEvent(format("[Extrude #%zu] failed: prism IsDone=false", operationSerial));
                    return false;
                }

                TopoDS_Shape slab = prism.Shape();

                if (slab.IsNull()) {
                    logEvent(format("[Extrude #%zu] failed: null swept solid", operationSerial));
                    return false;
                }

                BRepAlgoAPI_Fuse fuse(accum, slab);
                fuse.Build();

                if (!fuse.IsDone()) {
                    logEvent(format("[Extrude #%zu] failed: fuse IsDone=false", operationSerial));
                    return false;
                }

                TopoDS_Shape result = fuse.Shape();

                if (result.IsNull()) {
                    logEvent(format("[Extrude #%zu] failed: null fused result", operationSerial));
                    return false;
                }

                TopoDS_Shape chosen = result;

                try {
                    ShapeUpgrade_UnifySameDomain unifier(result, true, true, true);
                    unifier.Build();
                    TopoDS_Shape unified = unifier.Shape();
                    if (!unified.IsNull() && isShapeValid(unified)) { chosen = unified; }
                }
                catch (const Standard_Failure&) {}

                chosen = ensureSolid(chosen);

                if (!isShapeValid(chosen)) {
                    logEvent(format("[Extrude #%zu] failed: invalid result after unify", operationSerial));
                    return false;
                }

                adoptShape(chosen, true, format("Extrude #%zu", operationSerial));

                logEvent(format(
                    "[Extrude #%zu] succeeded: distance=%.4f newFaceCount=%zu",
                    operationSerial, distanceForLog, faces.size()
                ));

                return true;
            }
            catch (const Standard_Failure& failure) {
                logEvent(format("[Extrude #%zu] exception: %s", operationSerial, safeFailureMessage(failure)));
                return false;
            }
            catch (...) {
                logEvent(format("[Extrude #%zu] unknown exception", operationSerial));
                return false;
            }
        }

        // Thread-mill prep: reduce a cylindrical hole to its PRE-THREAD bore by
        // fusing an annular collar into it.  We deliberately do NOT model the
        // thread geometry (it is uniform and defined entirely by the callout);
        // we leave the solid that exists AFTER the pre-thread bore is drilled
        // but BEFORE the thread is milled.  So the resulting hole equals
        // preBoreDiameter -- a plain bore a later drilling step can address --
        // and the thread itself is cut by the Thread Mill toolpath from the
        // callout.  The selected face must be the (major-diameter) cylindrical
        // hole as drawn.
        bool threadMillInfill(size_t holeFaceId, double preBoreDiameter) {
            operationSerial++;

            if (shape.IsNull()) {
                logEvent(format("[ThreadMill #%zu] failed: shape is null", operationSerial));
                return false;
            }

            if (holeFaceId >= faces.size()) {
                logEvent(format(
                    "[ThreadMill #%zu] failed: stale hole face id=%zu faceCount=%zu",
                    operationSerial, holeFaceId, faces.size()
                ));
                return false;
            }

            const TopoDS_Face& face = faces[holeFaceId];

            if (face.IsNull()) {
                logEvent(format("[ThreadMill #%zu] failed: hole face is null", operationSerial));
                return false;
            }

            BRepAdaptor_Surface surf(face);

            if (surf.GetType() != GeomAbs_Cylinder) {
                logEvent(format("[ThreadMill #%zu] failed: selected face is not cylindrical", operationSerial));
                return false;
            }

            const gp_Cylinder cyl = surf.Cylinder();
            const double holeRadius    = cyl.Radius();
            const double preBoreRadius = preBoreDiameter * 0.5;

            if (preBoreRadius <= 1e-6 || preBoreRadius >= holeRadius - 1e-6) {
                logEvent(format(
                    "[ThreadMill #%zu] failed: pre-bore dia %.4f must be > 0 and smaller than the hole dia %.4f",
                    operationSerial, preBoreDiameter, holeRadius * 2.0
                ));
                return false;
            }

            // The hole's axial extent: a cylinder's V parameter runs along its
            // axis from the cylinder's Location.
            const double vFirst = surf.FirstVParameter();
            const double vLast  = surf.LastVParameter();
            const double height = std::fabs(vLast - vFirst);

            if (height <= 1e-6) {
                logEvent(format("[ThreadMill #%zu] failed: degenerate hole height", operationSerial));
                return false;
            }

            const gp_Ax3 pos     = cyl.Position();
            const gp_Dir axisDir = pos.Direction();
            const gp_Pnt base    = pos.Location().Translated(gp_Vec(axisDir) * std::min(vFirst, vLast));
            const gp_Ax2 ax(base, axisDir);

            try {
                // The collar: a tube from the pre-bore radius out to just past
                // the hole wall, so its outer face merges into the existing
                // wall on fusion (the tiny overlap keeps the boolean robust
                // without ever reaching the part's outer surface).
                const double eps = 1e-3;

                TopoDS_Shape outer = BRepPrimAPI_MakeCylinder(ax, holeRadius + eps, height).Shape();
                TopoDS_Shape inner = BRepPrimAPI_MakeCylinder(ax, preBoreRadius, height).Shape();

                BRepAlgoAPI_Cut cut(outer, inner);
                cut.Build();

                if (!cut.IsDone()) {
                    logEvent(format("[ThreadMill #%zu] failed: collar cut IsDone=false", operationSerial));
                    return false;
                }

                TopoDS_Shape collar = cut.Shape();

                if (collar.IsNull()) {
                    logEvent(format("[ThreadMill #%zu] failed: null collar", operationSerial));
                    return false;
                }

                TopoDS_Shape accum = ensureSolid(shape);

                BRepAlgoAPI_Fuse fuse(accum, collar);
                fuse.Build();

                if (!fuse.IsDone()) {
                    logEvent(format("[ThreadMill #%zu] failed: fuse IsDone=false", operationSerial));
                    return false;
                }

                TopoDS_Shape result = fuse.Shape();

                if (result.IsNull()) {
                    logEvent(format("[ThreadMill #%zu] failed: null fused result", operationSerial));
                    return false;
                }

                TopoDS_Shape chosen = result;

                try {
                    ShapeUpgrade_UnifySameDomain unifier(result, true, true, true);
                    unifier.Build();
                    TopoDS_Shape unified = unifier.Shape();
                    if (!unified.IsNull() && isShapeValid(unified)) { chosen = unified; }
                }
                catch (const Standard_Failure&) {}

                chosen = ensureSolid(chosen);

                if (!isShapeValid(chosen)) {
                    logEvent(format("[ThreadMill #%zu] failed: invalid result after unify", operationSerial));
                    return false;
                }

                adoptShape(chosen, true, format("ThreadMill #%zu", operationSerial));

                logEvent(format(
                    "[ThreadMill #%zu] succeeded: hole dia %.4f -> pre-bore dia %.4f over %.4f mm",
                    operationSerial, holeRadius * 2.0, preBoreDiameter, height
                ));

                return true;
            }
            catch (const Standard_Failure& failure) {
                logEvent(format("[ThreadMill #%zu] exception: %s", operationSerial, safeFailureMessage(failure)));
                return false;
            }
            catch (...) {
                logEvent(format("[ThreadMill #%zu] unknown exception", operationSerial));
                return false;
            }
        }

        std::string debugDump() const {
            std::ostringstream stream;

            stream
                << "Model debug dump\n"
                << "loaded=" << loaded
                << " changed=" << changed
                << " shapeNull=" << shape.IsNull()
                << " faces=" << faces.size()
                << " selectedCount=" << selectedFaceIds.size()
                << " selected={" << selectedFaceIdList() << "}"
                << " render.valid=" << render.valid
                << " render.triangles=" << (render.triangles.size() / 3)
                << " render.triangleFaceIds=" << render.triangleFaceIds.size()
                << "\n";

            ShapeStats stats = computeShapeStats(shape);

            stream
                << "shape stats: "
                << "valid=" << stats.valid
                << " compounds=" << stats.compounds
                << " solids=" << stats.solids
                << " shells=" << stats.shells
                << " faces=" << stats.faces
                << " edges=" << stats.edges
                << " vertices=" << stats.vertices
                << " volume=" << stats.volume
                << "\n";

            stream << "recent history:\n";

            for (const std::string& line : history) {
                stream << "  " << line << "\n";
            }

            return stream.str();
        }

        void dumpDebug() const {
            std::string dump = debugDump();
            dbg("%s", dump.c_str());
        }

    private:

        void adoptShape(const TopoDS_Shape& nextShape, bool markChanged, const std::string& reason) {
            logEvent(format(
                "[Adopt] requested: reason=%s nextShapeNull=%d markChanged=%d oldFaces=%zu oldSelected=%zu",
                reason.c_str(),
                nextShape.IsNull(),
                markChanged,
                faces.size(),
                selectedFaceIds.size()
            ));

            logShapeStats("[Adopt] incoming shape", nextShape);

            shape = nextShape;

            refreshTopologyAndRender(reason);

            clearSelection();

            loaded = !shape.IsNull();
            changed = markChanged;

            logEvent(format(
                "[Adopt] completed: reason=%s loaded=%d changed=%d faces=%zu selected=%zu renderTriangles=%zu",
                reason.c_str(),
                loaded,
                changed,
                faces.size(),
                selectedFaceIds.size(),
                render.triangles.size() / 3
            ));
        }

        void refreshTopologyAndRender(const std::string& reason) {
            logEvent(format("[Refresh] requested: reason=%s", reason.c_str()));

            collectFaces();
            tessellate();

            logEvent(format(
                "[Refresh] completed: reason=%s faces=%zu renderValid=%d triangles=%zu triangleFaceIds=%zu",
                reason.c_str(),
                faces.size(),
                render.valid,
                render.triangles.size() / 3,
                render.triangleFaceIds.size()
            ));
        }

        static TopoDS_Shape firstSolidOrSelf(const TopoDS_Shape& input) {
            for (TopExp_Explorer exp(input, TopAbs_SOLID); exp.More(); exp.Next()) {
                return exp.Current();
            }

            return input;
        }

        static bool isShapeValid(const TopoDS_Shape& input) {
            if (input.IsNull()) { return false; }

            try {
                BRepCheck_Analyzer analyzer(input);
                return analyzer.IsValid();
            }
            catch (...) {
                return false;
            }
        }

        // Promote a shell (or shells) to a solid.  Boolean ops require solids;
        // sewing-based healing can demote a solid to a shell, so we restore it.
        static TopoDS_Shape ensureSolid(const TopoDS_Shape& input) {
            if (input.IsNull()) { return input; }

            // Already has a solid — leave it alone.
            for (TopExp_Explorer ex(input, TopAbs_SOLID); ex.More(); ex.Next()) {
                return input;
            }

            try {
                BRepBuilderAPI_MakeSolid maker;
                bool anyShell = false;

                for (TopExp_Explorer ex(input, TopAbs_SHELL); ex.More(); ex.Next()) {
                    maker.Add(TopoDS::Shell(ex.Current()));
                    anyShell = true;
                }

                if (anyShell) {
                    maker.Build();

                    if (maker.IsDone()) {
                        TopoDS_Shape solid = maker.Solid();
                        if (!solid.IsNull()) { return solid; }
                    }
                }
            }
            catch (...) {
            }

            return input;
        }

        static double faceArea(const TopoDS_Face& face) {
            if (face.IsNull()) { return 0.0; }

            try {
                GProp_GProps props;
                BRepGProp::SurfaceProperties(face, props);
                return props.Mass();
            }
            catch (...) {
                return 0.0;
            }
        }

        static ShapeStats computeShapeStats(const TopoDS_Shape& input) {
            ShapeStats stats;

            if (input.IsNull()) {
                stats.valid = false;
                return stats;
            }

            TopTools_IndexedMapOfShape compounds;
            TopTools_IndexedMapOfShape solids;
            TopTools_IndexedMapOfShape shells;
            TopTools_IndexedMapOfShape faces;
            TopTools_IndexedMapOfShape edges;
            TopTools_IndexedMapOfShape vertices;

            TopExp::MapShapes(input, TopAbs_COMPOUND, compounds);
            TopExp::MapShapes(input, TopAbs_SOLID, solids);
            TopExp::MapShapes(input, TopAbs_SHELL, shells);
            TopExp::MapShapes(input, TopAbs_FACE, faces);
            TopExp::MapShapes(input, TopAbs_EDGE, edges);
            TopExp::MapShapes(input, TopAbs_VERTEX, vertices);

            stats.compounds = compounds.Extent();
            stats.solids = solids.Extent();
            stats.shells = shells.Extent();
            stats.faces = faces.Extent();
            stats.edges = edges.Extent();
            stats.vertices = vertices.Extent();
            stats.valid = isShapeValid(input);

            try {
                GProp_GProps props;
                BRepGProp::VolumeProperties(input, props);
                stats.volume = props.Mass();
            }
            catch (...) {
                stats.volume = 0.0;
            }

            return stats;
        }

        void logShapeStats(const char* label, const TopoDS_Shape& input) const {
            logShapeStatsStatic(label, input);
        }

        static void logShapeStatsStatic(const char* label, const TopoDS_Shape& input) {
            ShapeStats stats = computeShapeStats(input);

            dbg(
                "%s: null=%d valid=%d compounds=%d solids=%d shells=%d faces=%d edges=%d vertices=%d volume=%.9f",
                label,
                input.IsNull(),
                stats.valid,
                stats.compounds,
                stats.solids,
                stats.shells,
                stats.faces,
                stats.edges,
                stats.vertices,
                stats.volume
            );
        }

        void logEvent(const std::string& message) {
            dbg("%s", message.c_str());

            history.push_back(message);

            constexpr size_t maxHistory = 80;

            if (history.size() > maxHistory) {
                history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - maxHistory));
            }
        }

        void dumpRecentHistory(const char* label) const {
            dbg("%s", label);

            for (const std::string& line : history) {
                dbg("  %s", line.c_str());
            }
        }

        std::string selectedFaceIdList() const {
            std::ostringstream stream;

            bool first = true;

            for (size_t faceId : selectedFaceIds) {
                if (!first) {
                    stream << ", ";
                }

                stream << faceId;

                if (faceId >= faces.size()) {
                    stream << "(stale)";
                }

                first = false;
            }

            return stream.str();
        }

        static const char* safeFailureMessage(const Standard_Failure& failure) {
            const char* message = failure.GetMessageString();
            return message != nullptr ? message : "(no message)";
        }

        template <typename... Args>
        static std::string format(const char* fmt, Args... args) {
            int count = std::snprintf(nullptr, 0, fmt, args...);

            if (count <= 0) {
                return std::string(fmt);
            }

            std::string text(static_cast<size_t>(count), '\0');
            std::snprintf(text.data(), text.size() + 1, fmt, args...);

            return text;
        }

        void appendFaceTriangles(size_t faceId, Rev::Core::Color color) {
            if (faceId >= faces.size()) {
                dbg("[Tessellate] appendFaceTriangles skipped stale faceId=%zu faceCount=%zu", faceId, faces.size());
                return;
            }

            TopoDS_Face& face = faces[faceId];
            TopLoc_Location loc;

            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);

            if (tri.IsNull()) {
                dbg("[Tessellate] face %zu has no triangulation", faceId);
                return;
            }

            gp_Trsf trsf = loc.Transformation();

            for (int i = 1; i <= tri->NbTriangles(); i++) {
                appendTriangle(faceId, face, tri, trsf, i, color);
            }
        }

        void appendTriangle(
            size_t faceId,
            const TopoDS_Face& face,
            const Handle(Poly_Triangulation)& tri,
            const gp_Trsf& trsf,
            int triangleIndex,
            Rev::Core::Color color
        ) {
            Poly_Triangle triangle = tri->Triangle(triangleIndex);

            int i1 = 0;
            int i2 = 0;
            int i3 = 0;

            triangle.Get(i1, i2, i3);

            gp_Pnt p1 = tri->Node(i1).Transformed(trsf);
            gp_Pnt p2 = tri->Node(i2).Transformed(trsf);
            gp_Pnt p3 = tri->Node(i3).Transformed(trsf);

            gp_Vec normal = triangleNormal(p1, p2, p3);

            if (normal.Magnitude() <= 1e-12) {
                return;
            }

            normal.Normalize();

            if (face.Orientation() == TopAbs_REVERSED) {
                normal.Reverse();
                appendRenderTriangle(faceId, p1, p3, p2, normal, color);
                return;
            }

            appendRenderTriangle(faceId, p1, p2, p3, normal, color);
        }

        void appendRenderTriangle(
            size_t faceId,
            const gp_Pnt& p1,
            const gp_Pnt& p2,
            const gp_Pnt& p3,
            const gp_Vec& normal,
            Rev::Core::Color color
        ) {
            render.triangles.push_back(makeVertex(p1, color, normal));
            render.triangles.push_back(makeVertex(p2, color, normal));
            render.triangles.push_back(makeVertex(p3, color, normal));
            render.triangleFaceIds.push_back(faceId);
        }

        static gp_Vec triangleNormal(const gp_Pnt& p1, const gp_Pnt& p2, const gp_Pnt& p3) {
            return gp_Vec(p1, p2).Crossed(gp_Vec(p1, p3));
        }

        static Rev::Core::Vertex3 makeVertex(const gp_Pnt& point, Rev::Core::Color color, const gp_Vec& normal) {
            return Rev::Core::Vertex3(makePos3(point), color, makePos3(normal));
        }

        static Rev::Core::Pos3 makePos3(const gp_Pnt& point) {
            return Rev::Core::Pos3(
                static_cast<float>(point.X()),
                static_cast<float>(point.Y()),
                static_cast<float>(point.Z())
            );
        }

        static Rev::Core::Pos3 makePos3(const gp_Vec& vector) {
            return Rev::Core::Pos3(
                static_cast<float>(vector.X()),
                static_cast<float>(vector.Y()),
                static_cast<float>(vector.Z())
            );
        }
    };
}