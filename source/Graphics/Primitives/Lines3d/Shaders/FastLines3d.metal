// FastLines3d -- Metal port pending.
//
// Placeholder, mirroring Lines3d.metal: the active backend in this project is
// OpenGL, so this file is never compiled. The GPU polyline triangulation +
// per-fragment depth lives in FastLines3d.vert / FastLines3d.frag. When the
// Metal backend is brought up, port those two stages here (instanced segment
// expansion in pixel space, depth written via [[depth(any)]] from the
// interpolated NDC depth).
