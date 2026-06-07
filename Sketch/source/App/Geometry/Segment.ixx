module;

#include <nlohmann/json.hpp>

export module Sketch.App.Segment;

export namespace Sketch::App {

    using Json = nlohmann::json;

    // A straight 2D line segment in world (sketch) coordinates. The most basic
    // sketch primitive; compound shapes are built from these later.
    struct Segment2 {

        double ax = 0.0, ay = 0.0;   // start point
        double bx = 0.0, by = 0.0;   // end point

        Json toJson() const {
            return Json{
                { "ax", ax }, { "ay", ay },
                { "bx", bx }, { "by", by }
            };
        }

        static Segment2 fromJson(const Json& j) {
            Segment2 s;
            s.ax = j.value("ax", 0.0);
            s.ay = j.value("ay", 0.0);
            s.bx = j.value("bx", 0.0);
            s.by = j.value("by", 0.0);
            return s;
        }
    };
}
