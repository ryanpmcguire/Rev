# Architecture

Build the app as a system of composed systems — each owns its domain and one job. Watch for these recurring failures:

- No god objects. If a type accumulates unrelated responsibilities (rendering + control + math + IO), split it.
- Respect layer direction. GUI reflects/asks; it does not contain machine or domain logic. Lower layers never reach up.
- The machine layer is a dispatcher over operations. It does not generate-and-send commands on its own. Operations own their command generation; the machine ticks them.
- One vocabulary per seam: Command (inward intents), Event (outward, decoded), Operation (a unit of work). Concrete machines own their wire dialect in the adapter; the wire (G-code, bytes) appears in exactly one place.
- Keep verb methods thin. A `jog`/action on the machine should be a thin interface to the operation that does the work.
- Don't add a parallel/second path for something that already has one (e.g. two ways to jog). Extend the existing path.
- Measure, don't assume. Before changing behavior, confirm how it actually works (read the code, the firmware's real primitives) rather than guessing.
