# Rev.Core.Reactivity — the relay: dispatchers and revision flags together

This is the companion to `Rev.Core.RevisionFlag.md`. That file argues *what a revision flag
is* (a monotonic logical clock; the real dirty flag). This file is about *how the reactive
primitives compose into a sane command-and-information structure* — how a change in one
corner of the program reaches everyone who cares, at the right level of detail, without
anyone drowning and without anyone left uninformed.

The primitives are small. The discipline is the whole thing. The mental model that makes
the discipline obvious is **a company with departments, managers, and a chain of
escalation.**

---

## The three primitives, and what each is *for*

- **`RevisionFlag`** — a *nudge*. A monotonic counter that says "something advanced past
  where you last looked; come look when convenient." It carries **no payload**, it
  **coalesces** (behind-by-1 and behind-by-500 are the same fact), and it **propagates
  upward** (`subscribe`/`sendsTo`, a max-watermark) so a parent flag aggregates its
  children automatically. This is the awareness channel. It is a *pull with a nudge*: the
  reader reaches back into the source for the detail.

- **`Dispatcher<Event>`** — an *addressed memo*. A typed push carrying a payload to owner-
  keyed subscribers. Use it when the information is **not retrievable** from a source the
  listener already has: decoded bytes at a boundary, a transient edge that leaves no state,
  a death notice, an addressed command ("go fix truck #7"). The payload is the point.

- **`Observable<T>`** — a *value that announces itself*. Store the truth in one place; let
  views reflect it. A convenience layer over the same idea (a value plus a change signal).

The single most important fact: **a `RevisionFlag` already is a dispatcher.** It has
`onUpdate(owner, fn)` (fires on every advance) *and* a count for gating. So if you find a
struct carrying a `RevisionFlag` **and** a `Dispatcher` that are fired together and the
dispatcher's payload is the source itself (or is ignored), the dispatcher is redundant —
the flag does both jobs. Reach for a payload dispatcher only when you can *name the payload
that could not be re-derived by reaching in.*

---

## The company

Picture the program as an organization. Each struct that owns meaningful state is an **org
unit**: an individual contributor (a field, an `Axis`), a team lead (a `CoordinateSystem`,
a `Part`), a department (a `Machine` — which *has* a person: its live controller liaison),
a division (a collection: `Machines`, `Projects`, `Tools`), the executive
(`ApplicationState`).

Information moves through this org the way memos move through a real one, and the health of
the design is exactly the health of that flow.

### Emit once; fan out freely

A memo — "the truck broke down" — is written **once**. Then anyone who cares subscribes,
and each subscriber does a **different job** with it:

