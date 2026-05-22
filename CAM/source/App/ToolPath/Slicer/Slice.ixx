module;

#include <vector>

export module Cam.App.ToolPath.Slicer.Slice;

import Rev.Core.Pos;

import Cam.App.Geometry.Segment;
import Cam.App.Geometry.Chain;
import Cam.App.Geometry.Profile;

export namespace Cam::App::ToolPath::Slicer {

    using namespace Rev::Core;
    using namespace Cam::App::Geometry;

    struct Slice {

        float z = 0.0f;

        Pos min = {};
        Pos max = {};
        bool valid = false;

        std::vector<Segment> source;
        std::vector<Profile> profiles;
        std::vector<Segment> paths;
        std::vector<Pos> points;

        virtual ~Slice() = default;

        virtual void solve() = 0;

        // State
        //--------------------------------------------------

        void clear() {

            source.clear();
            profiles.clear();
            paths.clear();
            points.clear();

            min = {};
            max = {};
            valid = false;
        }

        bool empty() const {
            return source.empty() && paths.empty() && points.empty();
        }

        bool hasPointPath() const {
            return !points.empty();
        }

        // Bounds
        //--------------------------------------------------

        void includePoint(const Pos& p) {

            if (!p) { return; }

            if (!valid) {
                min = p;
                max = p;
                valid = true;
                return;
            }

            min = Pos::min(min, p);
            max = Pos::max(max, p);
        }

        void includeSegment(
            const Segment& s,
            int samples = 24
        ) {
            if (s.kind == Segment::Kind::Line) {
                includePoint(s.start());
                includePoint(s.end());
                return;
            }

            if (samples < 1) { samples = 1; }

            for (int i = 0; i <= samples; i++) {
                includePoint(
                    s.pointAt(float(i) / float(samples))
                );
            }
        }

        void addSegment(const Segment& s) {

            if (!s.valid()) { return; }

            source.push_back(s);
            includeSegment(s);
        }

        void addLine(
            const Pos& a,
            const Pos& b
        ) {
            addSegment(
                Segment::Line(a, b)
            );
        }

        void setSource(
            const std::vector<Segment>& segments
        ) {
            clear();

            for (const Segment& s : segments) {
                addSegment(s);
            }
        }

        // Profile helpers
        //--------------------------------------------------

        Profile makeProfile() const {

            return Profile::FromSegments(
                source
            );
        }

        void collectProfile(
            const Profile& profile
        ) {
            for (const Profile::Entry& entry : profile.entries) {

                for (const Segment& s : entry.chain.segments) {
                    paths.push_back(s);
                }
            }
        }
    };
}
