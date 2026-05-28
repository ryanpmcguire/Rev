module;

#include <cmath>
#include <algorithm>
#include <limits>

export module Cam.App.Slicer.Strategy.CutFrame;

import Rev.Core.Pos;
import Rev.Core.Pos3;

export namespace Cam::App::Slicer::Strategy {

    using namespace Rev::Core;

    // Local frame for axis-aware slicing.
    //
    // Slices are 2D in (u,v). Depth steps along axis from origin.
    struct CutFrame {

        Pos3 origin = {};
        Pos3 axis = { 0.0f, 0.0f, 1.0f };
        Pos3 u = { 1.0f, 0.0f, 0.0f };
        Pos3 v = { 0.0f, 1.0f, 0.0f };

        // Create
        //--------------------------------------------------

        static CutFrame fromAxis(const Pos3& axisIn, const Pos3& originIn = {}) {

            CutFrame frame;

            frame.origin = originIn;

            float len = axisIn.pythag();

            if (len <= 1e-6f) {
                frame.axis = { 0.0f, 0.0f, 1.0f };
            }
            else {
                frame.axis = axisIn / len;
            }

            Pos3 reference = (
                std::fabs(frame.axis.z) < 0.9f
                    ? Pos3(0.0f, 0.0f, 1.0f)
                    : Pos3(1.0f, 0.0f, 0.0f)
            );

            frame.u = reference.cross(frame.axis);

            float uLen = frame.u.pythag();

            if (uLen <= 1e-6f) {
                frame.u = { 1.0f, 0.0f, 0.0f };
            }
            else {
                frame.u /= uLen;
            }

            frame.v = frame.axis.cross(frame.u).normalized();

            return frame;
        }

        // Depth range
        //--------------------------------------------------

        void depthRange(const Pos3& min, const Pos3& max, float& minDepth, float& maxDepth) const {

            minDepth = std::numeric_limits<float>::max();
            maxDepth = std::numeric_limits<float>::lowest();

            for (int xi = 0; xi <= 1; xi++) {
                for (int yi = 0; yi <= 1; yi++) {
                    for (int zi = 0; zi <= 1; zi++) {

                        Pos3 corner = {
                            xi ? max.x : min.x,
                            yi ? max.y : min.y,
                            zi ? max.z : min.z
                        };

                        float depth = dotFromOrigin(corner);

                        minDepth = std::min(minDepth, depth);
                        maxDepth = std::max(maxDepth, depth);
                    }
                }
            }
        }

        // Coordinates
        //--------------------------------------------------

        Pos3 planeOrigin(float depth) const {
            return origin + axis * depth;
        }

        float dotFromOrigin(const Pos3& world) const {
            return (world - origin).dot(axis);
        }

        Pos worldToUv(const Pos3& world, float depth) const {

            Pos3 rel = world - planeOrigin(depth);

            return {
                rel.dot(u),
                rel.dot(v)
            };
        }

        Pos3 uvToWorld(const Pos& uv, float depth) const {
            return planeOrigin(depth) + u * uv.x + v * uv.y;
        }

        float uvAngle(const Pos& center, const Pos& point) const {
            return std::atan2(point.y - center.y, point.x - center.x);
        }
    };
}
