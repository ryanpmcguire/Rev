module;

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

export module Rev.Primitive.Group;

import Rev.Primitive;
import Rev.Core.Transform;
import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;

export namespace Rev::Primitives {

    using namespace Rev::Core;

    // Primitives drawn in their own units, placed in a parent group's units.
    // The group binds its composed transform at binding 4 while its members
    // draw, then rebinds its parent's (or the canvas identity at the root).
    // Members carry no transform of their own; the group tree is the stack.
    // FastLines, Lines, Triangles and Video read the slot (OpenGL only; Metal
    // is group-blind and draws them at identity). Other primitives draw in px.
    struct Group : public Primitive {

        // The slot every placeable primitive reads (std140, 32 bytes).
        //   px = (dot(r0.xy, p) + r0.w, dot(r1.xy, p) + r1.w)
        struct Affine {
            float r0[4];   // a, b, 0, tx
            float r1[4];   // c, d, 0, ty
        };

        // What toPixel was composed from.
        struct Stamp {
            uint64_t parent = 0;      // the parent's revision (0 at the root)
            uint64_t placement = 0;   // this group's placement revision

            bool operator==(const Stamp&) const = default;
        };

        inline static constexpr unsigned int Slot = 4;   // the canvas binds identity here each frame

        Group* parent = nullptr;              // not owned; also the stack below this group
        std::vector<Primitive*> members;      // drawn in order, primitives and child groups;
                                              // not owned: producers own their primitives, as elements do
        std::vector<Group*> children;         // the members that are groups, for unlinking

        Transform toParent = Transform::Identity();  // given: this group's units -> parent's units (px at the root)
        Transform toPixel = Transform::Identity();   // composed: this group's units -> logical px
        uint64_t placement = 1;               // bumps when toParent changes
        uint64_t revision = 0;                // bumps when toPixel changes
        Stamp composed;                       // what toPixel was composed from
        UniformBuffer* buffer = nullptr;      // toPixel on the GPU, written only on recompose

        // Create
        Group(Canvas* canvas) : Primitive(canvas) {
            buffer = new UniformBuffer(canvas->context, sizeof(Affine));
        }

        // Destroy: unlink from both sides, so either may die first.
        // ~Primitive is not virtual, so the group never deletes through Primitive*.
        ~Group() {
            if (parent) {
                parent->remove(this);
            }
            for (Group* child : children) {
                child->parent = nullptr;
            }
            delete buffer;
        }

        // Nest a group here, in draw order with the primitives.
        void add(Group* child) {
            if (child->parent) {
                child->parent->remove(child);
            }
            child->parent = this;
            child->composed = {};
            children.push_back(child);
            members.push_back(child);
        }

        // Any primitive. Those that read the slot are placed; the rest draw in px.
        void add(Primitive* primitive) {
            members.push_back(primitive);
        }

        // The producer calls this in its destructor, before deleting the primitive.
        void remove(Primitive* primitive) {
            for (size_t i = 0; i < members.size(); i++) {
                if (members[i] == primitive) {
                    members.erase(members.begin() + i);
                    break;
                }
            }
            for (size_t i = 0; i < children.size(); i++) {
                if (static_cast<Primitive*>(children[i]) == primitive) {
                    children.erase(children.begin() + i);
                    break;
                }
            }
        }

        // Give this group its transform into its parent's units. Unchanged costs nothing.
        void place(const Transform& transform) {
            if (std::memcmp(&transform, &toParent, sizeof(Transform)) == 0) {
                return;
            }
            toParent = transform;
            placement++;
        }

        // Recompose only when the parent or the placement moved; one buffer write.
        void compose() {
            Stamp now = { parent ? parent->revision : 0, placement };
            if (now == composed) {
                return;
            }
            composed = now;
            toPixel = parent ? parent->toPixel * toParent : toParent;
            revision++;
            Affine slot = affine();
            buffer->set(&slot);
        }

        // toPixel in the slot's two-row form. Rotation and shear are carried.
        Affine affine() const {
            return {
                { toPixel.m[0][0], toPixel.m[0][1], 0.0f, toPixel.m[0][2] },
                { toPixel.m[1][0], toPixel.m[1][1], 0.0f, toPixel.m[1][2] }
            };
        }

        // Push this group's transform, draw the members, pop back to the parent's.
        void draw() override {
            compose();
            buffer->bind(Slot);
            for (Primitive* member : members) {
                member->draw();
            }
            if (parent) {
                parent->buffer->bind(Slot);
            } else {
                canvas->identity->bind(Slot);
            }
        }
    };
};
