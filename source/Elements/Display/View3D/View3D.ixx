module;

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <glew/glew.h>
#include <glm/glm.hpp>

export module Rev.Element.View3d;

import Rev.Core.Pos;
import Rev.Core.Pos3;
import Rev.Core.Vertex3;
import Rev.Core.Animator;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

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
            //.margin = { 4_px, 4_px, 4_px, 4_px },
            //.background = { .color = rgba(255, 255, 255, 0.05) }
        };
    };

    using namespace Core;

    struct View : public Box {

        // Camera UBO layout must match Mesh3d.vert / Mesh3d.frag.
        //
        // This intentionally stays GLM-backed because it is the GPU boundary.
        // Camera / Actor world-space math is Pos3-backed.
        struct CameraData {
            glm::mat4 viewProj;
            glm::vec4 lightDir;
            glm::vec4 eyePos;
            glm::vec4 lightDir2;
        };

        Camera camera;

        Graphics::UniformBuffer* cameraBuff = nullptr;
        Core::Animator zoomAnimator;

        void stopZoomAnimation() {
            zoomAnimator.stop();
            camera.cancelZoomAnimation();
        }

        void setupZoomAnimator() {

            zoomAnimator.setFrequency(100.0);

            zoomAnimator.onFrame([this](Core::AnimationEvent& frame) {

                if (!shared || !shared->event) { return; }

                Event& e = *shared->event;

                if (!camera.stepZoomAnimation(static_cast<float>(frame.deltaMs))) {
                    zoomAnimator.stop();
                }

                refresh(e);
            });
        }

        // Create
        //--------------------------------------------------

        View(
            Element* parent,
            StyleList styles = {},
            std::string name = "View3d"
        ) : Box(parent, styles, name) {

            this->styles.prepend(&Styles::View3d);

            cameraBuff = new Graphics::UniformBuffer(
                shared->canvas->context,
                sizeof(CameraData)
            );

            camera.syncZoomGoalsFromCurrent();
            setupZoomAnimator();
        }

        ~View() {

            zoomAnimator.stop();

            delete cameraBuff;
            cameraBuff = nullptr;
        }

        // Canvas
        //--------------------------------------------------

        float canvasWidth() {

            float scale = shared->canvas->details.scale;
            float width = float(shared->canvas->details.width);

            if (scale > 0.0f) { width /= scale; }

            return std::max(width, 1.0f);
        }

        float canvasHeight() {

            float scale = shared->canvas->details.scale;
            float height = float(shared->canvas->details.height);

            if (scale > 0.0f) { height /= scale; }

            return std::max(height, 1.0f);
        }

        // Actor list
        //--------------------------------------------------

        void addActor(Actor* actor) {

            if (!actor) { return; }

            addChild(actor);

            if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        void removeActor(Actor* actor) {

            if (hoveredActor == actor) { hoveredActor = nullptr; }

            removeChild(actor);

            if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        void insertActorBefore(Actor* actor, Actor* before) {

            if (!actor || !before) { return; }

            if (actor->parent != this) { addChild(actor); }

            moveChild(actor, before, true);

            if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        void clearActors() {

            hoveredActor = nullptr;

            std::vector<Element*> childrenCopy = children;

            for (Element* child : childrenCopy) {
                if (Actor* actor = dynamic_cast<Actor*>(child)) {
                    delete actor;
                }
            }

            if (shared && shared->event) {
                refresh(*shared->event);
            }
        }

        // Actors in the subtree
        //--------------------------------------------------

        // Every Actor in the view's subtree (depth-first), not just direct children —
        // actors can nest under grouping elements. Walked on demand (the callers are
        // cold paths over few actors) rather than cached.
        void collectActors(Element* element, std::vector<Actor*>& out) const {

            // A hidden subtree is out of the scene entirely — draw already skips it, and now
            // so do fit/hit/hover — so a group of actors can toggle with one style.
            if (element->resolved.hidden) { return; }

            if (Actor* actor = dynamic_cast<Actor*>(element)) {
                out.push_back(actor);
            }

            for (Element* child : element->children) {
                collectActors(child, out);
            }
        }

        std::vector<Actor*> sceneActors() const {

            std::vector<Actor*> actors;

            for (Element* child : children) {
                collectActors(child, actors);
            }

            return actors;
        }

        // Fit
        //--------------------------------------------------

        bool sceneBounds(Pos3& sceneMin, Pos3& sceneMax) const {

            bool valid = false;

            for (Actor* actor : sceneActors()) {

                Pos3 actorMin;
                Pos3 actorMax;

                if (!actor->bounds(actorMin, actorMax)) { continue; }

                if (!valid) {
                    sceneMin = actorMin;
                    sceneMax = actorMax;
                    valid = true;
                    continue;
                }

                sceneMin = Pos3::min(sceneMin, actorMin);
                sceneMax = Pos3::max(sceneMax, actorMax);
            }

            return valid;
        }

        float sceneAverageDimension() const {

            Pos3 sceneMin;
            Pos3 sceneMax;

            if (sceneBounds(sceneMin, sceneMax)) {

                const float dx = sceneMax.x - sceneMin.x;
                const float dy = sceneMax.y - sceneMin.y;
                const float dz = sceneMax.z - sceneMin.z;

                return (dx + dy + dz) / 3.0f;
            }

            return std::max(camera.orthoScale * 2.0f, camera.distance * 0.25f);
        }

        void fitToActors() {

            Pos3 sceneMin;
            Pos3 sceneMax;

            if (!sceneBounds(sceneMin, sceneMax)) { return; }

            camera.fitBounds(
                sceneMin,
                sceneMax,
                canvasWidth(),
                canvasHeight()
            );
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

            for (Actor* actor : sceneActors()) {

                Hit hit;

                // Actor::hitTest decides whether the actor is selectable.
                // This allows visible=false / selectable=true picking actors.
                if (!actor->hitTest(ray, hit)) { continue; }

                if (!outHit.hit || hit.t < outHit.t) {
                    outHit = hit;
                }
            }

            return outHit.hit;
        }

        bool hitTest(
            Core::Pos mousePos,
            Core::Pos3& hitPoint
        ) {
            Hit hit;

            if (!hitTest(mousePos, hit)) { return false; }

            hitPoint = hit.point;

            return true;
        }

        // Orbit-pivot hit test: tests visible actors regardless of selectable,
        // so the pivot always lands on what is actually drawn on screen.
        bool hitTestVisible(
            Core::Pos mousePos,
            Hit& outHit
        ) {
            Ray ray = camera.rayFromMouse(
                mousePos,
                canvasWidth(),
                canvasHeight()
            );

            outHit = Hit();

            for (Actor* actor : sceneActors()) {

                Hit hit;

                if (!actor->hitTestVisible(ray, hit)) { continue; }

                if (!outHit.hit || hit.t < outHit.t) {
                    outHit = hit;
                }
            }

            return outHit.hit;
        }

        // Camera
        //--------------------------------------------------

        void updateCamera() {

            // Keep this as GLM because CameraData is uploaded directly to the
            // shader uniform buffer and must match the existing shader layout.
            glm::vec3 keyLight = glm::normalize(
                glm::vec3(-0.4f, 0.8f, 0.6f)
            );

            // Fill from the opposite hemisphere so back-facing features still read.
            glm::vec3 fillLight = glm::normalize(
                glm::vec3(0.45f, -0.25f, -0.85f)
            );

            Pos3 eye = camera.eye();

            CameraData data = {
                camera.viewProjMatrix(canvasWidth(), canvasHeight()),
                { keyLight.x, keyLight.y, keyLight.z, 0.0f },
                { eye.x, eye.y, eye.z, 1.0f },
                { fillLight.x, fillLight.y, fillLight.z, 0.0f }
            };

            cameraBuff->set(&data);
        }

        // Events
        //--------------------------------------------------

        // The actor the cursor is over, so we can tell it when the cursor leaves.
        Actor* hoveredActor = nullptr;

        // Was the gesture's press the left button alone? (left = select; right/middle
        // = camera.)
        bool pressLeft = false;

        // Ray-cast the cursor and deliver hover to the actor under it.
        void mouseMove(Event& e) override {

            Hit hit;
            Actor* nowHovered = hitTestVisible(e.mouse.pos, hit) ? hit.actor : nullptr;

            if (hoveredActor && hoveredActor != nowHovered) { hoveredActor->onUnhover(); }
            if (nowHovered) { nowHovered->onHover(hit); }
            hoveredActor = nowHovered;

            Box::mouseMove(e);
        }

        void mouseLeave(Event& e) override {
            if (hoveredActor) { hoveredActor->onUnhover(); hoveredActor = nullptr; }
            Box::mouseLeave(e);
        }

        // Left click selects: plain click replaces the selection (clearing every actor
        // first), ctrl+click adds; a click on empty space clears. A drag isn't a click.
        void click(Event& e) override {

            if (pressLeft) {

                auto d = e.mouse.up - e.mouse.down;

                if (d.x * d.x + d.y * d.y <= 25.0f) {

                    bool additive = static_cast<bool>(e.keyboard.ctrl);

                    Hit hit;
                    bool got = hitTestVisible(e.mouse.pos, hit) && hit.actor;

                    if (!additive) { for (Actor* a : sceneActors()) { a->onDeselect(); } }
                    if (got) { hit.actor->onPick(hit, additive); }
                }
            }

            Box::click(e);
        }

        // Escape clears the selection across the scene.
        void keyDown(Event& e) override {
            if (e.keyboard.escape) { for (Actor* a : sceneActors()) { a->onDeselect(); } }
            Box::keyDown(e);
        }

        void mouseDown(Event& e) override {

            pressLeft = e.mouse.lb && !e.mouse.rb && !e.mouse.mb;

            // Camera control is right/middle only — set up the orbit pivot for those.
            if (e.mouse.rb || e.mouse.mb) {

                Hit hit;
                Pos3 pivot;

                if (hitTestVisible(e.mouse.pos, hit)) {
                    pivot = hit.point;
                }

                else {
                    pivot = camera.worldOnTargetPlane(
                        e.mouse.pos,
                        canvasWidth(),
                        canvasHeight()
                    );
                }

                stopZoomAnimation();

                camera.mouseDown(
                    e,
                    pivot,
                    canvasWidth(),
                    canvasHeight()
                );

                refresh(e);
            }

            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            // Orbit / pan only while a right or middle button is held.
            if (e.mouse.rb || e.mouse.mb) {

                stopZoomAnimation();

                camera.mouseDrag(
                    e,
                    canvasWidth(),
                    canvasHeight()
                );

                refresh(e);
            }

            Box::mouseDrag(e);
        }

        void mouseWheel(Event& e) override {

            Hit hit;
            Pos3 anchorPoint;
            bool hasAnchorPoint = false;

            if (hitTestVisible(e.mouse.pos, hit)) {
                anchorPoint = hit.point;
                hasAnchorPoint = true;
            }

            else {
                anchorPoint = camera.worldOnTargetPlane(
                    e.mouse.pos,
                    canvasWidth(),
                    canvasHeight()
                );

                hasAnchorPoint = true;
            }

            camera.applyWheelZoom(
                e,
                canvasWidth(),
                canvasHeight(),
                sceneAverageDimension(),
                anchorPoint,
                hasAnchorPoint
            );

            if (!e.keyboard.alt && std::abs(e.mouse.wheel.y) > 1e-6f) {
                if (!zoomAnimator.isPlaying()) {
                    zoomAnimator.play();
                }
            }

            refresh(e);

            Box::mouseWheel(e);
        }

        // Draw
        //--------------------------------------------------

        void draw(Event& e) override {

            Box::draw(e);

            updateCamera();

            // Establish the shared 3D context the actors rely on. The actors
            // themselves are drawn by the window's normal pass (they're elements in
            // the draw list); each one toggles depth testing around its own draw.
            cameraBuff->bind(2);
            shared->canvas->clearDepth();
        }
    };
};
