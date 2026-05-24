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
#include <TopoDS.hxx>

#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>

#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>

#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>

#include <dbg.hpp>

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

        TopoDS_Shape shape;
        std::vector<TopoDS_Face> faces;
        RenderCache render;

        std::set<size_t> selectedFaceIds;

        bool loaded = false;
        bool changed = false;

        // Construction
        //--------------------------------------------------

        static Model FromStep(Rev::OS::File& file) {
            Model model;
            model.loadStep(file);
            return model;
        }

        static Model Difference(const Model& a, const Model& b) {
            Model result;

            if (a.shape.IsNull() || b.shape.IsNull()) { return result; }

            try {
                BRepAlgoAPI_Cut cut(a.shape, b.shape);
                cut.Build();

                if (!cut.IsDone() || cut.Shape().IsNull()) { return result; }

                result.adoptShape(result.healShape(cut.Shape()), false);
            }
            catch (const Standard_Failure& failure) {
                dbg("[Difference] exception: %s", failure.GetMessageString());
                return Model();
            }

            return result;
        }

        // State / serialization
        //--------------------------------------------------

        void clear() {
            shape = TopoDS_Shape();
            faces.clear();
            render.clear();
            selectedFaceIds.clear();
            loaded = false;
            changed = false;
        }

        std::string getState() const {
            if (shape.IsNull()) { return ""; }

            std::ostringstream stream;
            BRepTools::Write(shape, stream);
            return stream.str();
        }

        bool setState(const std::string& state) {
            clear();

            if (state.empty()) { return false; }

            std::istringstream stream(state);
            if (!stream.good()) { return false; }

            try {
                BRep_Builder builder;
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

            refreshTopologyAndRender();
            loaded = true;
            changed = false;

            return true;
        }

        // Selection
        //--------------------------------------------------

        void clearSelection() { selectedFaceIds.clear(); }
        void selectFace(size_t faceId) { selectedFaceIds.insert(faceId); }
        void deselectFace(size_t faceId) { selectedFaceIds.erase(faceId); }
        bool isFaceSelected(size_t faceId) const { return selectedFaceIds.contains(faceId); }

        void toggleFace(size_t faceId) {
            if (isFaceSelected(faceId)) { deselectFace(faceId); }
            else { selectFace(faceId); }
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
            clear();
            adoptShape(loadStepShape(file), false);
        }

        // Topology / render cache
        //--------------------------------------------------

        void collectFaces() {
            faces.clear();

            if (shape.IsNull()) { return; }

            for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
                faces.push_back(TopoDS::Face(exp.Current()));
            }
        }

        void tessellate(double tolerance = 0.1) {
            render.clear();
            if (shape.IsNull()) { return; }

            BRepMesh_IncrementalMesh mesher(shape, tolerance, false, 0.5, true);
            mesher.Perform();

            Rev::Core::Color defaultColor = { 0.0f, 0.0f, 0.0f, 0.0f };

            for (size_t faceId = 0; faceId < faces.size(); faceId++) {
                appendFaceTriangles(faceId, defaultColor);
            }

            render.valid = true;
        }

        // Modeling operations
        //--------------------------------------------------

        TopoDS_Shape healShape(TopoDS_Shape input) const {
            if (input.IsNull()) { return input; }

            try {
                BRepBuilderAPI_Sewing sewer(1e-6, Standard_True, Standard_True, Standard_True, Standard_True);
                sewer.Add(input);
                sewer.Perform();

                TopoDS_Shape sewed = sewer.SewedShape();
                if (!sewed.IsNull()) {
                    input = firstSolidOrSelf(sewed);
                }

                ShapeUpgrade_UnifySameDomain unifier(input, true, true, true);
                unifier.Build();

                TopoDS_Shape unified = unifier.Shape();
                if (!unified.IsNull()) { input = unified; }
            }
            catch (const Standard_Failure& failure) {
                dbg("[Heal] exception: %s", failure.GetMessageString());
            }

            return input;
        }

        bool defeatureSelected() {
            if (shape.IsNull() || selectedFaceIds.empty()) { return false; }

            BRepAlgoAPI_Defeaturing defeature;
            defeature.SetShape(shape);

            if (!addSelectedFacesToDefeature(defeature)) { return false; }

            try {
                defeature.Build();

                if (!defeature.IsDone() || defeature.Shape().IsNull()) {
                    return false;
                }

                adoptShape(healShape(defeature.Shape()), true);
                return true;
            }
            catch (const Standard_Failure& failure) {
                dbg("[Defeature] exception: %s", failure.GetMessageString());
                return false;
            }
        }

        // Placeholder for the future topology-preserving face/edge deformation path.
        // It intentionally does nothing for now.
        bool offsetSelected(double distance = 0.05) {
            (void)distance;
            return false;
        }

    private:

        void adoptShape(const TopoDS_Shape& nextShape, bool markChanged) {
            shape = nextShape;
            refreshTopologyAndRender();
            clearSelection();
            loaded = !shape.IsNull();
            changed = markChanged;
        }

        void refreshTopologyAndRender() {
            collectFaces();
            tessellate();
        }

        bool addSelectedFacesToDefeature(BRepAlgoAPI_Defeaturing& defeature) const {
            bool added = false;

            for (size_t faceId : selectedFaceIds) {
                if (faceId >= faces.size()) { continue; }
                defeature.AddFaceToRemove(faces[faceId]);
                added = true;
            }

            return added;
        }

        static TopoDS_Shape firstSolidOrSelf(const TopoDS_Shape& input) {
            for (TopExp_Explorer exp(input, TopAbs_SOLID); exp.More(); exp.Next()) {
                return exp.Current();
            }

            return input;
        }

        void appendFaceTriangles(size_t faceId, Rev::Core::Color color) {
            TopoDS_Face& face = faces[faceId];
            TopLoc_Location loc;

            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) { return; }

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
            if (normal.Magnitude() <= 1e-12) { return; }

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
