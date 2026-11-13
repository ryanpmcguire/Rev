module;

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

export module Rev.Primitive.Group;

import Rev.Primitive;
import Rev.Primitive.FastLines;
import Rev.Core.Transform;
import Rev.Graphics.Canvas;

export namespace Rev::Primitives {

    using namespace Rev::Core;

    // Primitives drawn in their own units, placed in a parent group's units.
    // The group never touches the canvas transform (binding 0). It is given its
    // transform, composes it, and writes the result into each member's own
    // optional second transform. A parent's move costs one compose per group;
    // no member is recomputed or re-uploaded.
    struct Group : public Primitive {

        // One drawn member: a primitive through its own second transform, or a child group.
        struct Member {
            Primitive* primitive;                    // drawn in order
            FastLines::Xform* transform = nullptr;   // the primitive's own world -> pixel slot
            Group* group = nullptr;                  // or a child group, composing against this one
        };

        // What toPixel was composed from.
        struct Stamp {
            uint64_t parent = 0;      // the parent's revision (0 at the root)
            uint64_t placement = 0;   // this group's placement revision

            bool operator==(const Stamp&) const = default;
        };

        Group* parent = nullptr;          // not owned
        std::vector<Member> members;      // one ordered list, primitives and child groups;
                                          // not owned: producers own their primitives, as elements do

        Transform toParent = Transform::Identity();  // given: this group's units -> parent's units (px at the root)
        Transform toPixel = Transform::Identity();   // composed: this group's units -> logical px
        uint64_t placement = 1;           // bumps when toParent changes
        uint64_t revision = 0;            // bumps when toPixel changes
        Stamp composed;                   // what toPixel was composed from

        // Create
        Group(Canvas* canvas) : Primitive(canvas) {}

        // Destroy: unlink from both sides, so either may die first.
        ~Group() {
            if (parent) {
                parent->remove(this);
            }
            for (Member& member : members) {
                if (member.group) {
                    member.group->parent = nullptr;
                }
            }
        }

        // Nest a group here, in draw order with the primitives.
        void add(Group* child) {
            if (child->parent) {
                child->parent->remove(child);
            }
            child->parent = this;
            child->composed = {};
            members.push_back({ child, nullptr, child });
        }

        // Draw lines in this group's units: points move, strokes stay in px.
        // ~Primitive is not virtual, so the group never deletes through Primitive*.
        void add(FastLines* lines) {
            lines->transform = xform();
            members.push_back({ lines, &lines->transform, nullptr });
        }

        // The producer calls this in its destructor, before deleting the primitive.
        void remove(Primitive* primitive) {
            for (size_t i = 0; i < members.size(); i++) {
                if (members[i].primitive == primitive) {
                    members.erase(members.begin() + i);
                    return;
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

        // Recompose only when the parent or the placement moved.
        void compose() {
            Stamp now = { parent ? parent->revision : 0, placement };
            if (now == composed) {
                return;
            }
            composed = now;
            toPixel = parent ? parent->toPixel * toParent : toParent;
            revision++;
            for (Member& member : members) {
                if (member.transform) {
                    *member.transform = xform();
                }
            }
        }

        // toPixel in FastLines' per-axis form. Rotation and shear are not carried.
        FastLines::Xform xform() const {
            return { toPixel.m[0][0], toPixel.m[1][1], toPixel.m[0][2], toPixel.m[1][2] };
        }

        // Members in order; a child group composes against this one in its own draw.
        void draw() override {
            compose();
            for (Member& member : members) {
                member.primitive->draw();
            }
        }
    };
};
