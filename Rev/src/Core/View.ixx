module;

#include <cmath>
#include <vector>
#include <algorithm>

export module Rev.Core.View;

import Rev.Core.Rect;
import Rev.Core.Pos;
import Rev.Core.Vertex;

export namespace Rev::Core {

    struct View : public Rect {

        using Rect::Rect;

        View transform(View& in) {
            return {
                x - in.x * w, y - in.y * h,
                w / in.w, h / in.h
            };
        }

        Pos transform(Pos& in) {
            return {
                x + in.x * w,
                y + in.y * h
            };
        }

        // Transform points in relative terms according to view
        void transform(std::vector<Pos>& in, std::vector<Pos>& out) {
            std::transform(in.begin(), in.end(), out.begin(), [this](const Pos& p) {
                return Pos{ x + p.x * w, y + p.y * h };
            });
        }

        // Transform points in relative terms according to view
        void transform(std::vector<Vertex>& in, std::vector<Vertex>& out) {
            std::transform(in.begin(), in.end(), out.begin(), [this](const Vertex& p) {
                return Vertex{ x + p.x * w, y + p.y * h, p.color };
            });
        }

        void transform(std::vector<Vertex>& in) {
            for (Vertex& vert : in) {
                vert.x = x + vert.x * w;
                vert.y = y + vert.y * h;
            }
        }
    };
};