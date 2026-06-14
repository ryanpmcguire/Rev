# Carvera Protocol Notes

A distilled reference for working with the Carvera (Makera) ATC desktop CNC
from the application side.  Co-located with `CarveraAir.ixx` so future
sessions can come up to speed without re-deriving the protocol by
experiment.  Anything noted as **observed** has been confirmed in this
project's session logs; anything noted as **community / inferred** is from
the Carvera Community Controller source and Smoothieware behaviour.

## Machine basics

- 3- or 4-axis desktop mill from Makera.  Working envelope ~360x240x140 mm
  (3-axis) with an optional 4th rotary (A).
- 6-slot **automatic tool changer (ATC)** with touch-off probe.
- Firmware: **Smoothieware fork** (Cortex-M4), with Carvera/Makera-specific
  M-codes and ATC orchestration on top.
- Transport: Wi-Fi TCP socket, default port **2222**.
- Coordinate systems: standard Grbl/Smoothie (G54..G59 WCS + G53 machine).
  `$J` (jog) honours `G90`/`G91` in the active WCS; absolute machine targets
  require an explicit `G53` prefix.

## Real-time bytes (single bytes, no newline)

These are processed out-of-band -- they can be sent mid-line and act
immediately, regardless of buffer state.

| Byte   | Hex  | Meaning                                            | Used in code |
|--------|------|----------------------------------------------------|--------------|
| `?`    | 0x3F | Status query (Grbl).  Returns `<...>` frame.       | `sendStatus` (confirmed) |
| `!`    | 0x21 | Feed hold (Grbl) -- decelerate, hold.               | `stop` (confirmed) |
| `~`    | 0x7E | Cycle start / resume from feed hold (Grbl).        | `stop` (combined w/ reset) |
| `*`    | 0x2A | "Play / continue" (Smoothie extension).            | tried -- does NOT release M490.1 |
| 0x18   |      | Soft reset / Ctrl-X (Grbl).                        | `stop` (confirmed) |
| 0x85   |      | Jog cancel (Grbl 1.1+).  Decelerates active jog.   | `jogCancel` (confirmed) |

### M490.1 confirm command -- UNKNOWN (probe mode)

We do NOT know what command releases the ATC `M490.1` wait state on this
Carvera firmware build.  Confirmed wrong: `~` (0x7E), `*` (0x2A).  Both are
delivered cleanly to the controller (other commands sent the same way work)
but neither makes the machine leave "Tool" state.

`confirmToolChange()` in `CarveraAir.ixx` is currently in **probe mode**:
each Ok-button click sends the next candidate from a fixed table and logs
which one it sent.  The table tries, in order:

1. `*`  (0x2A) -- Smoothie play/continue
2. `~`  (0x7E) -- Grbl cycle start
3. `\n` -- bare newline
4. `M600\n` -- Marlin/Smoothie filament-change resume
5. `M601\n` -- Smoothie continue from pause
6. `M0\n`   -- program stop / skip pause
7. `M6\n`   -- re-issue bare tool change
8. `M491\n` -- Carvera M-code adjacent to M490
9. `M492\n` -- Carvera M-code adjacent to M490
10. `M493\n` -- Carvera tool-length probe
11. `M495\n` -- Carvera ATC sub-op
12. `M496\n` -- Carvera ATC sub-op

**How to use the probe**: at the Standby prompt, keep clicking Ok.  After
each click, watch the debug log: the line that immediately precedes a

```
[Air] machine state 'Tool' -> 'Run'
```

transition is the command that worked.  Take that string and replace the
probe loop in `confirmToolChange()` with a single direct send.  Then update
the byte table above (cross off the wrong ones, mark the right one
"confirmed") and remove this whole probe section.

If the entire table is exhausted with no response, the M490.1 wait may only
be releasable via the physical button GPIO and there is no serial path --
in which case the GUI's Ok button has to read "press the machine's confirm
button to continue".  Worth checking the Carvera Community Controller's
source on GitHub for whatever it sends for its OK button before concluding
that.

