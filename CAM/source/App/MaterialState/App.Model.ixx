module;

#include <vector>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cstddef>
#include <cmath>
#include <limits>

#include <sstream>
#include <string>

#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS.hxx>

#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_State.hxx>
#include <TopLoc_Location.hxx>

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFill.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BRepCheck_Analyzer.hxx>

#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>

#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>

#include <dbg.hpp>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

export module Cam.App.Model;

import Rev.OS.File;
import Rev.Core.Color;
import Rev.Core.Pos3;
import Rev.Core.Vertex3;

export namespace Cam::App {

    struct Model {

        // Topology
        TopoDS_Shape shape;
        std::vector<TopoDS_Face> faces;

        // Render mesh derived from the BRep shape.
        struct RenderCache {

            std::vector<Rev::Core::Vertex3> triangles;

            // One face id per rendered triangle.
            std::vector<size_t> triangleFaceIds;

            bool valid = false;

            void clear() {
                triangles.clear();
                triangleFaceIds.clear();
                valid = false;
            }
        };

        RenderCache render;

        // Selection
        std::set<size_t> selectedFaceIds;

        // State
        bool loaded = false;
        bool changed = false;

        // Serialization
        //--------------------------------------------------

        std::string getState() const {

            if (shape.IsNull()) {
                return "";
            }

            std::ostringstream stream;
            BRepTools::Write(shape, stream);
            return stream.str();
        }

        bool setState(const std::string& state) {
            clear();

            if (state.empty()) { return false; }

            std::istringstream stream(state);
            if (!stream.good()) { return false; }

            BRep_Builder builder;

            try {
                BRepTools::Read(shape, stream, builder);
            }
            catch (...) {
                clear();
                return false;
            }

            if (shape.IsNull()) {
                clear();
                return false;
            }

            collectFaces();
            tessellate();

            loaded = true;
            changed = false;

            return true;
        }

        // Construction
        //--------------------------------------------------

        static Model FromStep(Rev::OS::File& file) {
            Model model;
            model.loadStep(file);
            return model;
        }

        static Model Difference(const Model& a, const Model& b) {
            Model result;

            if (a.shape.IsNull()) { return result; }
            if (b.shape.IsNull()) { return result; }

            try {
                BRepAlgoAPI_Cut cut(a.shape, b.shape);
                cut.Build();

                if (!cut.IsDone()) { return result; }

                TopoDS_Shape cutShape = cut.Shape();
                if (cutShape.IsNull()) { return result; }

                result.shape = result.healShape(cutShape);
                result.collectFaces();
                result.tessellate();

                result.loaded = true;
                result.changed = false;
            }
            catch (const Standard_Failure& failure) {
                dbg("[Difference] exception: %s", failure.GetMessageString());
                return Model();
            }

            return result;
        }

        // State
        //--------------------------------------------------

        void clear() {
            shape = TopoDS_Shape();
            faces.clear();
            render.clear();
            selectedFaceIds.clear();
            loaded = false;
            changed = false;
        }

        // Selection
        //--------------------------------------------------

        void clearSelection() {
            selectedFaceIds.clear();
        }

        void selectFace(size_t faceId) {
            selectedFaceIds.insert(faceId);
        }

        void deselectFace(size_t faceId) {
            selectedFaceIds.erase(faceId);
        }

        void toggleFace(size_t faceId) {
            if (isFaceSelected(faceId)) { deselectFace(faceId); }
            else { selectFace(faceId); }
        }

        bool isFaceSelected(size_t faceId) const {
            return selectedFaceIds.contains(faceId);
        }

        // STEP import
        //--------------------------------------------------

        static TopoDS_Shape loadStepShape(Rev::OS::File& file) {

            STEPControl_Reader reader;

            IFSelect_ReturnStatus status = reader.ReadFile(file.string().c_str());

            if (status != IFSelect_RetDone) {
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

            clear();

            shape = loadStepShape(file);

            collectFaces();
            tessellate();

            loaded = true;
            changed = false;
        }

        // Topology
        //--------------------------------------------------

        void collectFaces() {

            faces.clear();

            if (shape.IsNull()) { return; }

            for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
                faces.push_back(TopoDS::Face(exp.Current()));
            }
        }

