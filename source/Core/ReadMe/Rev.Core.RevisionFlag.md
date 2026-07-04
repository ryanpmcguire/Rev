# Rev.Core.RevisionFlag

`RevisionFlag` is a **monotonic logical clock** — and the claim this file makes is that
it is the *real* dirty flag, of which a boolean `DirtyFlag` is only a degenerate,
incomplete case.

## A dirty flag is a broken revision flag

Both mechanisms answer one question: *"has this changed since I last looked?"* They differ
in **where the comparison baseline lives**.

- **Revision flag** — the baseline lives in the *observer* (`RevisionObserver`). To
  acknowledge a change, the observer advances *its own* cursor. The source is never touched.
- **Dirty flag** — the baseline is pinned at `0` and lives *in the shared source itself*.
  There is no per-observer cursor to advance, so the only way to acknowledge a change is to
  drag the source back down (`dirty = false`).

That is the whole tragedy in one line: **a dirty flag acknowledges a read by mutating the
source.** A source mutated by every reader can, by construction, have only *one* reader —
the first to reset steals the signal from the rest. "I've seen this" and "this is now
clean" have been collapsed into the same write, so asynchronous, multi-consumer
reactivity isn't hard with dirty flags, it is *structurally impossible*.

So a dirty flag is a revision flag whose observer cursor has been welded into the source
and hard-coded to zero. One observer, baseline fixed. Degenerate in the strict sense.

## Why a *counter*, compared with `<`

There are three tiers of comparison, and only the third is sound:

1. **Boolean** (`> 0`) — degenerate, as above.
2. **Inequality** (`≠`) — looks like the fix, but isn't.
3. **Order** (`<`, *"am I behind?"*) — the real pattern.

Monotonicity (the count only ever moves up) makes staleness a **total order**, so the
*magnitude* of staleness carries no information — "behind by 1" and "behind by 500" are the
same fact: *behind*. This is what makes catching up **idempotent and coalescing**: a
consumer that missed 500 revisions does the work *once* and snaps its cursor to the head.
The cost of being out of date is bounded by one recompute, not by how out of date you got.

Inequality (`≠`) almost delivers this — but only while the source moves strictly up. The
moment an observer's cursor is somehow *ahead* of the source, `≠` reports "different,
therefore stale, therefore work," which is exactly backwards: the observer is ahead, has
nothing to do, and a `≠` test would schedule redundant work *and* drag its cursor
backward, arming itself to redo it. `<` answers the real question — *"is there something
newer than what I've seen?"* — and a value below the watermark correctly answers *no*.

## The invariant

**The count only ever moves up.** Every mutation path (`inc`, `set`, `operator++`,
`operator+=`, `operator=`) routes through `setCount`, which rejects any proposed value that
is not strictly greater than the current one (*cannot be the same, cannot be lower*).

Because that one guard is the only way the count changes, it is *also* the propagation
guard: when a flag advances it tries to advance every subscriber to its value, and the
same `<=` rejection fires on the way down. A target already at or above the incoming count
is untouched. **Propagation is monotonic by construction — the guard is the max**, so a
target fed by several sources can never be moved backward. (It tracks "something upstream
advanced," not a per-source count — a staleness signal, not an accounting ledger.)

This is the **high-water mark**, the same shape as a GPU timeline semaphore (a monotonic
`uint64` waited on with `>=`, which replaced the binary semaphore — itself a dirty flag),
GC epochs, and log-replication watermarks. Every system needing *many independent
observers of "has this advanced past where I care?"* converges on a monotonic counter
compared with an ordered operator.

## Callbacks and the dangling-pointer problem

`RevisionFlag` carries the same owner-keyed callback scheme as `Rev.Core.Dispatcher`. Each
callback stores the address of whoever registered it (its `this`):

- `onUpdate(owner, fn)` registers a callback owned by `owner`; the owner-less `onUpdate(fn)`
  is for callbacks that live as long as the flag itself.
- `unsubscribe(owner)` removes every callback that owner registered. A subscriber calls it
  with its own `this` in its destructor, so the flag can never fire into a destroyed
  object.

Callbacks fire on **every** genuine revision (not just a zero-crossing edge), because with
per-observer cursors there is no "stuck dirty" state to suppress — every advance is a real
change.

### Two kinds of subscriber, two dangling cases

1. **A foreign object subscribing a callback** (`onDirty`) — solved by the owner-keyed
   `unsubscribe(this)` above.
2. **A flag-to-flag connection** (`subscribe` / `sendsTo`) — here the *source* holds a raw
   `RevisionFlag*` in `targets`. `disconnect` removes it, but a flag does not track its own
   *sources*, so it cannot currently auto-disconnect on destruction. Closing this requires
   a source back-link (each flag remembering its sources so its destructor can disconnect
   from all of them), the way `Element` removes itself from its shared lists. Not yet done.

## RevisionObserver

A per-consumer **read cursor** against a `RevisionFlag`. Each pass or consumer owns one;
`changed(flag)` returns whether the source has advanced past the cursor, and advancing the
cursor is how a reader acknowledges work *without touching the shared source* — which is
the entire point. The test is ordered (`<`), so a cursor that is ahead of the source does
no work and is never dragged backward.

## check(token) — hosted cursors

`flag.check(this)` is the observer pattern with the cursor *storage* relocated into the
flag: a map of token → count-last-seen. A first visit records the current count and
reports dirty (a new checker has by definition never caught up); every later visit is the
same ordered (`<`) test, and the cursor snaps to the head either way — checking *is*
acknowledging. This is **not** the dirty-flag regression: each token has its own baseline,
so no checker can steal another's signal; only the storage moved.

Reach for it when one party tracks *many* flags (a supervisor checking each child's
revision): the cursors ride along with the children and die with them, instead of the
supervisor reconciling a parallel observer collection as children come and go.

The lifecycle discipline is the one callbacks already demand, and it is *naturally*
satisfied: a checker has, by the nature of the pattern, a persistent recurring interest in
the flag — transient passers-by have no business checking — so when the **checker** dies
first, it calls `unsubscribe(this)`, which now removes both its callbacks *and* its
cursor. When the **flag's owner** dies first there is nothing to do; the cursors die with
it.

## How not to use it

- **Don't reset the source to acknowledge a read.** Advance an observer cursor; that is the
  difference between this and a dirty flag.
- **Don't compare with `≠`.** Use the ordered test (`RevisionObserver` already does); an
  observer ahead of the source must do nothing.
- **Don't `onUpdate` without an owner** if the subscriber can outlive nothing / die before
  the flag — pass `this` and `unsubscribe(this)` in your destructor.
- **Don't rely on per-source accumulation.** A flag fed by multiple sources signals
  "something advanced," not how many times or by whom.
