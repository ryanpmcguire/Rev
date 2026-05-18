module;

#include <vector>
#include <set>
#include <stdexcept>
#include <cstddef>

#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>

#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopLoc_Location.hxx>

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <BRepAlgoAPI_Cut.hxx>

#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

export module Cam.App.Model;

import Rev.OS.File;
import Rev.Core.Color;
import Rev.Core.Vertex3;

export namespace Cam::App {

    struct Model {

        TopoDS_Shape shape;
        std::vector<TopoDS_Face> faces;

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

        static Model Difference(
            const Model& a,
            const Model& b
        ) {
            Model result;

            if (a.shape.IsNull()) { return result; }
            if (b.shape.IsNull()) { return result; }

            BRepAlgoAPI_Cut cut(
                a.shape,
                b.shape
            );

            cut.Build();

            if (!cut.IsDone()) {
                return result;
            }

            TopoDS_Shape shape = cut.Shape();

            if (shape.IsNull()) {
                return result;
            }

            result.shape = result.healShape(shape);

            result.collectFaces();
            result.tessellate();

            result.loaded = true;
            result.changed = false;

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

            if (isFaceSelected(faceId)) {
                deselectFace(faceId);
            }

            else {
                selectFace(faceId);
            }
        }

        bool isFaceSelected(size_t faceId) const {

            return selectedFaceIds.contains(faceId);
        }

        // STEP import
        //--------------------------------------------------

        static TopoDS_Shape loadStepShape(Rev::OS::File& file) {

            STEPControl_Reader reader;

            IFSelect_ReturnStatus status = reader.ReadFile(
                file.string().c_str()
            );

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

            if (shape.IsNull()) {
                return;
            }

            for (
                TopExp_Explorer exp(shape, TopAbs_FACE);
                exp.More();
                exp.Next()
            ) {
                faces.push_back(
                    TopoDS::Face(exp.Current())
                );
            }
        }

        // Tessellation
        //--------------------------------------------------

        static Rev::Core::Vertex3 makeVertex(
            const gp_Pnt& p,
            Rev::Core::Color color,
            const gp_Vec& normal
        ) {
            Rev::Core::Vertex3 v = {
                static_cast<float>(p.X()),
                static_cast<float>(p.Y()),
                static_cast<float>(p.Z()),
                color
            };

            v.nx = static_cast<float>(normal.X());
            v.ny = static_cast<float>(normal.Y());
            v.nz = static_cast<float>(normal.Z());

            return v;
        }

        void tessellate(double tolerance = 0.1) {

            render.clear();

            if (shape.IsNull()) {
                return;
            }

            BRepMesh_IncrementalMesh mesher(
                shape,
                tolerance,
                false,
                0.5,
                true
            );

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

                Handle(Poly_Triangulation) tri =
                    BRep_Tool::Triangulation(face, loc);

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

            if (input.IsNull()) {
                return input;
            }

            ShapeUpgrade_UnifySameDomain unifier(
                input,
                true,
                true,
                true
            );

            unifier.Build();

            return unifier.Shape();
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
    };
}