        // Tessellation
        //--------------------------------------------------

        static Rev::Core::Pos3 makePos3(const gp_Pnt& p) {
            return Rev::Core::Pos3(static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z()));
        }

        static Rev::Core::Pos3 makePos3(const gp_Vec& v) {
            return Rev::Core::Pos3(static_cast<float>(v.X()), static_cast<float>(v.Y()), static_cast<float>(v.Z()));
        }

        static Rev::Core::Vertex3 makeVertex(const gp_Pnt& p, Rev::Core::Color color, const gp_Vec& normal) {
            return Rev::Core::Vertex3(makePos3(p), color, makePos3(normal));
        }

        void tessellate(double tolerance = 0.1) {

            render.clear();

            if (shape.IsNull()) { return; }

            BRepMesh_IncrementalMesh mesher(shape, tolerance, false, 0.5, true);
            mesher.Perform();

            // Alpha 0 means "use mesh uniform color".
            Rev::Core::Color defaultColor = {
                0.0f,
                0.0f,
                0.0f,
                0.0f
            };

            for (size_t faceId = 0; faceId < faces.size(); faceId++) {

                TopoDS_Face& face = faces[faceId];

                TopLoc_Location loc;

                Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);

                if (tri.IsNull()) {
                    continue;
                }

                gp_Trsf trsf = loc.Transformation();

                for (int i = 1; i <= tri->NbTriangles(); i++) {

                    Poly_Triangle triangle = tri->Triangle(i);

                    int i1;
                    int i2;
                    int i3;

                    triangle.Get(i1, i2, i3);

                    gp_Pnt p1 = tri->Node(i1).Transformed(trsf);
                    gp_Pnt p2 = tri->Node(i2).Transformed(trsf);
                    gp_Pnt p3 = tri->Node(i3).Transformed(trsf);

                    gp_Vec a(p1, p2);
                    gp_Vec b(p1, p3);

                    gp_Vec n = a.Crossed(b);

                    if (n.Magnitude() <= 1e-12) {
                        continue;
                    }

                    n.Normalize();

                    if (face.Orientation() == TopAbs_REVERSED) {
                        n.Reverse();

                        render.triangles.push_back(makeVertex(p1, defaultColor, n));
                        render.triangles.push_back(makeVertex(p3, defaultColor, n));
                        render.triangles.push_back(makeVertex(p2, defaultColor, n));
                    }
                    else {
                        render.triangles.push_back(makeVertex(p1, defaultColor, n));
                        render.triangles.push_back(makeVertex(p2, defaultColor, n));
                        render.triangles.push_back(makeVertex(p3, defaultColor, n));
                    }

                    render.triangleFaceIds.push_back(faceId);
                }
            }

