module;

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <glew/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

export module Rev.Element.View3D;

import Rev.Core.Pos;
import Rev.Core.Color;
import Rev.Core.Vertex3;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;

import Rev.Primitive.Mesh;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;

export namespace Rev::Element {

    namespace Styles {
        
        Style View3D = {
            .overflow = Overflow::Hide,
            .size = { .width = Grow(), .height = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .background = { .color = rgba(255, 255, 255, 0.05) }
        };
    };

    using namespace Core;

    struct View3D : public Box {

        // Camera UBO layout must match Mesh.vert / Mesh.frag.
        struct CameraData {
            glm::mat4 viewProj;
            glm::vec4 lightDir;
            glm::vec4 eyePos;
        };

        struct Actor {
            Primitives::Mesh* mesh = nullptr;
            bool visible = true;
        };

        // Geometry
        //--------------------------------------------------

        std::vector<Vertex3> cubeTriangles;

        // Scene actors owned by this View3D.
        std::vector<Actor> actors;

        // Camera
        //--------------------------------------------------

        Graphics::UniformBuffer* cameraBuff = nullptr;

        Pos yawPitch = { 0.65f, 0.45f };
        Pos pinYawPitch;

        float distance = 4.0f;

        // Create
        //--------------------------------------------------

        View3D(
            Element* parent,
            StyleList styles = {},
            std::string name = "View3D"
        ) : Box(parent, styles, "View3D") {

            // Self
            this->styles = { &Styles::View3D };

            Graphics::Canvas* canvas = shared->canvas;

            cameraBuff = new Graphics::UniformBuffer(
                canvas->context,
                sizeof(CameraData)
            );

            Primitives::Mesh* cubeMesh = new Primitives::Mesh(canvas, {
                .triangles = &cubeTriangles
            });

            actors.push_back({
                .mesh = cubeMesh,
                .visible = true
            });
        }

        ~View3D() {

            for (Actor& actor : actors) {
                delete actor.mesh;
                actor.mesh = nullptr;
            }

            actors.clear();

            delete cameraBuff;
        }

        // Cube construction
        //--------------------------------------------------

        void pushTriangle(
            Vertex3 a,
            Vertex3 b,
            Vertex3 c
        ) {
            cubeTriangles.push_back(a);
            cubeTriangles.push_back(b);
            cubeTriangles.push_back(c);
        }

        void pushQuad(
            Vertex3 a,
            Vertex3 b,
            Vertex3 c,
            Vertex3 d
        ) {
            pushTriangle(a, b, c);
            pushTriangle(a, c, d);
        }

        void setNormal(
            Vertex3& v,
            float nx,
            float ny,
            float nz
        ) {
            v.nx = nx;
            v.ny = ny;
            v.nz = nz;
        }

        Vertex3 makeVertex(
            float x,
            float y,
            float z,
            Core::Color color,
            float nx,
            float ny,
            float nz
        ) {
            Vertex3 v = { x, y, z, color };

            v.nx = nx;
            v.ny = ny;
            v.nz = nz;

            return v;
        }

        void buildCube() {

            cubeTriangles.clear();

            Core::Color color = { 0.75f, 0.75f, 0.82f, 1.0f };

            float s = 1.0f;

            // Cube positions in object/world space.
            // Duplicated per face so each face has a flat normal.

            // Front, +Z
            pushQuad(
                makeVertex(-s, -s,  s, color, 0, 0, 1),
                makeVertex( s, -s,  s, color, 0, 0, 1),
                makeVertex( s,  s,  s, color, 0, 0, 1),
                makeVertex(-s,  s,  s, color, 0, 0, 1)
            );

            // Back, -Z
            pushQuad(
                makeVertex( s, -s, -s, color, 0, 0, -1),
                makeVertex(-s, -s, -s, color, 0, 0, -1),
                makeVertex(-s,  s, -s, color, 0, 0, -1),
                makeVertex( s,  s, -s, color, 0, 0, -1)
            );

            // Left, -X
            pushQuad(
                makeVertex(-s, -s, -s, color, -1, 0, 0),
                makeVertex(-s, -s,  s, color, -1, 0, 0),
                makeVertex(-s,  s,  s, color, -1, 0, 0),
                makeVertex(-s,  s, -s, color, -1, 0, 0)
            );

            // Right, +X
            pushQuad(
                makeVertex( s, -s,  s, color, 1, 0, 0),
                makeVertex( s, -s, -s, color, 1, 0, 0),
                makeVertex( s,  s, -s, color, 1, 0, 0),
                makeVertex( s,  s,  s, color, 1, 0, 0)
            );

            // Top, +Y
            pushQuad(
                makeVertex(-s,  s,  s, color, 0, 1, 0),
                makeVertex( s,  s,  s, color, 0, 1, 0),
                makeVertex( s,  s, -s, color, 0, 1, 0),
                makeVertex(-s,  s, -s, color, 0, 1, 0)
            );

            // Bottom, -Y
            pushQuad(
                makeVertex(-s, -s, -s, color, 0, -1, 0),
                makeVertex( s, -s, -s, color, 0, -1, 0),
                makeVertex( s, -s,  s, color, 0, -1, 0),
                makeVertex(-s, -s,  s, color, 0, -1, 0)
            );
        }

