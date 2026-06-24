# Rev — Essence

The other `About/` docs tell you the *model* (the element tree, the style system, the
graphics layer) and the reference docs tell you the *API*. This folder tells you the
**thesis**: what Rev actually is underneath all of that, why it's shaped the way it is,
and how to work *with the grain* instead of against it.

If you are an agent about to write Rev code, read these in order. They are short on
purpose. They will not teach you the `Style` fields (that's `Rev.Style.md`) or the event
virtuals (that's `Element.Events.md`) — they will teach you the **disposition** that makes
all of those feel easy instead of fiddly. Most mistakes in Rev are not API mistakes; they
are *disposition* mistakes — fighting the framework by doing too much.

## The files

1. **[What-Rev-Really-Is.md](What-Rev-Really-Is.md)** — the one big idea: Rev is
   *additive over a plain CLI*. Nothing is mandatory; the escape hatch goes *down*, not
   *out*; the abstraction has no lies in it. This is the mental model everything else
   hangs on.

2. **[How-To-Really-Use-Rev.md](How-To-Really-Use-Rev.md)** — the working discipline:
   declare structure once, mutate state directly, call `refresh(e)`, let the branch
   reconsider. When (and only when) to reach for `Observable`/dirty-gating. Why events are
   the *easy* part. How to handle dynamic children (reconcile, don't rebuild).

3. **[The-Escape-Staircase.md](The-Escape-Staircase.md)** — the rungs from "style a
   Box" down to "rewrite the shader every Box draws through" down to "use literally none
   of Rev." How to pick the right rung, with examples.

4. **[Dos-And-Donts.md](Dos-And-Donts.md)** — the concentrated checklist. The patterns
   that work and the anti-patterns that hurt, each with a one-line *why*.

## The single sentence

If you remember nothing else: **build the tree once, reflect live state into it, and
`refresh` after you change something — and whenever Rev seems to be in your way, the
answer is almost always to use *less* of it, not to fight it.**
