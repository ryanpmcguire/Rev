module;

#include <string>
#include <vector>
#include <stdexcept>
#include <cmath>

#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>

#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopLoc_Location.hxx>

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>

#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>

#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

export module CAM.Step;

import Rev.Core.Color;
import Rev.Core.Vertex3;
import Rev.OS.File;

import Rev.Graphics.Canvas;

import Rev.Element.View3d.Actor3d;
import Rev.Primitive.Mesh;

export namespace CAM {

    using namespace Rev::Core;

    struct Step {

        static TopoDS_Shape LoadShape(Rev::OS::File& file) {

            STEPControl_Reader reader;

            IFSelect_ReturnStatus status = reader.ReadFile(
                file.string().c_str()
            );

            if (status != IFSelect_RetDone) {
                throw std::runtime_error("Failed to read STEP file.");
            }

            reader.TransferRoots();

            TopoDS_Shape shape = reader.OneShape();

            if (shape.IsNull()) {
                throw std::runtime_error("STEP import produced null shape.");
            }

            return shape;
        }

        static Vertex3 makeVertex(
            const gp_Pnt& p,
            Color color,
            const gp_Vec& normal
        ) {
            Vertex3 v = {
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

        static void TessellateShape(
            TopoDS_Shape shape,
            std::vector<Vertex3>& out,
            double tolerance = 0.1
        ) {
            out.clear();

            BRepMesh_IncrementalMesh mesher(
                shape,
                tolerance,
                false,
                0.5,
                true
            );

            mesher.Perform();

            Color color = {
                0.75f,
                0.75f,
                0.82f,
                1.0f
            };

            for (
                TopExp_Explorer exp(shape, TopAbs_FACE);
                exp.More();
                exp.Next()
            ) {
                TopoDS_Face face = TopoDS::Face(exp.Current());

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

                        out.push_back(makeVertex(p1, color, n));
                        out.push_back(makeVertex(p3, color, n));
                        out.push_back(makeVertex(p2, color, n));
                    }

                    else {
                        out.push_back(makeVertex(p1, color, n));
                        out.push_back(makeVertex(p2, color, n));
                        out.push_back(makeVertex(p3, color, n));
                    }
                }
            }
        }

        static Rev::Actor3D* ActorFromFile(
            Rev::Graphics::Canvas* canvas,
            Rev::OS::File& file
        ) {
            TopoDS_Shape shape = LoadShape(file);

            Rev::Actor3D* actor = new Rev::Actor3D();

            actor->visible = true;
            actor->ownsMesh = true;
            actor->ownsTriangles = true;

            TessellateShape(
                shape,
                actor->triangles,
                0.1
            );

            actor->mesh = new Rev::Primitives::Mesh(canvas, {
                .triangles = &actor->triangles
            });

            actor->mesh->color = {
                0.75f,
                0.75f,
                0.82f,
                1.0f
            };

            return actor;
        }
    };
}