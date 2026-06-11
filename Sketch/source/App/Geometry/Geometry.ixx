export module Sketch.App.Geometry;

import Rev.Core.Pos;

export import Geo.Geometry;

// Compatibility shim: the geometry kernel lives in the shared Geo library
// (./Geometry), consumed by both Sketch and CAM. Sketch code keeps addressing
// it as Sketch::App::* through these re-exported names.
export namespace Sketch::App {

    using Rev::Core::Pos;

    using Geo::Json;
    using Geo::PI;
    using Geo::TAU;

    using Geo::Id;
    using Geo::newId;
    using Geo::idToHex;
    using Geo::idFromHex;
    using Geo::Pos2;

    using Geo::ellipsePointAt;
    using Geo::ellipseOffsetPoint;
    using Geo::ellipseParamOf;
    using Geo::EllipseFit;
    using Geo::fitEllipse;
    using Geo::circumcentre;
    using Geo::wrapTau;
    using Geo::spanSteps;
    using Geo::sampleArc;
    using Geo::sampleEllipse;
    using Geo::closestOnSegment;
    using Geo::arcContains;
    using Geo::isectSegSeg;
    using Geo::isectSegArc;
    using Geo::isectArcArc;

    using Geo::SnapCandidate;
    using Geo::snapScore;

    using Geo::Stoicheion;
    using Geo::intersect;
    using Geo::Point2;
    using Geo::Segment2;
    using Geo::Circle2;
    using Geo::Arc2;
    using Geo::Ellipse2;
    using Geo::EllipseArc2;
    using Geo::OffsetEllipse2;
    using Geo::stoicheionFromJson;

    using Geo::PointRef;
    using Geo::Capacity;
    using Geo::DragPoint;
    using Geo::MotionField;
    using Geo::Relation;
    using Geo::readPoint;
    using Geo::writePoint;
    using Geo::Coincident;
    using Geo::Lock;
    using Geo::Distance;
    using Geo::Parallel;
    using Geo::Equal;
    using Geo::relationFromJson;

    using Geo::SketchGeometry;
}
