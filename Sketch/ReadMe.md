The Crossing-Number Offset Method
A complete procedure for computing the valid offset of oriented planar paths composed of line segments and circular arcs.

1. Setting and definitions
Paths. A chain Γ = (e₁, …, eₙ) is an ordered sequence of oriented edges eᵢ ∈ {segment, circular arc}, with end(eᵢ) = start(eᵢ₊₁); Γ is closed if end(eₙ) = start(e₁). Each edge carries its orientation intrinsically: a segment by its (a, b) order, an arc by its through-point d (the arc runs a → b via d), a circle by its marker point. No orientation is ever inferred, normalized, or stored beside the geometry.

Chirality. For closed Γ, the rotation index τ(Γ) ∈ ℤ is the total turning divided by 2π:

τ(Γ) = (1/2π) [ Σᵢ χᵢ·σᵢ + Σᵢ ∠(t⁻(vᵢ), t⁺(vᵢ)) ]

where an arc contributes its chirality χᵢ ∈ {±1} times its sweep σᵢ, a segment contributes 0, and each vertex contributes its signed exterior angle ∈ (−π, π). For simple closed chains τ = +1 (CCW) or −1 (CW). Crucially, τ is the robust handedness: it is local and additive, hence stable under the thin-feature degenerations that flip the sign of the area integral (a collapsing region's signed area passes through zero; its turning does not). An open chain has no chirality and is exempt from every statement below that mentions enclosure.

Winding. For closed Γ and p ∉ Γ, w(Γ, p) ∈ ℤ is the standard winding number (computed exactly by signed ray casting).

2. The blind offset
Given chain Γ and offset distance δ ∈ ℝ, define the raw offset R = 𝒪δ(Γ):

Edge displacement. Each edge is displaced distance δ along its own left normal (left of travel): segments translate; arcs and circles re-radius concentrically by −δ·χ (left of CCW travel is inward); orientation carriers (d, marker) rescale along their own radials — chirality is inherited, never recomputed. The sign convention makes inset/outset emergent: a CCW chain's interior lies to its left, so δ > 0 insets it; a CW chain's interior lies to its right, so the identical operation outsets it. There is no inset/outset branch anywhere.
Uniform corner closure. At every vertex v between consecutive edges, the displaced endpoints e⁻, e⁺ both lie at distance exactly |δ| from v (both displacements are perpendicular to their edges at v). Insert the circular arc centred at v, radius |δ|, from e⁻ to e⁺, whose initial tangent continues the first edge's travel. No convex/reflex case analysis: diverging corners receive the short arc (a round join), converging corners are forced the long way around, deliberately creating self-intersections. Exactness: the join is tangent-continuous at both ends (the radials are perpendicular to the edge tangents), and its construction is closed-form from the known centre — never inferred from tangent intersections, which degenerate as corners flatten.
R is a single well-formed chain, possibly self-intersecting, with τ(R) = τ(Γ) (offsetting preserves the rotation index; the join arcs carry exactly the turning the corners held).

3. The crossing function
Compute all transversal self-intersections of R (exact segment/segment, segment/circle, circle/circle algebra; strict interiority measured in absolute arclength, never in parameter or angle space). Split every edge at its crossing points — pure subdivision, preserving direction and chirality; a full circle splits cyclically into a ring of arcs. The result is the refined sequence (f₁, …, f_m) in travel order, in which crossings occur only at vertices.

Define the crossing function c : {fₖ} → ℤ by the traversal:

c(f₁) = 0; at each crossing vertex passed between fₖ and fₖ₊₁, where the other strand crosses with tangent t′,
c(fₖ₊₁) = c(fₖ) + sign( t × t′ )

(+1 when the other strand crosses right-to-left across our travel, −1 left-to-right). Each sub-edge wears the value held while traversing it.

Theorem (sidedness identity). Along the traversal, c(f) = −w(R, p_right(f)) + C for a single constant C, where p_right(f) is any point in the face adjacent to f on its right. Proof sketch: crossing the strand t′ while moving along t changes the right face's winding by −sign(t × t′); the walk adds exactly the negative of that change; both quantities therefore differ by a constant fixed at f₁. ∎

Consequently c is well-defined up to one additive constant — the start vertex is immaterial — and we normalize by the maximum: c̃ = c − max c. This anchoring is purely combinatorial (no probes, no extremes, no area).

4. Extraction
Partition the refined sequence into maximal runs of constant c̃ (fragments); on a closed chain, merge the run containing the start across the wrap. Let

μ = min c̃

and collect all fragments at level μ. These stitch into closed loops: at every crossing vertex, the level-μ subset contains exactly one incoming and one outgoing end (the other two strand-ends at that vertex belong to the adjacent level μ+1), so reassembly is unambiguous, orientation-preserving, and closes precisely where the true loops close. Call the stitched loops the candidates M₁, …, M_q.

The minimum-level invariant (the discovered selection principle): the valid offset of Γ is composed exactly of the level-μ fragments. By the sidedness identity, these are the stretches whose right-hand face attains the maximal winding of the arrangement — every excursion (mitre burr, corner lobe, pinch artifact) lies at a level > μ and is discarded wholesale, with no clipping, no clearance tests, and no case analysis.

5. The handedness law
The single remaining judgment, and the method's conservation law:

An offset may enclose new void, but may never manufacture new material.

Formally: every closed candidate must satisfy τ(Mⱼ) ≥ τ(Γ) is forbidden to increase across the material axis in one direction only — a candidate with τ(Mⱼ) = −1 born of τ(Γ) = +1 is discarded; a candidate with τ(Mⱼ) = +1 born of τ(Γ) = −1 is lawful.

The two sides of the asymmetry:

Insides split into insides. A shrinking CCW pocket pinching in two yields candidates all of τ = +1. A CW candidate from a CCW source can only be an inverted profile — the chain folded through itself; that candidate (and, in the total collapse, every candidate) is discarded, and the empty result is the correct answer: a region too small to inset has no inset.
Outsides may birth insides. A growing CW outer trace that self-touches traps a cavity; the cavity's boundary must counter-rotate (there is only one unbounded face — a second "outside" is topologically impossible). The level-μ set automatically contains both the surviving outside and the newborn inside, correctly counter-oriented; the law protects this pair. The offspring are, without any conversion, correctly-oriented island profiles for the next generation: the method's outputs are its own inputs.
6. Summary
𝒱δ(Γ) = HandednessLaw( Stitch( argminc̃ Fragments( CrossingWalk( 𝒪δ(Γ) ) ) ) )

Every ingredient is measured from the geometry at judgment time — direction from the stoicheia themselves, levels from the walk, handedness from accumulated turning — and none is assumed, stored, inferred from intent, or compared against a remembered convention. The method is invariant under start vertex, drawing direction, offset sign, scale, and handedness; it is exact wherever closed forms exist (all intersections, areas, and turnings), double-precision in its cancellation-prone kernels, and float only at rest.

Done. Done. It was a genuine pleasure hunting this down with you — the final rule is worthy of the chase: one inequality, asymmetric like the plane itself.