## ATC tool change cycle

Manual change driven from the app (the program-orchestration path is the
same shape):

```
host -> "M5"            ; spindle off -- bare M6 is rejected while spinning
host -> "G4 P2"         ; let spindle decelerate
host -> "M6 T<n>"       ; request slot n
machine -> "Please change the tool to: T<n>"
machine -> "G53 G0 Z-3.000"          ; lift to clearance
machine -> "G53 G0 X.. Y.."           ; move to ATC area
machine -> "M497.2"                   ; ATC sub-op (carousel move / position)
state '...' -> 'Tool'
machine -> "M490.1"                   ; WAIT for confirm (button or 0x2A)
                                     ; -- operator inserts tool --
host -> 0x2A                          ; the GUI Ok button
state 'Tool' -> 'Run' (touch-off begins)
                                     ; -- touch-off / probe --
state '...' -> 'Idle' (machine has returned)
```

Mapping onto Air's `ToolChangePhase` (see `CarveraAir.ixx::updateToolChangeGate`):

| Gate | Phase        | Enter on                                    |
|------|--------------|---------------------------------------------|
| 0    | None         | initial / after Complete                    |
| 1    | Seeking      | `M6` sent (host action)                     |
| 2    | Standby      | state becomes `"Tool"`                      |
| 3    | Confirming   | state leaves `"Tool"` (any non-Tool report) |
| 0    | None (= Complete) | state has been `Idle`/`Alarm` for ~500 ms |

Gotchas:

- **Don't send `M6 T<n>` if T<n> is already loaded.**  The Carvera silently
  no-ops, the gate sits in Seeking forever, and the UI gets stuck.  Air
  refuses this in `changeTool` by comparing `slot == loadedSlot.load()`.
- **The Confirming -> None transition needs a settle.**  During touch-off
  the state cycles `Tool -> Run -> Tool -> Run -> Idle`; the first brief
  non-`Tool` window is the *start* of touch-off, not the end.  Air waits for
  a sustained Idle (~70 consecutive 7 ms ticks ~ 500 ms) before firing
  Complete.
- **Auto-confirm is gone on purpose.**  The operator must explicitly press
  either the physical button on the machine or the GUI Ok -- there's no
  "auto-press" path.  Both follow the same code path because the gate
  advances from machine state, not from whichever input did the confirm.

## Status frame

```
<Idle|MPos:0.000,0.000,0.000,0.000|WPos:0.000,0.000,0.000,0.000|FS:0,0|...>
```

Parsed by `Air::processLine`:

- **state**: first field after `<` and before `|`/`,`/`>`.  Known values:
  `Idle`, `Run`, `Hold`, `Jog`, `Tool`, `Alarm`, `Sleep`, `Home`.
- **MPos**: 3 or 4 floats -- machine absolute position.  This is what
  `livePosition()` returns and what jog math is anchored on.
- **WPos**: 3 or 4 floats -- same point in the active WCS.  The 4th field
  (A) is the WCS-relative rotary angle -- fed straight into the part
  rotation transform in WorldView, no offset math needed.

## Parser-state reply (`$G`)

Sent once after every successful connect (see `client->onConnect` in Air):

```
[G0 G54 G17 G21 G90 G94 M0 M5 M9 T0 F3000.0000 S1.0000]
```

`Air::pickToolFromAnywhere` scans bracketed and status lines for `T<n>` at a
token boundary so the loaded tool is recovered without waiting for a
successful change.  This also catches manual tool changes done at the
machine itself (the operator pressing physical buttons).

## Jog (`$J=...`)

```
$J=G91 X10.000 F1000      ; relative jog
$J=G90 X120.000 F1000     ; absolute jog (in active WCS -- NOT machine coords)
```

Key facts:

