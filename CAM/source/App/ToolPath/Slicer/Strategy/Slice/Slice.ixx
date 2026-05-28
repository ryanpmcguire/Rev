module;

#include <vector>

export module Cam.App.Slicer.Strategy.Slice.Slice;

import Rev.Core.Pos;

import Cam.App.Slicer.Strategy.Slice.Segment2;
import Cam.App.Slicer.Strategy.Slice.Profile;

export namespace Cam::App::Slicer::Strategy::Slice {

    using namespace Rev::Core;

    struct Slice {

        float z = 0.0f;

        // Bounds of source segments in XY.
        Pos min = {};
        Pos max = {};
        bool valid = false;

        // Raw section edges at this Z height.
        std::vector<Segment> source;

        // Derived profiles used by strategies.
        Profile geometricProfile;
        Profile boundaryProfile;
        std::vector<Profile> profiles;

        // State
        //--------------------------------------------------

        void clear() {

            source.clear();
            resetProfiles();

            min = {};
            max = {};
            valid = false;
        }

        void resetProfiles() {

            geometricProfile.clear();
            boundaryProfile.clear();
            profiles.clear();
        }

        bool empty() const {
            return source.empty() && !hasProfiles();
        }

        bool hasProfiles() const {
            return !geometricProfile.empty() || !boundaryProfile.empty() || !profiles.empty();
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

        void includeSegment(const Segment& s, int samples = 24) {
            if (s.kind == Segment::Kind::Line) {
                includePoint(s.start());
                includePoint(s.end());
                return;
            }

            if (samples < 1) { samples = 1; }

            for (int i = 0; i <= samples; i++) {
                includePoint(s.pointAt(float(i) / float(samples)));
            }
        }

        void addSegment(const Segment& s) {

            if (!s.valid()) { return; }

            source.push_back(s);
            includeSegment(s);
        }

        void addLine(const Pos& a, const Pos& b) {
            addSegment(Segment::Line(a, b));
        }

        void setSource(const std::vector<Segment>& segments) {
            source.clear();
            resetProfiles();

            min = {};
            max = {};
            valid = false;

            for (const Segment& s : segments) {
                addSegment(s);
            }
        }
    };
}
