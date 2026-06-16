module;

export module Cam.CoordinateSystem.Axis;

// ------------------------------------------------------------------
// Cam::Axis
//
// ONE degree of freedom of a coordinate system, reduced to its essence: a value,
// how SURE we are of that value, and how FAST the machine can change it.  The
// axis does NOT know whether it is a translation or a rotation, nor which
// direction it points -- that is the owning CoordinateSystem's job (it holds
// named x/y/z/rx/ry/rz axes and supplies the directions/units).  This keeps Axis
// a pure, reusable scalar DOF.
//
//   certainty  -- [0,1].  How well the VALUE is known.
//                   0 == undefined (unknown; blocks the frame from being trusted)
//                   1 == fully certain (user-defined, or a perfect measurement)
//                 a probe sets it somewhere in between, from the fit confidence.
//
//   maxSpeed   -- how fast the MACHINE can drive this DOF (units/sec; translation
//                 is typically far faster than rotation).
//                   0 == LOCKED -- the machine cannot change it at all
//                  >0 == free -- a live axis the IK solves / the operator jogs
//
// The two are orthogonal: a rotary is free (maxSpeed>0) AND well-known
// (certainty high, from telemetry); a fixtured lateral offset is locked
// (maxSpeed 0) but may be unknown (certainty 0) until probed or taken on faith.
// ------------------------------------------------------------------

export namespace Cam {

    struct Axis {

        double value     = 0.0;   // mm or radians -- the owning frame decides which
        double certainty = 0.0;   // [0,1]: 0 = undefined, 1 = fully certain
        double maxSpeed  = 0.0;   // units/sec the machine can change it; 0 = LOCKED

        bool isUnknown() const { return certainty <= 0.0; }
        bool isKnown()   const { return certainty >  0.0; }
        bool isLocked()  const { return maxSpeed  <= 0.0; }
        bool isFree()    const { return maxSpeed  >  0.0; }   // machine can drive it
    };
}
