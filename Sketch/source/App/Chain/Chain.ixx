export module Sketch.App.Chain;

export import Geo.Chain;

// Compatibility shim: chains, profiles and the offsetting method live in the
// shared Geo library (./Geometry), consumed by both Sketch and CAM. Sketch
// code keeps addressing them as Sketch::App::* through these re-exports.
export namespace Sketch::App {

    using Geo::dPos;
    using Geo::dTAU;
    using Geo::dWrap;

    using Geo::Chain;
    using Geo::Profile;
}