- the CEO is *aware* (tracks it, acts on nothing),
- the fleet manager *recomputes* his count of available vehicles,
- the repairman is *dispatched* to that specific truck,
- the driver *changes his behavior* (don't drive it).

**The number of subscribers is never the problem.** Fan-out is the feature. Two things must
hold: there is **one canonical emission** of the event (the source does not maintain several
identical outboxes), and each subscriber has a **genuinely distinct role**. That is the
whole test for "is this redundancy healthy." Parallel redundancy — two channels carrying the
same signal to the same audience for the same purpose — is two managers sending the same
email. Hierarchical redundancy — the same event restated at each level up the chain, losing
detail and gaining meaning ("axis moved" → "a frame changed" → "the machine's definition
changed" → "there is unsaved work") — is not redundancy at all; it is the memo seen from
successively higher offices.

### Awareness vs. response — and one authority per memo

Subscribers split into two kinds:

- **Observers** want to *know* (repaint, recompute a summary, light an "unsaved" lamp).
  This is the `RevisionFlag` relationship: come look, no response required.
- **Handlers** must *act*, and typically need an **identity** to act on ("fix *#7*", not
  "fix a truck"). That identity is the one honest reason a payload rides along.

For every event you should be able to name the **single authority for the response** and be
honest about who is merely aware. Many observers, exactly one handler. The failure is not
too many recipients; it is:

- **Fragmented emission** — one real event pushed on several parallel channels.
- **Orphaned response** — everyone aware, nobody the authority; the event feels handled and
  isn't.
- **Duplicated response** — two handlers both act; they collide.
- **Reconstruction burden** — a subscriber forced to rebuild state that was retrievable
  (fix: nudge + reach in).
- **Wrong altitude** — the CEO wired to 500 leaf channels, or a leaf wired straight to the
  CEO. Subscribe at the altitude that matches your job: awareness → the summary board,
  action → the specific event. Direct-CC when fan-in is small; subscribe-to-the-summary
  when it is large.

### Escalate through levels; let the chain do the work

A unit reports to its **immediate manager**, not up the whole chain. The manager aggregates
(`subscribe` a child's board into the parent's) so one bump flows department → division →
executive automatically. When a leaf talks straight to the executive *and* to its manager,
that is an IC CC'ing both the manager and the VP on one memo — a small org smell. The tidy
shape: **report to your manager; let escalation happen by subscription.**

`ApplicationState` already models this: its `dirty` flag `subscribe`s the divisions'
(`machines`, `projects`, `tools`) flags. `app.dirty` is the executive dashboard — it never
watches a single tool; it watches its divisions, which watch their departments.

### Separate the operational feed from the definitional feed

The most consequential judgment. A department often has **two reporting lines that must
never merge**:

- the **operational** feed — high-frequency, ephemeral, *not saved* (a machine's live
  telemetry pose, `liveDirty`). The ops room watches it to keep the live picture current.
- the **definitional** feed — rare, meaningful, *saved* (a machine's `revision`: an edited
  frame, a renamed part). This is the one that escalates to "unsaved work."

If the shop-floor intercom (telemetry, 20 Hz) escalated onto the executive dashboard, the
app would believe it had unsaved work twenty times a second. Keeping them as separate lines
is the department correctly deciding that "the spindle moved" does not belong on the CEO's
desk while "we changed a work offset" does.

### The manager is the consistency boundary — "take stock, then post"

When a leaf changes, prefer handing the raw change to its manager and letting the **manager
reconcile and announce**, rather than the leaf announcing for itself. Two things fall out,
both real:

1. **Encapsulation.** Subscribers bind to the department's one public board and reach in
   through its public accessors — never to a leaf's internal wiring. The department can
   reshuffle its internals (add a frame, rename a part) without touching a single
   subscriber. You give reporters the press office, not the org chart.

2. **Coherence.** The manager makes *all* dependent state consistent — re-resolve matrices,
   re-fold visibility, re-derive downstream — **before** it posts to the board. So no
   subscriber ever catches the department half-updated: by the time anyone reaches in, the
   picture is settled. A leaf announcing for itself would announce mid-update. The manager
   can also *decide not to escalate* (debounce, or "that change matters to no one") — a
   filter the leaf can't apply because it lacks the whole picture.

So: **leaf mutates and reports up → manager reconciles → manager posts one summary → observers
fan out and reach in.**

### Reach for a per-leaf token only to *act*, not to *reflect*

"Anyone interested in a specific frame can check whether *theirs* changed" needs a checkable
token. The fork:

- **If reflecting is cheap** (re-reading a frame, re-posing a triad), give the subscriber
  **no** per-leaf token. On the department's "I updated," it just re-reads the one it cares
  about, unconditionally. Idempotent, no gating machinery. This is almost always right.
- **Only if a subscriber must *act* on the change** (not merely reflect), or the reflect is
  genuinely expensive, give that leaf its **own `RevisionFlag`** and let the subscriber hold
  a `RevisionObserver` on it. A *flag*, not a dispatcher: the flag is the cheap "did mine
  advance past where I last looked?" token, and unlike a payload it cannot be over-delivered
  or leak internal detail.

---

## Which primitive? — a decision guide

- **"Come look, something changed" (awareness, retrievable detail)** → `RevisionFlag` +
  `onUpdate` / `RevisionObserver`. Default. Reach into the source for the detail.
- **"Aggregate my children's changes into my status"** → `RevisionFlag::subscribe` the
  children into the parent. Escalation for free.
- **"This carries data that exists nowhere else yet"** (wire decode, cross-boundary edge) →
  `Dispatcher<Event>`. The payload is the point.
- **"Go do X to this specific thing"** (addressed command / handler needs identity) →
  `Dispatcher<Event>` carrying the identity, one authoritative handler.
- **"I am about to be destroyed; drop your reference"** → `Dispatcher<Event>` (a death
  notice; you cannot reach into a destroyed object).
- **"Store one truth, let views mirror it"** → `Observable<T>`.

---

## Worked examples in the codebase

- **Escalation done right.** `ApplicationState::dirty.subscribe(&machines.dirty)` (and
  projects, tools). The executive board aggregates the divisions.
- **Two feeds, kept apart.** `Machine::liveDirty` (telemetry pose — operational, never
  saved) vs `Machine::revision` (definition changed — escalates to "unsaved"). The world
  view watches the first to track the live spindle; the tree's Save button watches the
  second.
- **One authority, many observers.** A machine-definition edit: the **Save button** owns the
  response (persist); the tree and world view merely repaint; an app-level lamp is merely
  aware. One memo, distinct roles, exactly one handler.
- **The decode boundary (legit payload dispatchers).** The controller adapter fans decoded
  wire frames out on typed dispatchers (connection / telemetry / info / state / log). The
  payload is freshly decoded bytes with nowhere else to be retrieved from — the one place
  "delivered information" is correct.
- **A death notice.** `Stages::destroyDispatcher` — a subject announces its own destruction
  so subscribers drop their reference. You cannot reach into a destroyed object, so this
  earns its payload dispatcher.

---

## The one-line version

**Emit once, subscribe freely; for every memo name the single authority for the response and
be honest about who is merely aware; report to your manager and let escalation happen by
subscription; keep the operational feed off the executive's desk; and nudge-then-reach-in
unless the information genuinely cannot be retrieved.** Bureaucracy is making the aware ones
fill out forms; a free-for-all is the truck breaking down and four people each assuming the
others will fix it. The target is every memo with one owner and any number of readers.
