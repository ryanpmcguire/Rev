# clever

A build orchestrator for C++23-modules projects that is *suspicious of dirtiness*.
It rebuilds only what genuinely changed — and it gets there by **lowering modules
to classic headers and sources** rather than fighting the compiler's binary-module
machinery.

If you know Make, CMake, Ninja, and C++ named modules, this README will make sense
quickly. It explains *why* clever exists before it explains *how* to use it, because
the design choices only make sense in light of the problems they dodge.

---

## Why this exists

### Problem 1 — module builds are brittle

A C++23 named-module build is a graph of BMIs (`.pcm` / `.ifc`). Compiling a module
interface produces a binary artifact that every importer must consume, and the
compiler **validates BMI consistency at compile time**: a `.pcm` embeds a hash of the
BMIs it was built against. Rebuild one module and every importer's BMI is now stale.
Feed the compiler a *mix* of fresh and stale BMIs and, with clang, you don't get a
clean diagnostic — you get a segfault ("frontend command failed due to signal").

This makes incremental module builds fragile in exactly the way header builds never
were. Ninja can drive them, but the moment its notion of "up to date" and the
compiler's notion of "BMI-consistent" diverge, you're recovering by nuking BMIs.

### Problem 2 — dirtiness propagates too eagerly

Ninja keys on mtime. Touch a widely-imported module — even to fix a comment — and the
whole dependency cone is marked dirty and rebuilt. The information needed to *not* do
that (did the exported interface actually change? does this consumer even reference the
changed symbol?) exists, but the conventional pipeline throws it away.

### The thesis

clever treats every dirtiness signal as a **hypothesis to disprove**. A file whose
mtime moved is *suspected* dirty, not *convicted*. It is acquitted by an escalating
ladder of cheaper-than-rebuild checks:

| Level | Check | Verdict |
|-------|-------|---------|
| L0 | mtime unchanged | clean |
| L1 | content hash unchanged | clean (touch with no edit) |
| L2 | comment/whitespace-only change | clean (cosmetic) |
| L3 | only function *bodies* changed, interface stable | recompile self, **not** consumers |
| L4 | a signature changed, but a consumer doesn't reference it | that consumer stays clean |

The payoff: a comment in a header imported by 20 files rebuilds **nothing**; a one-line
body edit rebuilds **one** object; an interface change rebuilds **exactly** its real
dependents.

---

## The key move: lower modules to headers