        // Camera
        //--------------------------------------------------

        void updateCamera() {

            float safeHeight = std::max(rect.h, 1.0f);
            float aspect = rect.w / safeHeight;

            float yaw = yawPitch.x;
            float pitch = yawPitch.y;

            glm::vec3 target = {
                0.0f,
                0.0f,
                0.0f
            };

            glm::vec3 eye = {
                distance * std::cos(pitch) * std::sin(yaw),
                distance * std::sin(pitch),
                distance * std::cos(pitch) * std::cos(yaw)
            };

            glm::mat4 view = glm::lookAt(
                eye,
                target,
                glm::vec3(0.0f, 1.0f, 0.0f)
            );

            // CAD-like orthographic camera.
            float scale = 2.2f;

            glm::mat4 proj = glm::ortho(
                -scale * aspect,
                 scale * aspect,
                -scale,
                 scale,
                -100.0f,
                 100.0f
            );

            CameraData data;

            data.viewProj = proj * view;

            glm::vec3 light = glm::normalize(
                glm::vec3(-0.4f, 0.8f, 0.6f)
            );

            data.lightDir = {
                light.x,
                light.y,
                light.z,
                0.0f
            };

            data.eyePos = {
                eye.x,
                eye.y,
                eye.z,
                1.0f
            };

            cameraBuff->set(
                &data
            );
        }

        // Events
        //--------------------------------------------------

        void mouseDown(Event& e) override {

            pinYawPitch = yawPitch;

            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            float sensitivity = 0.008f;

            yawPitch = pinYawPitch + e.mouse.diff * sensitivity;

            yawPitch.y = std::clamp(
                yawPitch.y,
                -1.45f,
                 1.45f
            );

            refresh(e);

            Box::mouseDrag(e);
        }

        void mouseWheel(Event& e) override {

            float scale = (
                e.mouse.wheel.y > 0.0f
                ? 0.9f
                : 1.1f
            );

            distance *= scale;

            distance = std::clamp(
                distance,
                1.5f,
                50.0f
            );

            refresh(e);

            Box::mouseWheel(e);
        }

        // Computing
        //--------------------------------------------------

        void computePrimitives(Event& e) override {

            buildCube();

            for (Actor& actor : actors) {

                if (!actor.mesh) {
                    continue;
                }

                actor.mesh->color = {
                    0.75f,
                    0.75f,
                    0.82f,
                    1.0f
                };

                actor.mesh->compute();
            }

            Box::computePrimitives(e);
        }

        // Draw
        //--------------------------------------------------

        void draw(Event& e) override {

            // Draw normal element background first.
            Box::draw(e);

            updateCamera();

            cameraBuff->bind(2);

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);

            // This only works fully if your framebuffer has a depth attachment.
            glClear(GL_DEPTH_BUFFER_BIT);

            for (Actor& actor : actors) {

                if (!actor.visible) {
                    continue;
                }

                if (!actor.mesh) {
                    continue;
                }

                actor.mesh->draw();
            }

            glDisable(GL_DEPTH_TEST);
        }
    };
}