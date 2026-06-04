module;

#include <vector>
#include <algorithm>
#include <cmath>

export module Cam.Gui.ToolPathPreview;

import Cam.App.Project;
import Cam.App.Stage;
import Cam.App.ToolPath;

export namespace Cam::Gui {

    struct PreviewSegment {
        Cam::App::Stage* state = nullptr;
        double startSeconds = 0.0;
        double durationSeconds = 0.0;
    };

    struct ToolPathPreviewTimeline {

        std::vector<PreviewSegment> segments;
        double totalDurationSeconds = 0.0;
        double elapsedSeconds = 0.0;

        struct LocateResult {
            size_t segmentIndex = 0;
            double localProgress = 0.0;
            bool atSegmentEnd = false;
            bool valid = false;
        };

        // Forward machining time runs opposite to storage index order:
        // higher index = earlier op, lower index = later op (index 0 = final).
        static bool forwardTimeBefore(
            Cam::App::Project* project,
            Cam::App::Stage* a,
            Cam::App::Stage* b
        ) {
            if (!project || !a || !b) { return false; }

            return project->indexOf(a) > project->indexOf(b);
        }

        static std::vector<Cam::App::Stage*> previewSequence(
            Cam::App::Project* project
        ) {

            std::vector<Cam::App::Stage*> sequence;

            if (!project) { return sequence; }

            auto collect = [&](Cam::App::Stage* state) {

                if (!state || !state->hasToolPath) { return; }

                sequence.push_back(state);
            };

            if (project->viewSelection.size() > 1) {

                for (Cam::App::Stage* state : project->viewSelection) {
                    collect(state);
                }
            }
            else {
                Cam::App::Stage* fallback = nullptr;

                if (project->displayedStage && project->displayedStage->hasToolPath) {
                    fallback = project->displayedStage;
                }

                if (!fallback) {
                    Cam::App::Stage* primary = project->primaryViewStage();

                    if (primary && primary->hasToolPath) {
                        fallback = primary;
                    }
                }

                if (!fallback) {
                    for (Cam::App::Stage* state : project->viewSelection) {
                        if (state && state->hasToolPath) {
                            fallback = state;
                            break;
                        }
                    }
                }

                if (!fallback) {
                    for (Cam::App::Stage* state : project->stages) {
                        if (state && state->hasToolPath) {
                            fallback = state;
                            break;
                        }
                    }
                }

                collect(fallback);
            }

            std::sort(
                sequence.begin(),
                sequence.end(),
                [project](
                    Cam::App::Stage* a,
                    Cam::App::Stage* b
                ) {
                    return forwardTimeBefore(project, a, b);
                }
            );

            return sequence;
        }

        void rebuild(Cam::App::Project* project) {

            segments.clear();
            totalDurationSeconds = 0.0;

            if (!project) { return; }

            const std::vector<Cam::App::Stage*> sequence =
                previewSequence(project);

            double start = 0.0;

            for (Cam::App::Stage* state : sequence) {

                if (!state) { continue; }

                project->ensureToolPathComputed(state);

                const double duration = state->toolPath.durationSeconds();

                segments.push_back({
                    .state = state,
                    .startSeconds = start,
                    .durationSeconds = duration
                });

                start += duration;
            }

            totalDurationSeconds = start;
            elapsedSeconds = std::clamp(elapsedSeconds, 0.0, totalDurationSeconds);
        }

        void setElapsed(double seconds) {
            elapsedSeconds = std::clamp(seconds, 0.0, totalDurationSeconds);
        }

        bool atEnd() const {
            return elapsedSeconds >= totalDurationSeconds - 1e-9;
        }

        LocateResult locate() const {

            LocateResult result = {};

            if (segments.empty()) {
                return result;
            }

            result.valid = true;

            if (elapsedSeconds <= segments.front().startSeconds + 1e-9) {
                result.segmentIndex = 0;
                result.localProgress = 0.0;
                result.atSegmentEnd = segments.front().durationSeconds <= 1e-9;
                return result;
            }

            if (elapsedSeconds >= totalDurationSeconds - 1e-9) {
                result.segmentIndex = segments.size() - 1;
                result.localProgress = 1.0;
                result.atSegmentEnd = true;
                return result;
            }

            for (size_t i = 0; i < segments.size(); i++) {

                const PreviewSegment& segment = segments[i];
                const double segmentEnd = segment.startSeconds + segment.durationSeconds;

                if (elapsedSeconds > segmentEnd + 1e-9) { continue; }

                result.segmentIndex = i;

                if (segment.durationSeconds <= 1e-12) {
                    result.localProgress = 1.0;
                    result.atSegmentEnd = true;
                }
                else {
                    const double localTime = elapsedSeconds - segment.startSeconds;
                    result.localProgress = localTime / segment.durationSeconds;
                    result.atSegmentEnd = localTime >= segment.durationSeconds - 1e-9;
                }

                return result;
            }

            result.segmentIndex = segments.size() - 1;
            result.localProgress = 1.0;
            result.atSegmentEnd = true;

            return result;
        }

        double pathProgressForState(
            Cam::App::Stage* state,
            Cam::App::Project* project
        ) const {

            if (!state || !project) { return 0.0; }

            for (const PreviewSegment& segment : segments) {

                if (segment.state != state) { continue; }

                if (segment.durationSeconds <= 1e-12) {
                    return elapsedSeconds >= segment.startSeconds ? 1.0 : 0.0;
                }

                const double localTime = elapsedSeconds - segment.startSeconds;

                return std::clamp(localTime / segment.durationSeconds, 0.0, 1.0);
            }

            const size_t stateIndex = project->indexOf(state);

            if (stateIndex == static_cast<size_t>(-1)) { return 0.0; }

            const LocateResult here = locate();

            if (!here.valid || here.segmentIndex >= segments.size()) {
                return 0.0;
            }

            const size_t activeIndex = project->indexOf(segments[here.segmentIndex].state);

            if (activeIndex == static_cast<size_t>(-1)) { return 0.0; }

            // Lower index = later in forward time; already finished once we pass it.
            if (stateIndex < activeIndex) { return 1.0; }

            return 0.0;
        }

        float sliderPercent() const {

            if (totalDurationSeconds <= 1e-12) { return 100.0f; }

            return float(
                (elapsedSeconds / totalDurationSeconds) * 100.0
            );
        }

        void setFromSliderPercent(float percent) {

            const double scale = std::clamp(double(percent) / 100.0, 0.0, 1.0);
            setElapsed(scale * totalDurationSeconds);
        }
    };
}