Rather than make L3/L4 safe against the BMI-consistency trap (which is hard, because a
body change still invalidates an importer's BMI), clever sidesteps it entirely.

> **clever transpiles each `.ixx` module into a classic `.hpp` + `.cpp`, then compiles
> and links the old-fashioned way — no BMIs, no module flags at compile time.**

- **Interface → `.hpp`.** Exported declarations. `import X;` becomes `#include "X.hpp"`.
- **Bodies → `.cpp`.** Ordinary function/method definitions are relocated out-of-line
  and qualified (`Class::method`). Templates, `constexpr`, `inline`, deduced-return,
  and other *body-is-interface* cases stay in the header (they have to).

This dissolves both problems at once:

- **No BMI consistency to violate.** It's a header build. Correctness is whatever your
  compiler already guarantees for headers.
- **Dirtiness propagation becomes exact and free.** The `.hpp`/`.cpp` split *is* the
  propagation gate. A body edit changes only the `.cpp` (the `.hpp` is byte-identical,
  so no consumer's dependency set is disturbed). An interface edit changes the `.hpp`,
  and the compiler's own `-MMD` depfiles name precisely which TUs include it. clever
  hashes the depfile headers to decide what recompiles — no graph guessing.

### One mental model explains most of the work

> **Modules isolate; headers leak.**

A module import is an opaque BMI: importing `Foo` does **not** replay `Foo`'s global
module fragment into your translation unit. Flattening modules into headers removes
that isolation, and every "weird" problem clever had to solve is a symptom of it:

- `using namespace` directives that a header propagates to every includer → name
  collisions (`Color`, `Text`, `Svg`) that modules silently masked. Fixed by
  **qualify-on-transpile**: names defined in more than one namespace are rewritten to
  the namespace the file's own imports provide.
- `<winsock2.h>` (from one module) and `<windows.h>`/OLE (pulled transitively by
  another) landing in the *same* TU and colliding — impossible under modules, ordinary
  under headers. Fixed with a small forced-include prelude.
- `windows.h` macros (`near`, `far`, `interface`) clobbering identifiers once they're
  no longer quarantined in a module. Fixed by targeted, per-file `#undef`s.

Naming the model paid for itself repeatedly.

---

## How a build actually runs

```
clever.json  ─►  transpile  ─►  .clever/xpp/<Module>/<stem>.{hpp,cpp}
(the manifest)   (libclang)        │
                                   ▼
                              classic compile (-c, -MMD)  ─►  .clever/obj/…
                                   │
                                   ▼
                              link (llvm-ar / clang)      ─►  .clever/out/…
```

- **`clever.json`** is the project model — clever's analogue of `CMakeLists.txt`:
  compiler/ar/ranlib, language standard, and per-target `{ include_dirs, defines,
  compile_flags, sources, output, depends, link_libraries, … }`. It is generated once
  from an existing CMake build tree and is thereafter the *only* thing clever reads.
  **No CMake is needed to build** — link libraries are resolved to absolute paths at
  generation time.
- **Transpile** uses libclang to comprehend each `.ixx` and emit the `.hpp`/`.cpp`.
  It is CPU-bound (a Python AST walk), so it runs across **processes**.
- **Compile** shells out to the compiler per `.cpp` with `-MMD`, across **threads**.
- **Link** reuses the harvested link recipes; on Windows, vcpkg DLLs are copied next to
  the executable.

### Two layers of incrementality

1. **Content hashes.** A `.cpp` re-transpiles only if its source, parse flags, or an
   embedded resource changed. An object recompiles only if its `.cpp` hash, its flags,
   or any header in its depfile changed.
2. **A structural mtime guard** layered on top: if a source is newer on disk than the
   artifact it should have produced (`.ixx` newer than `.hpp`/`.cpp`, or `.cpp`/header
   newer than `.obj`), it rebuilds **regardless** of the hash. This is the same
   invariant Make/Ninja enforce, and it exists to close one specific hole: a file edited
   *while a build is in flight* could otherwise leave the cache recording a hash for an
   artifact built from older bytes, so the edit silently never reaches the binary. The
   guard can only ever force *more* rebuilding, never less.

---

## Usage

Run everything from the repository root.

### 1. Generate a manifest (one-time, needs a configured CMake build)

```sh
python clever/clever.py init ./CAM/clever.json --cmake-build build
```

This harvests `build/compile_commands.json` (per-target flags) and the Ninja link
edges into a self-contained `clever.json`. Re-run it only when dependencies change;
day-to-day builds never touch CMake again.

### 2. Build (transpile → compile → link)

```sh
python clever/clever.py transpile ./CAM/clever.json -j8
```

- `-j N` — parallel transpile/compile jobs (default: CPU count).
- `--run [--target T]` — launch the executable afterward.
- `--no-embed` — skip the resource-embed pre-step.
- The first argument is the manifest; `.clever/` is created beside it, so a manifest can
  live in any subdirectory and projects stay independent.

### 3. Inspect without building

```sh
python clever/clever.py check ./CAM/clever.json   # read-only build itinerary
python clever/clever.py show  <file.ixx>          # dump one file's comprehension
```

`check` is strictly read-only: it reports clever's dirtiness verdicts (Touched /
Changed / Need-rebuilding) so you can validate its reasoning by hand, and never
advances the baseline.

### Manifest sources support globs

`sources` entries may be literal paths, glob strings, or `{ "glob": …, "exclude": [ … ] }`
objects, mixed freely. Excludes match the absolute path, the manifest-relative path, or
the bare filename — handy for filtering platform variants:

```jsonc
"sources": [
  { "glob": "Rev/src/**/*.ixx",
    "exclude": ["*.mac.ixx", "*.metal.ixx", "*.vulkan.ixx", "*.lnx.ixx", "*.linux.ixx"] },
  "Rev/Resources/.modules/Files.ixx",
  "Rev/external/glm/detail/glm.cpp"
]
```

Globs are expanded once at load time and de-duplicated; everything downstream still sees
a flat, deterministic path list.

---

## VSCode integration

clever produces a real PDB (`-g -gcodeview` + `/pdb:`), so the Microsoft C/C++
extension's `cppvsdbg` debugs it with no custom adapter.

- **`.vscode/launch.json` + `tasks.json`** wire **F5** to build-then-debug.
- **`clever/vscode-ext/`** is a tiny zero-dependency extension that adds a dedicated
  **"Clever"** Output channel (tasks alone can only write to the terminal), runs the
  build into it before launching, and tracks a **current project**: `Clever: Set Current`
  walks up from the active file to the nearest `clever.json`, and F5 builds/debugs
  whatever project you're in — deriving the executable from the manifest.
- **`.ixx` source mapping.** The transpiler emits `#line` directives back to the original
  `.ixx`, so breakpoints, stepping, and call stacks land on the source you actually edit,
  not the generated `.cpp`/`.hpp`.

---

## Status & scope

- Targets **clang on Windows** (MSVC ABI, CodeView/PDB, vcpkg). The architecture isn't
  Windows-specific, but that's what's exercised.
- **libclang is pinned** (`clang==18.1.8`, `libclang==18.1.1`); it is used only to
  *comprehend* source for transpilation, never to generate code, so its version skew vs
  the build compiler is harmless.
- Validated end-to-end on three real apps, including one (CAM) combining OpenCASCADE,
  Windows OLE/shell, and winsock in a single binary.

clever is a deliberate bet: that the most robust way to build C++ modules today is to
**not** build them as modules — to lower them to the header/source model compilers have
gotten right for decades, and spend the saved complexity budget on being genuinely
precise about what changed.