- New `$J` lines **queue** behind any in-flight jog; they do NOT replace it.
  Combine with `0x85` (jog cancel) to stop and re-aim.
- `G53` is NOT a valid modal inside a jog line -- absolute machine targets
  must be done via plain `G53 G0 ...` (rapids), not `$J`.
- Soft limits are checked at planning time.  Issuing `$J=G91 X10000 ...`
  triggers an alarm even though the user just meant "go far in +X" --
  always target a reasonable chunk (e.g. 25 mm) within the envelope.  See
  `JogSection::issueLegJog` / `maybeExtendLeg` for the chunk-then-extend
  pattern.

## Probing (`G38.2` + `[PRB:...]`)  -- FIRST STEP, in progress

The wired touch probe is exposed in the app via a **temporary "PROBE" button**
in the jog grid (`JogSection::build` -> `Air::probeTest`).  The flow is
**two-phase and sequenced** -- the probe move is NEVER sent in the same burst
as the tool change:

```
; -- phase 1: ensure the probe tool is loaded (only if not already) --
host -> "M5" / "G4 P2" / "M6 T<probe>"   ; via the gated changeTool()
machine -> "Please change the tool to: T<probe>"   ; ATC cycle runs, gate tracks it
        ... gate walks Seeking -> Standby -> ... -> Complete (sustained Idle) ...

; -- phase 2: fired from tcComplete(), only after the change fully finishes --
host -> "G91"                 ; relative frame so the target is a delta
host -> "G38.2 Z-50.000 F100" ; probe straight down up to 50 mm at F100
host -> "G90"                 ; restore absolute
machine -> "[PRB:0.000,0.000,-12.345:1]"   ; trigger! flag 1 = contacted
                                           ; flag 0 = reached target, no touch
                                           ;   (a "probe fail" -> also alarms)
```

`probeTest()`: if the probe tool is already loaded it probes immediately;
otherwise `ensureProbeTool()` starts a gated `changeTool(kProbeToolSlot)` and
sets `probePending_`, which `tcComplete()` consumes to fire the `G38.2`.  A
deferred probe is cancelled if the change aborts (`tcAbort`).

> **!! CRITICAL UNRESOLVED: `M6 T0` is NOT "select the probe".**  Observed on
> the real machine (2026-06-05): with T2 loaded, `M6 T0` made the Carvera
> **drop T2 and rapid to the ATC area** -- i.e. T0 means *"unload to an empty
> spindle"*, not *"pick up the wired probe"*.  (The earlier crash was a
> SEPARATE bug -- the `G38.2` was injected mid-ATC; that is now fixed by the
> two-phase sequencing above.)  The correct probe selector is still UNKNOWN:
> the community 3D probe registers as `T999990` but that is a *logical* tag,
> and `pickToolFromAnywhere` won't even record it (it caps recorded slots at
> 99).  **Next:** confirm how this machine selects the wired probe -- it may
> not be an ATC `M6` at all (the wired probe may simply be plugged in and its
> input always live, in which case `kProbeToolSlot` / the whole tool-change
> step should be dropped and we probe directly).

Key facts (**community / inferred** unless noted):

- **`G38.2`** = "probe toward target, stop on contact". Implemented on the
  Carvera's Smoothie fork (G38.2--G38.5; G38.4/.5 probe X/Y). The controller
  decelerates and stops *itself* the instant the probe closes -- the host does
  NOT poll position to catch the touch.
- The result comes back as a single bracketed **`[PRB:x,y,z:flag]`** line.
  `Air::processLine` parses it -> `ProbeEvent` (`onProbe`) + a "Probe
  TRIGGERED/FAIL" log line. x/y/z are MACHINE coords at contact.
- A probe that reaches its target untriggered is a **fail**: Smoothie reports
  `flag 0` and raises an alarm. Recover with `$X` / reset like any alarm.
