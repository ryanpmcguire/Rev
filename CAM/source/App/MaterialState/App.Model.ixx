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
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
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
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
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

        TopoDS_Shape shape;
        std::vector<TopoDS_Face> faces;
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

            dbg("[Topology] collectFaces: collected %zu face(s)", faces.size());
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
                TopoDS_Shape accum = shape;
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

                // Heal: sew + unify coplanar faces so the extended sides merge
                // back into single planar faces rather than seamed pairs.
                TopoDS_Shape healed = healShape(accum);

                TopoDS_Shape chosenResult = accum;

                if (!healed.IsNull() && isShapeValid(healed)) {
                    chosenResult = healed;
                }
                else if (!isShapeValid(accum)) {
                    logEvent(format("[Extend #%zu] failed: fused result invalid and heal fallback invalid", operationSerial));
                    dumpRecentHistory("[Extend] recent history because raw/healed were invalid");
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