            render.valid = true;
        }

        // Modifying the model
        //--------------------------------------------------

        TopoDS_Shape healShape(TopoDS_Shape input) const {

            if (input.IsNull()) { return input; }

            try {
                BRepBuilderAPI_Sewing sewer(1e-6, Standard_True, Standard_True, Standard_True, Standard_True);
                sewer.Add(input);
                sewer.Perform();

                TopoDS_Shape sewed = sewer.SewedShape();

                if (!sewed.IsNull()) {
                    TopExp_Explorer solidExplorer(sewed, TopAbs_SOLID);

                    if (solidExplorer.More()) {
                        input = solidExplorer.Current();
                    }
                    else {
                        input = sewed;
                    }
                }

                ShapeUpgrade_UnifySameDomain unifier(input, true, true, true);
                unifier.Build();

                TopoDS_Shape unified = unifier.Shape();
                if (!unified.IsNull()) {
                    input = unified;
                }
            }
            catch (const Standard_Failure& failure) {
                dbg("[Heal] exception: %s", failure.GetMessageString());
            }

            return input;
        }

        bool defeatureSelected() {

            if (shape.IsNull()) { return false; }
            if (selectedFaceIds.empty()) { return false; }

            BRepAlgoAPI_Defeaturing algo;
            algo.SetShape(shape);

            bool added = false;

            for (size_t faceId : selectedFaceIds) {

                if (faceId >= faces.size()) {
                    continue;
                }

                algo.AddFaceToRemove(faces[faceId]);
                added = true;
            }

            if (!added) {
                return false;
            }

            try {
                algo.Build();

                if (!algo.IsDone()) {
                    return false;
                }

                TopoDS_Shape result = algo.Shape();

                if (result.IsNull()) {
                    return false;
                }

                shape = healShape(result);

                collectFaces();
                tessellate();
                clearSelection();

                loaded = true;
                changed = true;

                return true;
            }
            catch (const Standard_Failure& failure) {
                dbg("[Defeature] exception: %s", failure.GetMessageString());
                return false;
            }
        }

        // Extend selected connected face islands by moving the selected patch to a
        // nearby target position and reconnecting its known boundary.
        //
        // Primary path: local shell surgery. The old selected faces are omitted,
        // moved copies are inserted, and generic ruled connector faces are built
        // only along the selected patch boundary. The result is sewn into a new
        // solid directly, without asking a boolean to discover the intent.
        //
        // Fallback path: build the swept patch volume from the same correspondence
        // and fuse it. This keeps simple add-material cases usable when direct
        // shell replacement is too strict about tolerances/orientation.
        bool extendSelected(double distance = 0.05) {

            if (shape.IsNull()) {
                dbg("[Extend] failed: model shape is null");
                return false;
            }

            if (selectedFaceIds.empty()) {
                dbg("[Extend] failed: no faces selected");
                return false;
            }

            if (distance <= 0.0) {
                dbg("[Extend] failed: distance must be > 0 (got %.6f)", distance);
                return false;
            }

            std::vector<size_t> validSelectedFaceIds;

            for (size_t faceId : selectedFaceIds) {
                if (faceId >= faces.size()) {
                    dbg("[Extend] skipping stale face id %zu (face count=%zu)", faceId, faces.size());
                    continue;
                }

                validSelectedFaceIds.push_back(faceId);
            }

            if (validSelectedFaceIds.empty()) {
                dbg("[Extend] failed: no valid selected faces");
                return false;
            }

            std::vector<FaceIsland> islands = makeSelectedFaceIslands(validSelectedFaceIds);

            if (islands.empty()) {
                dbg("[Extend] failed: no selected face islands");
                return false;
            }

            dbg("[Extend] extending %zu island(s), %zu face(s), distance=%.4f", islands.size(), validSelectedFaceIds.size(), distance);

            TopoDS_Shape result = shape;
            bool any = false;

            for (const FaceIsland& island : islands) {
                if (extendIsland(result, island, distance)) {
                    any = true;
                }
            }

            if (!any) {
                dbg("[Extend] failed: all islands failed");
                return false;
            }

            shape = healShape(result);

            collectFaces();
            tessellate();
            clearSelection();

            loaded = true;
            changed = true;

            dbg("[Extend] succeeded");

            return true;
        }

    private:

        struct FaceIsland {
            std::vector<size_t> faceIds;
        };

        static double solidVolume(const TopoDS_Shape& solid) {

            GProp_GProps props;
            BRepGProp::VolumeProperties(solid, props);
            return props.Mass();
        }

        static double faceArea(const TopoDS_Face& face) {

            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            return props.Mass();
        }

        static const char* classifierStateName(TopAbs_State state) {

            switch (state) {
                case TopAbs_IN: return "IN";
                case TopAbs_OUT: return "OUT";
                case TopAbs_ON: return "ON";
                case TopAbs_UNKNOWN: return "UNKNOWN";
                default: return "?";
            }
        }

        static bool faceCenterAndNormal(const TopoDS_Face& face, gp_Pnt& center, gp_Vec& normal) {

            BRepAdaptor_Surface surf(face);

            Standard_Real uMid = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
            Standard_Real vMid = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;

            gp_Vec du;
            gp_Vec dv;

            surf.D1(uMid, vMid, center, du, dv);

            normal = du ^ dv;

            if (normal.Magnitude() <= 1e-12) {
                return false;
            }

            normal.Normalize();

            if (face.Orientation() == TopAbs_REVERSED) {
                normal.Reverse();
            }

            return true;
        }

        static bool sameShape(const TopoDS_Shape& a, const TopoDS_Shape& b) {
            return a.IsSame(b);
        }

        static std::vector<TopoDS_Edge> faceEdges(const TopoDS_Face& face) {

            std::vector<TopoDS_Edge> edges;

            for (TopExp_Explorer exp(face, TopAbs_EDGE); exp.More(); exp.Next()) {
                edges.push_back(TopoDS::Edge(exp.Current()));
            }

            return edges;
        }

        bool facesShareEdge(size_t aFaceId, size_t bFaceId) const {

            const std::vector<TopoDS_Edge> aEdges = faceEdges(faces[aFaceId]);
            const std::vector<TopoDS_Edge> bEdges = faceEdges(faces[bFaceId]);

            for (const TopoDS_Edge& aEdge : aEdges) {
                for (const TopoDS_Edge& bEdge : bEdges) {
                    if (sameShape(aEdge, bEdge)) {
                        return true;
                    }
                }
            }

            return false;
        }

        std::vector<FaceIsland> makeSelectedFaceIslands(const std::vector<size_t>& selectedIds) const {

            std::vector<FaceIsland> islands;
            std::vector<bool> visited(selectedIds.size(), false);

            for (size_t seed = 0; seed < selectedIds.size(); seed++) {
                if (visited[seed]) { continue; }

                FaceIsland island;
                std::vector<size_t> stack;

                visited[seed] = true;
                stack.push_back(seed);

                while (!stack.empty()) {
                    const size_t currentSelectedIndex = stack.back();
                    stack.pop_back();

                    const size_t currentFaceId = selectedIds[currentSelectedIndex];
                    island.faceIds.push_back(currentFaceId);

                    for (size_t other = 0; other < selectedIds.size(); other++) {
                        if (visited[other]) { continue; }

                        if (facesShareEdge(currentFaceId, selectedIds[other])) {
                            visited[other] = true;
                            stack.push_back(other);
                        }
                    }
                }

                islands.push_back(island);
            }

            return islands;
        }

        bool islandContainsFace(const FaceIsland& island, size_t faceId) const {
            for (size_t islandFaceId : island.faceIds) {
                if (islandFaceId == faceId) {
                    return true;
                }
            }

            return false;
        }

        int countSelectedOwners(const FaceIsland& island, const TopoDS_Edge& edge) const {

            int count = 0;

            for (size_t faceId : island.faceIds) {
                const std::vector<TopoDS_Edge> edges = faceEdges(faces[faceId]);

                for (const TopoDS_Edge& faceEdge : edges) {
                    if (sameShape(edge, faceEdge)) {
                        count++;
                        break;
                    }
                }
            }

            return count;
        }

        std::vector<TopoDS_Edge> boundaryEdges(const FaceIsland& island) const {

            std::vector<TopoDS_Edge> boundary;

            for (size_t faceId : island.faceIds) {
                const std::vector<TopoDS_Edge> edges = faceEdges(faces[faceId]);

                for (const TopoDS_Edge& edge : edges) {
                    if (countSelectedOwners(island, edge) != 1) {
                        continue;
                    }

                    bool duplicate = false;

                    for (const TopoDS_Edge& existing : boundary) {
                        if (sameShape(edge, existing)) {
                            duplicate = true;
                            break;
                        }
                    }

                    if (!duplicate) {
                        boundary.push_back(edge);
                    }
                }
            }

            return boundary;
        }

        static TopoDS_Shape translatedShape(const TopoDS_Shape& input, const gp_Vec& translation) {

            gp_Trsf trsf;
            trsf.SetTranslation(translation);

            BRepBuilderAPI_Transform transform(input, trsf, true, false);
            return transform.Shape();
        }

        static TopoDS_Face translatedFace(const TopoDS_Face& face, const gp_Vec& translation) {
            TopoDS_Shape moved = translatedShape(face, translation);
            if (moved.IsNull()) { return TopoDS_Face(); }
            return TopoDS::Face(moved);
        }

        static TopoDS_Edge translatedEdge(const TopoDS_Edge& edge, const gp_Vec& translation) {
            TopoDS_Shape moved = translatedShape(edge, translation);
            if (moved.IsNull()) { return TopoDS_Edge(); }
            return TopoDS::Edge(moved);
        }

        bool chooseReferenceFace(const FaceIsland& island, size_t& referenceFaceId) const {

            double bestArea = -std::numeric_limits<double>::infinity();
            bool found = false;

            for (size_t faceId : island.faceIds) {
                if (faceId >= faces.size()) { continue; }

                const double area = faceArea(faces[faceId]);

                if (area > bestArea) {
                    bestArea = area;
                    referenceFaceId = faceId;
                    found = true;
                }
            }

            return found;
        }

        bool pullDirectionForFace(
            const TopoDS_Shape& solid,
            const TopoDS_Face& face,
            double distance,
            size_t faceId,
            gp_Vec& direction
        ) const {
            gp_Pnt center;
            gp_Vec normal;

            if (!faceCenterAndNormal(face, center, normal)) {
                dbg("[Extend] face %zu: could not compute center/normal", faceId);
                return false;
            }

            const double probe = std::max(distance * 0.5, 1e-4);

            BRepClass3d_SolidClassifier classifier(solid);

            classifier.Perform(center.Translated(normal * probe), 1e-6);
            const TopAbs_State positiveState = classifier.State();

            if (positiveState == TopAbs_OUT) {
                direction = normal;
                return true;
            }

            classifier.Perform(center.Translated(normal * -probe), 1e-6);
            const TopAbs_State negativeState = classifier.State();

            if (negativeState == TopAbs_OUT) {
                gp_Vec reversed = normal;
                reversed.Reverse();
                direction = reversed;
                return true;
            }

            dbg(
                "[Extend] face %zu: no free-space direction (classifier +=%s, -=%s)",
                faceId,
                classifierStateName(positiveState),
                classifierStateName(negativeState)
            );

            return false;
        }

        bool pullDirectionForIsland(
            const TopoDS_Shape& solid,
            const FaceIsland& island,
            double distance,
            gp_Vec& direction,
            size_t& referenceFaceId
        ) const {
            if (!chooseReferenceFace(island, referenceFaceId)) {
                return false;
            }

            if (pullDirectionForFace(solid, faces[referenceFaceId], distance, referenceFaceId, direction)) {
                return true;
            }

            // If the largest face is unsuitable, try the remaining faces. This keeps
            // the operation resilient for awkward imported/trimmed faces.
            for (size_t faceId : island.faceIds) {
                if (faceId == referenceFaceId) { continue; }

                if (pullDirectionForFace(solid, faces[faceId], distance, faceId, direction)) {
                    referenceFaceId = faceId;
                    return true;
                }
            }

            return false;
        }

        static TopoDS_Face makeBoundaryWall(const TopoDS_Edge& edge, const gp_Vec& translation) {

            TopoDS_Edge movedEdge = translatedEdge(edge, translation);
            if (movedEdge.IsNull()) {
                return TopoDS_Face();
            }

            try {
                TopoDS_Face wall = BRepFill::Face(edge, movedEdge);
                return wall;
            }
            catch (const Standard_Failure& failure) {
                dbg("[Extend] boundary wall exception: %s", failure.GetMessageString());
                return TopoDS_Face();
            }
        }

        TopoDS_Shape makeIslandPatchCompound(const FaceIsland& island, bool reverseFaces) const {

            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);

            for (size_t faceId : island.faceIds) {
                if (faceId >= faces.size()) { continue; }

                TopoDS_Face face = faces[faceId];

                if (reverseFaces) {
                    face.Reverse();
                }

                builder.Add(compound, face);
            }

            return compound;
        }

        TopoDS_Shape makeTranslatedIslandPatchCompound(const FaceIsland& island, const gp_Vec& translation) const {

            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);

            for (size_t faceId : island.faceIds) {
                if (faceId >= faces.size()) { continue; }

                TopoDS_Face movedFace = translatedFace(faces[faceId], translation);
                if (movedFace.IsNull()) { continue; }

                builder.Add(compound, movedFace);
            }

            return compound;
        }

        TopoDS_Shape sewFacesToSolid(const std::vector<TopoDS_Shape>& faceShapes, size_t logFaceId) const {

            try {
                BRepBuilderAPI_Sewing sewer(1e-6, Standard_True, Standard_True, Standard_True, Standard_True);

                for (const TopoDS_Shape& faceShape : faceShapes) {
                    if (!faceShape.IsNull()) {
                        sewer.Add(faceShape);
                    }
                }

                sewer.Perform();

                TopoDS_Shape sewed = sewer.SewedShape();
                if (sewed.IsNull()) {
                    dbg("[Extend] island near face %zu: sewing produced null shape", logFaceId);
                    return TopoDS_Shape();
                }

                for (TopExp_Explorer shellExplorer(sewed, TopAbs_SHELL); shellExplorer.More(); shellExplorer.Next()) {
                    TopoDS_Shell shell = TopoDS::Shell(shellExplorer.Current());

                    BRepBuilderAPI_MakeSolid solidMaker(shell);
                    TopoDS_Shape candidate = solidMaker.Solid();

                    if (candidate.IsNull()) {
                        continue;
                    }

                    BRepCheck_Analyzer analyzer(candidate);
                    if (analyzer.IsValid()) {
                        return candidate;
                    }
                }

                // Some OCCT sew operations return a shell as the top-level shape.
                if (sewed.ShapeType() == TopAbs_SHELL) {
                    TopoDS_Shell shell = TopoDS::Shell(sewed);
                    BRepBuilderAPI_MakeSolid solidMaker(shell);
                    TopoDS_Shape candidate = solidMaker.Solid();

                    if (!candidate.IsNull()) {
                        BRepCheck_Analyzer analyzer(candidate);
                        if (analyzer.IsValid()) {
                            return candidate;
                        }
                    }
                }

                dbg("[Extend] island near face %zu: no valid solid after sewing", logFaceId);
                return TopoDS_Shape();
            }
            catch (const Standard_Failure& failure) {
                dbg("[Extend] island near face %zu: sew/solid exception: %s", logFaceId, failure.GetMessageString());
                return TopoDS_Shape();
            }
        }

        bool fuseAddedVolume(TopoDS_Shape& solid, const TopoDS_Shape& added, size_t logFaceId) const {

            if (added.IsNull()) {
                dbg("[Extend] island near face %zu: added volume is null", logFaceId);
                return false;
            }

            const double volumeBefore = solidVolume(solid);

            try {
                BRepAlgoAPI_Fuse fuse(solid, added);
                fuse.SetFuzzyValue(1e-6);
                fuse.Build();

                if (!fuse.IsDone()) {
                    dbg("[Extend] island near face %zu: boolean fuse failed", logFaceId);
                    return false;
                }

                TopoDS_Shape fused = fuse.Shape();

                if (fused.IsNull()) {
                    dbg("[Extend] island near face %zu: fuse result is null", logFaceId);
                    return false;
                }

                const double volumeAfter = solidVolume(fused);

                if (volumeAfter <= volumeBefore + 1e-9) {
                    dbg(
                        "[Extend] island near face %zu: fuse did not increase volume (before=%.9f after=%.9f)",
                        logFaceId,
                        volumeBefore,
                        volumeAfter
                    );
                    return false;
                }

                solid = fused;
                return true;
            }
            catch (const Standard_Failure& failure) {
                dbg("[Extend] island near face %zu: fuse exception: %s", logFaceId, failure.GetMessageString());
                return false;
            }
        }

        bool tryReplaceIslandByMovedPatch(
            TopoDS_Shape& solid,
            const FaceIsland& island,
            const gp_Vec& translation,
            const std::vector<TopoDS_Edge>& boundary,
            size_t referenceFaceId
        ) const {
            std::vector<TopoDS_Shape> replacementFaces;

            // Keep every original face that is not part of the edited island.
            // This is the "do the fuse ourselves" path: replace just the local
            // boundary patch, then ask sewing/solid construction to verify that
            // the shell is closed.
            for (size_t faceId = 0; faceId < faces.size(); faceId++) {
                if (islandContainsFace(island, faceId)) {
                    continue;
                }

                replacementFaces.push_back(faces[faceId]);
            }

            // Insert moved copies of the selected faces.
            for (size_t faceId : island.faceIds) {
                if (faceId >= faces.size()) { continue; }

                TopoDS_Face movedFace = translatedFace(faces[faceId], translation);
                if (!movedFace.IsNull()) {
                    replacementFaces.push_back(movedFace);
                }
            }

            int wallCount = 0;

            // Reconnect old known boundary edges to their moved counterparts.
            // This is intentionally topological, not feature-taxonomy based.
            for (const TopoDS_Edge& edge : boundary) {
                TopoDS_Face wall = makeBoundaryWall(edge, translation);
                if (!wall.IsNull()) {
                    replacementFaces.push_back(wall);
                    wallCount++;
                }
            }

            if (wallCount == 0) {
                dbg("[Extend] island near face %zu: direct replacement built no connector walls", referenceFaceId);
                return false;
            }

            TopoDS_Shape rebuilt = sewFacesToSolid(replacementFaces, referenceFaceId);

            if (rebuilt.IsNull()) {
                dbg("[Extend] island near face %zu: direct replacement did not make a valid solid", referenceFaceId);
                return false;
            }

            // Reject wildly different volumes, but allow both add and remove-ish
            // local edits. The final BRepCheck validation is done in sewFacesToSolid.
            const double before = std::abs(solidVolume(solid));
            const double after = std::abs(solidVolume(rebuilt));
            const double scale = std::max(before, 1.0);

            if (after <= 1e-12 || std::abs(after - before) > scale * 0.50) {
                dbg(
                    "[Extend] island near face %zu: direct replacement volume looked suspicious (before=%.9f after=%.9f)",
                    referenceFaceId,
                    before,
                    after
                );
                return false;
            }

            solid = rebuilt;
            dbg("[Extend] island near face %zu: direct shell replacement succeeded", referenceFaceId);
            return true;
        }

        TopoDS_Shape makeMovedPatchVolume(
            const FaceIsland& island,
            const gp_Vec& translation,
            const std::vector<TopoDS_Edge>& boundary,
            size_t referenceFaceId
        ) const {
            std::vector<TopoDS_Shape> volumeFaces;

            // Bottom of the temporary volume. It is coincident with the existing
            // solid and should face into the volume, so reverse selected faces.
            volumeFaces.push_back(makeIslandPatchCompound(island, true));

            // Top of the temporary volume.
            volumeFaces.push_back(makeTranslatedIslandPatchCompound(island, translation));

            int wallCount = 0;

            for (const TopoDS_Edge& edge : boundary) {
                TopoDS_Face wall = makeBoundaryWall(edge, translation);
                if (!wall.IsNull()) {
                    volumeFaces.push_back(wall);
                    wallCount++;
                }
            }

            if (wallCount == 0) {
                dbg("[Extend] island near face %zu: fallback volume built no connector walls", referenceFaceId);
                return TopoDS_Shape();
            }

            return sewFacesToSolid(volumeFaces, referenceFaceId);
        }

        bool extendIsland(TopoDS_Shape& solid, const FaceIsland& island, double distance) const {

            if (island.faceIds.empty()) {
                return false;
            }

            gp_Vec direction;
            size_t referenceFaceId = island.faceIds.front();

            if (!pullDirectionForIsland(solid, island, distance, direction, referenceFaceId)) {
                dbg("[Extend] island: could not determine pull direction");
                return false;
            }

            if (direction.Magnitude() <= 1e-12) {
                dbg("[Extend] island near face %zu: zero pull direction", referenceFaceId);
                return false;
            }

            direction.Normalize();
            const gp_Vec translation = direction * distance;

            const std::vector<TopoDS_Edge> boundary = boundaryEdges(island);

            if (boundary.empty()) {
                dbg("[Extend] island near face %zu: no boundary edges", referenceFaceId);
                return false;
            }

            // First attempt the intended strategy: rebuild the shell with the
            // selected patch moved and the known boundary reconnected.
            if (tryReplaceIslandByMovedPatch(solid, island, translation, boundary, referenceFaceId)) {
                return true;
            }

            // If direct shell surgery fails, use the exact same correspondence to
            // build a temporary swept patch volume and let the boolean operate on
            // a very simple local volume rather than rediscovering the feature.
            TopoDS_Shape addedVolume = makeMovedPatchVolume(island, translation, boundary, referenceFaceId);

            if (addedVolume.IsNull()) {
                return false;
            }

            return fuseAddedVolume(solid, addedVolume, referenceFaceId);
        }
    };
}