- **Probe tool slot is UNCONFIRMED.** `Air::kProbeToolSlot` is currently `0`
  (the bare wired probe on the original Carvera). The community 3D probe is the
  pseudo-slot **`T999990`** (`M6 T999990`). If `M6 T0` doesn't select the probe
  on this machine, try 999990 and update the constant.
- The Carvera firmware *also* ships higher-level probe macros (**`M461`** bore,
  **`M464`** ZProbe, **`M465.x`** 4th-axis, **`M466.x`** bed-level/rect). The
  community controller found `M464` unreliable on the **Carvera Air** and
  recommends issuing **`G38.2` + `G10 L20`** directly instead -- which is why
  this first step uses raw `G38.2` rather than a Carvera macro. See
  Carvera_Controller issue #269.

### Spindle safety interlock (HARD requirement)

A probe must NEVER be spun.  Air enforces a **spindle interlock** that is the
single source of truth for "is the spindle allowed to start right now":

- `Air::spindleInhibited_` latches ON whenever the loaded tool is a probe /
  spindle-disabled slot (`isProbeSlot` -- currently slot 0 and `>= 999990`),
  and conservatively whenever the loaded tool is unknown (slot 0 on a fresh
  connect).  It follows `recordLoadedTool`, so swapping a real cutter back in
  releases it automatically.
- `requestProbeTool()` engages the latch and forces `M5` **before** the `M6` so
  the spindle is provably off before the probe is in.  It does NOT force-disarm:
  the interlock (not the armed flag) is what guarantees safety.
- Every outgoing line passes `spindleGuardBlocks()` (via `rawSend`/`sendLine`)
  and the program stream passes the same check in `pump()`.  Any `M3`/`M4`
  while inhibited is **refused** (`commandsSpindleOn` matches M3/M4/M03/M04 but
  not M5/M30): the line is dropped, an `M5` is forced out, a `SafetyEvent`
  (`onSafety`) fires, and the running program is stopped.
- **Arming is a USER control, never blocked.** `setSpindleArmed(true)` is always
  allowed, probe loaded or not -- arming is intent, not motion.  The interlock at
  the point of motion (above) is the single guarantee the spindle never spins.
  This decouples "the user armed the spindle" from "the machine may spin it now".
- The GUI reflects it: the ArmSection spindle button shows **"SPINDLE ARMED
  (HELD)"** when armed with a probe loaded (armed by intent, held off by the
  interlock); `onSafety` surfaces any refused spin attempt.

If you add any new code path that emits G-code to the controller, it MUST go
through `rawSend`/`sendLine` (or the guarded `pump` site) -- do not call
`client->send()` directly with anything that could start the spindle.

Open questions for the next step:

- Confirm the probe tool slot (0 vs 999990) on the real machine.
- After a clean trigger, set WCS with `G10 L20 P1 Z<known-offset>` to turn the
  touch into a usable Z zero.
- Decide whether to drive probing through the action queue (like tool changes)
  or keep it a discrete operator action.

## Custom Carvera M-codes spotted

| Code     | Meaning (inferred)                                         |
|----------|------------------------------------------------------------|
| M490     | Wait for tool-change confirmation                          |
| M490.1   | Same, ATC variant                                          |
| M493     | Tool length probe / touch-off                              |
| M461     | Probe workpiece feature (e.g. bore diameter)               |
| M464     | ZProbe macro (unreliable on Carvera Air -- prefer G38.2)   |
| M465.x   | 4th-axis stock probe                                       |
| M466.x   | Bed-level / rectangular multi-point probe                  |
| M497     | ATC carousel ops (move-to-pickup / rotate / drop)          |
| M497.2   | An ATC sub-op observed during M6 sequencing                |

These are emitted *by the machine* during an ATC cycle and echoed back in
the output stream.  We don't send them from the host; we just see them
flow past in the log.

## Start flow / separation of concerns (the "operation by operation" model)

One direction, one owner per concern:

```
GUI (ArmSection START)            -- asks:    Air::requestStart()
Air                               -- owns:    preflight -> provider -> validate -> stream
CAM (WorldView::buildExecuteOperations)
                                  -- builds:  typed Operations (cut/probe) on demand
```

- The CAM view registers `Air::operationProvider` (a builder, set/cleared
  across WorldView's lifetime).  It NEVER streams; it returns a
  `std::vector<Operation>` (empty = nothing to run).
- `Air::requestStart()` is the single start pipeline: `preflightStart()`
  (connected, armed, no tool change, not Alarm/Hold, not executing, **work
  origin set this session**), then asks the provider, then
  `validateOperations()`, then `enqueueOperations()` (which validates again
  -- defence in depth).  Every refusal carries the exact operator-facing
  reason.
- `validateOperations` refuses any program containing a non-finite
  coordinate / feed / rpm or an invalid tool slot.  A NaN that reaches the
  controller formats as "nan", is rejected, and ALARMS instantly with
  nothing in the log -- this validator is what turns "the machine alarms for
  no reason" into a named, fixable refusal.
- The work-origin preflight exists for the same reason: the program is in
  WCS relative to the begin-work point; executing against a stale/unset WCS
  is the other classic instant-alarm/crash.
- Probe operations are built with `NoToolChange` (-1): until the real probe
  selector is confirmed (see the critical `M6 T0` note above), the operator
  fits the wired probe by hand and the program never issues an M6 for it.
  KNOWN GAP: with a hand-fitted probe the loaded-slot still reports the
  previous cutter, so the spindle interlock is NOT latched during the probe
  op -- the op itself never commands M3 (buildSteps only spins for Cut ops),
  but resolving the probe selector remains the real fix.
- The legacy `Air::onStartRequested` callback is GONE; `onTelemetry` (the
  high-rate display push) is the only remaining single-callback hook.

## Coordinate conventions in our code

- All telemetry (`livePosition`, `currentConfirmed`) reports **MPos**.
- `goTo(x, y, z, a)` emits `G53 G0` -- absolute machine.
- `jog(dx, dy, dz)` / `jogA(deg)` / `jogRel(dx, dy, dz, da, f)` all emit
  `$J=G91` (relative).  Sidesteps WCS issues.
- The WorldView's program builder is in WCS -- `WorldView::buildExecuteOperations`
  computes `machineX/Y/Z` as `inFrame - beginWorkInFrame` and the waypoints are
  emitted as absolute WCS coordinates with `G90`.  This is correct because the
  WCS origin matches the begin-work point after the operator's `Set Origin` --
  which `Air::preflightStart` now REQUIRES before any execution.

## Tools / refs

- **Carvera Community Controller**: open-source Python desktop app for the
  Carvera (Makera's official controller is closed).  Best living reference
  for what bytes/commands actually do what.  Search GitHub for
  `Carvera Controller` / `Makera`.
- **Smoothieware**: base firmware.  Real-time bytes and M-code parsing live
  there; Carvera adds the ATC/probe overlays.
- **Grbl 1.1 protocol doc**: foundation for `?` / `~` / `!` / 0x85 / status
  frames / `$J`.  Useful, but DO NOT assume Grbl-only mappings apply to the
  Carvera ATC layer -- see the `*` vs `~` confirmation gotcha above.

## Open questions / things to verify

- **The exact set of M-codes M491 / M492 / M493 / M495 / M496** -- sniffed
  but not fully attributed.  If you trace a tool-change sequence and see
  unknown M-codes flying past, add them above.
- **Soft-limit envelope per axis** -- we currently use a fixed 25 mm chunk
  for continuous-hold jog.  If we ever query `$$` and parse `$130..$132`
  (max travel), `JogSection::kChunkMm` could be made adaptive.
- **A-axis behaviour for soft limits** -- typically the rotary has no soft
  limit but some Carvera builds add one.  Worth verifying before assuming
  large A targets are safe.
