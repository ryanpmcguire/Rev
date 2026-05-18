module;

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <glew/glew.h>
#include <glm/glm.hpp>

export module Rev.Element.View3d;

import Rev.Core.Pos;
import Rev.Core.Vertex3;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;

import Rev.Element.View3d.Actor3d;
import Rev.Element.View3d.Camera3d;

export namespace Rev::Element::View3d {

    namespace Styles {
        
        Style View3d = {
            .overflow = Overflow::Hide,
            .size = { .width = Grow(), .height = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            //.border = { .color = rgba(0, 0, 0, 1), .width = 1_px },
            .background = { .color = rgba(255, 255, 255, 0.05) }
        };
    };

    using namespace Core;

    struct View : public Box {

        // Camera UBO layout must match Mesh.vert / Mesh.frag.
        struct CameraData {
            glm::mat4 viewProj;
            glm::vec4 lightDir;
            glm::vec4 eyePos;
        };

        struct OrbitGesture {

            glm::vec3 pivot = {
                0.0f,
                0.0f,
                0.0f
            };

            Core::Pos mouse;

            bool hasPivot = false;
        };

        // View3d does NOT own these actors.
        std::vector<Actor*> actors;

        // Camera
        //--------------------------------------------------

        Camera camera;
        OrbitGesture orbit;

        Graphics::UniformBuffer* cameraBuff = nullptr;

        // Create
        //--------------------------------------------------

        View(
            Element* parent,
            StyleList styles = {},
            std::string name = "View3d"
        ) : Box(parent, styles, "View3d") {

            // Self
            this->styles = { &Styles::View3d };

            Graphics::Canvas* canvas = shared->canvas;

            cameraBuff = new Graphics::UniformBuffer(
                canvas->context,
                sizeof(CameraData)
            );
        }

        ~View() {

            actors.clear();

            delete cameraBuff;
            cameraBuff = nullptr;
        }

        // Canvas and view management
        //--------------------------------------------------

        float canvasWidth() {

            float scale = shared->canvas->details.scale;

            if (scale <= 0.0f) {
                return std::max(float(shared->canvas->details.width), 1.0f);
            }

            return std::max(float(shared->canvas->details.width) / scale, 1.0f);
        }

        float canvasHeight() {

            float scale = shared->canvas->details.scale;

            if (scale <= 0.0f) {
                return std::max(float(shared->canvas->details.height), 1.0f);
            }

            return std::max(float(shared->canvas->details.height) / scale, 1.0f);
        }

        void fitToActors() {

            glm::vec3 sceneMin;
            glm::vec3 sceneMax;

            bool valid = false;

            for (View3d::Actor* actor : actors) {

                if (!actor || !actor->visible) { continue; }

                glm::vec3 actorMin;
                glm::vec3 actorMax;

                if (!actor->bounds(actorMin, actorMax)) { continue; }

                if (!valid) {
                    sceneMin = actorMin;
                    sceneMax = actorMax;
                    valid = true;
                    continue;
                }

                sceneMin = glm::min(sceneMin, actorMin);
                sceneMax = glm::max(sceneMax, actorMax);
            }

            if (!valid) { return; }

            camera.fitBounds(
                sceneMin,
                sceneMax,
                canvasWidth(),
                canvasHeight()
            );
        }

        // Actor list
        //--------------------------------------------------

        void addActor(Actor* actor) {

            if (!actor) { return; }

            actors.push_back(actor);

            if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        void removeActor(Actor* actor) {

            actors.erase(
                std::remove(
                    actors.begin(),
                    actors.end(),
                    actor
                ),
                actors.end()
            );

            if (shared && shared->event) {
                refresh(*shared->event); 
            }
        }

        void clearActors() {

            actors.clear();

            if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        // Hit testing
        //--------------------------------------------------

        bool hitTest(
            Core::Pos mousePos,
            Hit& outHit
        ) {
            Ray ray = camera.rayFromMouse(
                mousePos,
                canvasWidth(),
                canvasHeight()
            );

            outHit = Hit();

            for (Actor* actor : actors) {

                if (!actor || !actor->visible) {
                    continue;
                }

                Hit hit;

                if (!actor->hitTest(ray, hit)) {
                    continue;
                }

                if (!outHit.hit || hit.t < outHit.t) {
                    outHit = hit;
                }
            }

            return outHit.hit;
        }

        bool hitTest(
            Core::Pos mousePos,
            glm::vec3& hitPoint
        ) {
            Hit hit;

            if (!hitTest(mousePos, hit)) {
                return false;
            }

            hitPoint = hit.point;

            return true;
        }

        // Camera
        //--------------------------------------------------

        void updateCamera() {

            CameraData data;

            data.viewProj = camera.viewProjMatrix(
                canvasWidth(),
                canvasHeight()
            );

            glm::vec3 light = glm::normalize(
                glm::vec3(-0.4f, 0.8f, 0.6f)
            );

            data.lightDir = {
                light.x,
                light.y,
                light.z,
                0.0f
            };

            glm::vec3 eye = camera.eye();

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

            Hit hit;
            glm::vec3 pivot;

            if (hitTest(e.mouse.pos, hit)) {
                pivot = hit.point;
            }

            else {
                pivot = camera.worldOnTargetPlane(
                    e.mouse.pos,
                    canvasWidth(),
                    canvasHeight()
                );
            }

            camera.mouseDown(
                e,
                pivot,
                canvasWidth(),
                canvasHeight()
            );

            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            camera.mouseDrag(
                e,
                canvasWidth(),
                canvasHeight()
            );

            refresh(e);

            Box::mouseDrag(e);
        }

        void mouseWheel(Event& e) override {

            camera.mouseWheel(
                e,
                canvasWidth(),
                canvasHeight()
            );

            refresh(e);

            Box::mouseWheel(e);
        }

        // Computing
        //--------------------------------------------------

        void computePrimitives(Event& e) override {

            for (Actor* actor : actors) {

                if (!actor) {
                    continue;
                }

                actor->compute();
            }

            Box::computePrimitives(e);
        }

        // Draw
        //--------------------------------------------------

        void begin3dDraw() {

            cameraBuff->bind(2);

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);

            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT);
        }

        void drawActors() {

            for (Actor* actor : actors) {

                if (!actor) {
                    continue;
                }

                actor->draw();
            }
        }

        void end3dDraw() {

            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
        }

        void draw(Event& e) override {

            Box::draw(e);

            updateCamera();

            begin3dDraw();

            drawActors();

            end3dDraw();
        }
    };
};